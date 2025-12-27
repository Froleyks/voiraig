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
constrain_transition(aiger *model, const std::vector<bool> &not_q,
                     unsigned shoal_start) {
  L5 << "manually adding" << not_q << "to shoal" << aiger_not(output(model));
  assert(model);
  std::vector<unsigned> q_violation;
  q_violation.reserve(model->num_latches);
  // LV5(not_q);
  assert(model->num_latches == not_q.size());
  unsigned i{};
  for (auto l : latches(model) | lits) {
    q_violation.push_back(l ^ (not_q[i++] ? 0u : 1u));
  }
  LV5(q_violation);
  const unsigned shoal =
      disj(model, conj(model, q_violation), aiger_not(output(model)));
  const unsigned shoal_end = model->num_ands;
  assert(shoal_start <= shoal_end);

  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) -> unsigned {
    // LV5(from, to, map.size());
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

  // I feel like I get points to a gate and modify before using it quite a bit
  // in my code... Those are all bugs.
  for (int i = shoal_start; i < shoal_end; ++i) {
    aiger_and a = model->ands[i];
    L5 << a.lhs << "=" << a.rhs0 << "&" << a.rhs1;
    assert(map[a.rhs0] != INVALID_LIT);
    assert(map[a.rhs1] != INVALID_LIT);
    m(a.lhs, conj(model, map[a.rhs0], map[a.rhs1]));
  }

  const unsigned shoal_n = map[shoal];
  assert(shoal_n != INVALID_LIT);
  // aiger_add_constraint(model, conj(model, aiger_not(shoal),
  // aiger_not(shoal_n)), "shoal");
  aiger_add_constraint(model, aiger_not(shoal), "shoal");
  L5 << "constrained with shoal literals" << shoal << shoal_n;
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

aiger *build_witness(aiger *model, const std::vector<unsigned> &S,
                     const std::vector<unsigned> &Sn) {
  assert(model);
  assert(model->num_justice == 1);
  assert(S.size() == Sn.size());
  unsigned increase = 0;
  unsigned prefix_equal = 1;
  for (size_t i = 0; i < S.size(); ++i) {
    const unsigned sn = Sn[i];
    const unsigned s = S[i];
    const unsigned gt_bit = conj(model, sn, aiger_not(s));
    const unsigned gt_here = conj(model, prefix_equal, gt_bit);
    increase = disj(model, increase, gt_here);
    prefix_equal = conj(model, prefix_equal, eq(model, sn, s));
  }
  model->justice[0].lits[0] = increase;
  return model;
}

bool rlive(aiger *model, aiger *&witness,
           std::vector<std::vector<unsigned>> &cex) {
  L1 << "Running RLive liveness checker";
  std::vector<unsigned> S, Sn;
  std::vector<std::tuple<std::vector<bool>, std::vector<bool>, size_t>> stack;
  static std::set<std::vector<bool>> unlive;
  unsigned num_og_constraints = model->num_constraints;
  std::vector<unsigned> og_reset;
  og_reset.reserve(model->num_latches);
  for (auto [_, r] : latches(model) | resets)
    og_reset.push_back(r);
  std::vector<bool> s{};
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
    L5 << "starting search for not q state from reset" << s;
    aiger_open_and_write_to_file(model, "rlive_safety.aag");
    bool bug = ic3(model, safety_cex, &shoal_start);
    if (bug) { // found not q state
      L3 << "possible liveness violation found";
      auto [not_q, new_reset] = last_states(model, safety_cex);
      LV5(not_q, new_reset);
      s = new_reset;
      if (cex.empty()) {
        cex = safety_cex;
        std::vector<bool> pseudo_violation;
        pseudo_violation.reserve(model->num_latches);
        assert(cex[0].size() == model->num_latches);
        for (auto l : cex[0])
          pseudo_violation.push_back(!aiger_sign(l));
        stack.emplace_back(pseudo_violation, pseudo_violation, 0);
        L5 << "push not_q" << pseudo_violation << "new reset"
           << pseudo_violation << "cex size" << cex.size();
      } else // drop the reset state
        cex.insert(cex.end(), safety_cex.begin() + 1, safety_cex.end());
      stack.emplace_back(not_q, new_reset, cex.size());
      L5 << "push not_q" << not_q << "new reset" << new_reset << "cex size"
         << cex.size();
      auto [_, inserted] = unlive.emplace(not_q);
      if (!inserted) {
        // I don't need to check if not_q is still on the current branch,
        // because if it where closed then we would have added a shoal blocking
        // it.
        L2 << "Found dead loop" << not_q;
        return true;
      }
    } else {
      L5 << "no not q state found from reset" << s;
      if (stack.size() == 1) {
        L3 << "liveness proven";
        unsigned i{};
        for (auto &l : latches(model))
          l.reset = og_reset[i++];
        for (auto &l : // remove shoal constraints
             constraints(model) | std::views::drop(num_og_constraints))
          l.lit = 1;
        std::vector<unsigned> S_copy{S};
        model->outputs[0].lit = aiger_not(disj(model, S_copy));
        witness = build_witness(model, S, Sn);
        return false;
      }
      auto [not_q, new_reset, cex_size] = stack.back();
      stack.pop_back();
      L5 << "pop not_q" << not_q << "new reset" << new_reset << "cex size"
         << cex_size;
      auto [shoal, shoal_n] = constrain_transition(model, not_q, shoal_start);
      S.push_back(shoal);
      Sn.push_back(shoal_n);
      cex.resize(cex_size);
      if (stack.size()) s = std::get<1>(stack.back());
    }
  }
  assert(false);
  return false;
}
