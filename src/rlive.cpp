// Implementation of the rlive algorithm
// Xia et al. - 2024 - Avoiding the Shoals - A New Approach to Liveness
// Checking.pdf
#include "rlive.hpp"

#include "aiger.h"
#include "ic3.hpp"
#include "ternary.hpp"
#include "utils.hpp"

#include <cstdint>
#include <set>

static void to_safety(aiger *model) {
  assert(model);
  assert(model->num_justice);
  const unsigned J = model->justice[0].lits[0];
  if (model->num_outputs) {
    model->outputs[0].lit = J;
  } else {
    aiger_add_output(model, J, "bad");
  }
}

static std::pair<unsigned, unsigned>
constrain_transition(aiger *model, unsigned shoal_start) {
  assert(model);
  const unsigned shoal = output(model);
    const unsigned shoal_end = model->num_ands;
  assert(shoal_start <= shoal_end);

  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) -> unsigned {
    LV5(from, to,map.size());
    assert(from < map.size());
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
    return to;
  };
  m(0, 0);
  // assumption IC3 does not include inputs in the invariant?
  // for (auto x : inputs(model) | lits)
  //   m(x, x); // TODO not sure about this
  for (auto [l, n] : latches(model) | nexts)
    m(l, n);
  for (auto x : ands(model))
    m(x.lhs, x.lhs);

  for (int i = shoal_start; i < shoal_end; ++i) {
    aiger_and *a = model->ands + i;
    assert(map[a->rhs0] != INVALID_LIT);
    assert(map[a->rhs1] != INVALID_LIT);
    m(a->lhs, conj(model, map[a->rhs0], map[a->rhs1]));
  }

  const unsigned shoal_n = map[shoal];
  assert(shoal_n != INVALID_LIT);
  aiger_add_constraint(model, conj(model, aiger_not(shoal), aiger_not(shoal_n)),
                       "shoal");
  return {shoal, shoal_n};
}

// Returns a pair of the two last states. The one violating the liveness signal
// and the one after it. Both are returned as vectors of Booleans representing
// the values of latches.
static std::pair<std::vector<bool>, std::vector<bool>>
last_states(aiger *model, const std::vector<std::vector<unsigned>> &cex) {
  assert(model);
  assert(!cex.empty());
  std::vector<bool> not_q, new_reset;
  not_q.reserve(model->num_latches);
  new_reset.reserve(model->num_latches);
  L5 << "replaying stack length" << cex.size() - 1;
  for (auto x : cex)
    L5 << x;

  std::vector<ternary> s(model->maxvar + 1, X);
  s[0] = X0; // constant false
  for (auto l : cex[0])
    s[IDX(l)] = STX(l);
  for (size_t i = 1; i < cex.size(); ++i) {
    for (auto i : cex[i])
      s[IDX(i)] = STX(i);
    propagate(model->ands, model->num_ands, s);
    std::vector<std::pair<unsigned, ternary>> updates;
    updates.reserve(model->num_latches);
    for (auto [l, n] : latches(model) | nexts)
      updates.emplace_back(IDX(l), sign(s[IDX(n)], n));
    L5 << updates;
    if (i == cex.size() - 1)
      for (auto l : latches(model) | lits)
        not_q.push_back(s[IDX(l)] == X1);
    for (auto [i, v] : updates)
      s[i] = v;
    if (i == cex.size() - 1)
      for (auto l : latches(model) | lits)
        new_reset.push_back(s[IDX(l)] == X1);
    L5 << s;
  }
  return {not_q, new_reset};
}

aiger *encode(aiger *model, const std::vector<unsigned> &S,
              const std::vector<unsigned> &Sn,
              const std::vector<bool> &s = {}) {
  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) -> unsigned {
    assert(map[from] == INVALID_LIT);
    assert(from != INVALID_LIT && to != INVALID_LIT);
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
    return to;
  };
  m(0, 0);
  auto *safety = aiger_init();
  for (auto l : inputs(model) | lits)
    m(l, input(safety));
  for (auto l : latches(model) | lits)
    m(l, latch(safety));
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  assert(s.size() == 0 || s.size() == model->num_latches);
  size_t idx{};
  for (auto l : latches(model)) {
    assert(map[l.lit] != INVALID_LIT);
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    aiger_symbol *nl = aiger_is_latch(safety, map[l.lit]);
    assert(nl);
    nl->next = map[l.next];
    if (!s.empty()) {
      assert(idx < s.size());
      nl->reset = s[idx++];
    } else
      nl->reset = map[l.reset];
  }
  for (auto l : constraints(model)) {
    assert(map[l.lit] != INVALID_LIT);
    aiger_add_constraint(safety, map[l.lit], l.name);
  }

  // Observation: Given that we have the list of shoals encoded over current and
  // next state literals, we can simply add the constraint that both are true to
  // ensure that only transitions in the deep (i.e., outside shoals) are
  // considered and that the bad is also within there. I am using a (possibly)
  // slightly modified understanding of shoal where the bad is added to the
  // shoal manually.
  unsigned deep = S.empty() ? 1 : aiger_not(conj(safety, S));
  unsigned deep_n = S.empty() ? 1 : aiger_not(conj(safety, Sn));
  aiger_add_constraint(safety, conj(safety, deep, deep_n), "deep");
  const unsigned J{map[model->justice[0].lits[0]]};
  assert(J != INVALID_LIT);
  aiger_add_output(safety, J, "bad");

  aiger_open_and_write_to_file(safety, "rlive_safety.aag");
  return safety;
}

aiger *build_witness(aiger *model, aiger *safety,
                     const std::vector<unsigned> &S,
                     const std::vector<unsigned> &Sn) {
  return safety;
}

bool rlive(aiger *model, aiger *&witness,
           std::vector<std::vector<unsigned>> &cex) {
  L1 << "Running RLive liveness checker";
  std::vector<unsigned> S, Sn;
  std::vector<std::pair<std::vector<bool>, size_t>> stack;
  static std::set<std::vector<bool>> unlive;
  std::vector<bool> s{};
  stack.emplace_back(s, 0); // initial reset
  while (true) {
    if (s.size()) { // adjust reset
      assert(s.size() == model->num_latches);
      unsigned i = 0;
      for (auto &l : latches(model))
        l.reset = s[i++];
    }
    to_safety(model);
    std::vector<std::vector<unsigned>> safety_cex;
    unsigned shoal_start;
    bool bug = ic3(model, safety_cex, &shoal_start);
    if (bug) { // found not q state
      L3 << "possible liveness violation found";
      auto [not_q, new_reset] = last_states(model, safety_cex);
      LV5(not_q, new_reset);
      s = new_reset;
      if (cex.empty())
        cex = safety_cex;
      else {
        // drop the reset state
        cex.insert(cex.end(), safety_cex.begin() + 1, safety_cex.end());
        stack.emplace_back(not_q, cex.size());
      }
      auto [_, inserted] = unlive.emplace(not_q);
      if (!inserted) {
        // I don't need to check if not_q is still on the current branch,
        // because if it where closed then we would have added a shoal blocking
        // it.
        L2 << "Found dead loop" << not_q;
        return true;
      }
    } else {
      stack.pop_back();
      if (stack.empty()) {
        L3 << "liveness proven";
        return false;
      }
      cex.resize(stack.back().second);
      s = stack.back().first;
      auto [shoal, shoal_n] = constrain_transition(model, shoal_start);
      S.push_back(shoal);
      Sn.push_back(shoal_n);
    }
  }
  assert(false);
  return false;
}
