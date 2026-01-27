// Implementation of the rlive algorithm
// Xia et al. - 2024 - Avoiding the Shoals - A New Approach to Liveness
// Checking.pdf
// Or at least my interpretation of it. The certificate construction is based on
// encoding the shoals for the current and the next (or simply other) state.
// Then Q' is defined as a comparison of which shoal is active in the order they
// where found. Crucially the identified not Q states need to be in some shoal,
// but not in the same as the shoal generated for their successors.
// TODO This implementation "grew" soo...

#include "rlive.hpp"

#include "aiger.h"
#include "ic3.hpp"
#include "ternary.hpp"
#include "utils.hpp"

#include <cstring>
#include <unordered_set>

static void to_safety(aiger *model) {
  assert(model);
  assert(model->num_justice);
  const unsigned J = model->justice[0].lits[0];
  if (model->num_bad) {
    model->bad[0].lit = J;
  } else {
    aiger_add_bad(model, J, "bad");
  }
}

static std::pair<unsigned, unsigned> constrain_shoal(aiger *model,
                                                     unsigned shoal_start) {
  L5 << "constraining transition with shoal" << aiger_not(output(model));
  assert(model);
  const unsigned shoal = aiger_not(output(model));
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
  // assumption IC3 does not include inputs in the invariant
  for (auto [l, n] : latches(model) | nexts)
    m(l, n);
  for (auto x : ands(model))
    m(x.lhs, x.lhs);

  for (int i = shoal_start; i < shoal_end; ++i) {
    aiger_and a = model->ands[i];
    L5 << a.lhs << "=" << a.rhs0 << "&" << a.rhs1;
    assert(map[a.rhs0] != INVALID_LIT);
    assert(map[a.rhs1] != INVALID_LIT);
    m(a.lhs, conj(model, map[a.rhs0], map[a.rhs1]));
  }

  const unsigned shoal_n = map[shoal];
  assert(shoal_n != INVALID_LIT);
  // This would be correct and a stronger constraint for the search, but it
  // messes with the certificate generation. While that can be fixed for the
  // proof of the certificate format its not worth the effort now.
  // aiger_add_constraint(model, conj(model, aiger_not(shoal),
  // aiger_not(shoal_n)), "shoal");

  aiger_add_constraint(model, aiger_not(shoal), "shoal");
  L5 << "constrained with shoal literals" << shoal << shoal_n;
  return {shoal, shoal_n};
}

// An encoding of the liveness signal over the next state values is needed to
// encode the shoal comparison correctly. However, in the checks this needs to
// actually reference the real next state values. It is therefore not enough to
// encode Q' over just the next state values when using this construction. In
// theory it is possible to find a witness without this need, and I may have a
// practical construction but imposing the arbitrary restriction to the witness
// seems unwise.
std::pair<aiger *, std::vector<unsigned>> next_live(aiger *model) {
  assert(model);
  assert(model->num_justice == 1);
  aiger *extended = aiger_init();

  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) -> unsigned {
    assert(from < map.size());
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
    return to;
  };
  m(0, 0);

  std::vector<unsigned> next_inputs;
  next_inputs.reserve(model->num_inputs);

  for (unsigned l : inputs(model) | lits)
    m(l, input(extended));
  for (unsigned l : inputs(model) | lits)
    next_inputs.push_back(input(extended));
  for (auto [l, n] : latches(model) | nexts)
    m(l, latch(extended));

  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(extended, map[x], map[y]));
  }

  // add back original latch transition and reset
  for (auto &l : latches(model)) {
    aiger_symbol *nl = aiger_is_latch(extended, map[l.lit]);
    assert(nl);
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    nl->next = map[l.next];
    if (l.reset == l.lit)
      nl->reset = nl->lit;
    else
      nl->reset = map[l.reset];
  }
  for (auto &c : constraints(model)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(extended, map[c.lit], c.name);
  }

  unsigned J = model->justice[0].lits[0];
  assert(map[J] != INVALID_LIT);
  unsigned violations[] = {map[J]};
  aiger_add_justice(extended, 1, violations, "J");

  unsigned i{};
  for (auto x : inputs(model) | lits)
    m(x, next_inputs[i++]);
  for (auto [l, n] : latches(model) | nexts) {
    m(l, map[n]);
    L5 << "mapping latch" << l << "to next" << map[n];
  }
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] != INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(extended, map[x], map[y]));
    L5 << a << "=" << x << "&" << y << "mapped to" << map[a] << "=" << map[x]
       << "&" << map[y];
  }

  assert(map[J] != INVALID_LIT);
  violations[0] = map[J];
  aiger_add_justice(extended, 1, violations, "Jn");

  return {extended, next_inputs};
}

static std::pair<unsigned, unsigned>
constrain_dead_state(aiger *model, const std::vector<bool> &not_q) {
  L5 << "constraining dead state" << not_q;
  assert(model);
  assert(not_q.size() == model->num_latches);
  std::vector<unsigned> dead_lits;
  std::vector<unsigned> dead_n_lits;
  dead_lits.reserve(not_q.size());
  dead_n_lits.reserve(not_q.size());
  size_t i = 0;
  for (auto [l, n] : latches(model) | nexts) {
    const bool val = not_q[i++];
    dead_lits.push_back(val ? l : aiger_not(l));
    dead_n_lits.push_back(val ? n : aiger_not(n));
  }
  const unsigned dead =
      conj(model, model->justice[0].lits[0], conj(model, dead_lits));
  const unsigned dead_n =
      conj(model, model->justice[1].lits[0], conj(model, dead_n_lits));
  aiger_add_constraint(model, aiger_not(dead), nullptr);
  L5 << "constrained dead state" << dead << dead_n;
  return {dead, dead_n};
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

void add_shoal_comparator(aiger *model, const std::vector<unsigned> &S,
                          const std::vector<unsigned> &Sn) {
  assert(model);
  assert(model->num_justice >= 1);
  assert(S.size() == Sn.size());
  if (S.empty()) return;
  std::vector<unsigned> m, mn;
  m.reserve(S.size());
  mn.reserve(S.size());
  m.push_back(S[0]);
  mn.push_back(Sn[0]);
  for (size_t i = 1; i < S.size(); ++i) {
    m.push_back(disj(model, m[i - 1], S[i]));
    mn.push_back(disj(model, mn[i - 1], Sn[i]));
  }
  unsigned Q{1};
  for (size_t i = 0; i < S.size(); ++i)
    Q = conj(model, Q, impl(model, m[i], mn[i]));
  L5 << "increase" << aiger_not(Q);
  model->justice[0].lits[0] = aiger_not(Q);
}

bool rlive(aiger *model, aiger *&witness,
           std::vector<std::vector<unsigned>> &cex) {
  L1 << "Running rlive liveness checker";
  struct search_state {
    std::vector<bool> not_q;
    size_t cex_size;
    bool original_reset;
    search_state(std::vector<bool> n, size_t c, bool original = false)
        : not_q(std::move(n)), cex_size(c), original_reset(original) {}
  };
  std::vector<search_state> stack;
  stack.emplace_back(std::vector<bool>{}, 0, true);
  std::vector<unsigned> S, Sn;
  std::unordered_set<std::vector<bool>> visited;
  unsigned num_og_inputs{model->num_inputs},
      num_og_constraints{model->num_constraints},
      num_og_justice{model->num_justice};
  for (auto &l : latches(model)) // alias
    l.next = conj(model, l.next, l.next);
  aiger *original_model = model;
  auto [extended, next_inputs] = next_live(model);
  std::vector<unsigned> og_reset;
  og_reset.reserve(extended->num_latches);
  for (auto [_, r] : latches(extended) | resets)
    og_reset.push_back(r);
  while (!stack.empty()) {
    auto [violation, depth, original_reset] = stack.back();
    cex.resize(depth);
    if (original_reset) {
      L5 << "setting original reset";
      // initial state, reset to original reset
      assert(og_reset.size() == extended->num_latches);
      unsigned i{};
      for (auto &l : latches(extended))
        l.reset = og_reset[i++];
    } else {
      L5 << "setting reset violation" << violation;
      assert(violation.size() == extended->num_latches);
      unsigned i{};
      for (auto &l : latches(extended))
        l.reset = violation[i++];
    }
    to_safety(extended);
    std::vector<std::vector<unsigned>> safety_cex;
    unsigned shoal_start;
    L5 << "starting search for not q state from reset" << violation;
    bool bug = ic3(extended, safety_cex, &shoal_start, !original_reset);
    if (bug) { // found not q state
      L3 << "possible liveness violation found";
      auto [violation, next] = last_states(extended, safety_cex);
      LV5(violation, next);
      if (cex.empty()) {
        cex = safety_cex;
      } else { // stitch traces
        cex.pop_back();
        cex.insert(cex.end(), safety_cex.begin() + 1, safety_cex.end());
      }
      stack.emplace_back(violation, cex.size());
      L5 << "push" << violation << " -> " << next << "depth" << cex.size();
      auto [_, inserted] = visited.insert(violation);
      if (!inserted) {
        // I don't need to check if not_q is still on the current branch,
        // because if it where closed then we would have added a shoal blocking
        // it.
        L2 << "Found dead loop around" << violation;
        extended->num_inputs = num_og_inputs;
        for (unsigned &x : cex[0])
          x -= 2 * num_og_inputs;
        for (auto &x : cex | std::views::drop(1))
          x.resize(num_og_inputs);
        cex.pop_back();
        for (auto x : cex)
          L3 << x;
        return true;
      }
    } else {
      L5 << "no not q state found from reset" << violation;
      L5 << "pop" << violation << "->" << next << "depth" << cex.size();
      stack.pop_back();
      auto [shoal, shoal_n] = constrain_shoal(extended, shoal_start);

      S.push_back(shoal);
      Sn.push_back(shoal_n);

      if (!original_reset) {
        auto [dead, dead_n] = constrain_dead_state(extended, violation);
        S.push_back(dead);
        Sn.push_back(dead_n);
      }
    }
  }

  L3 << "liveness proven";
  // drop shoal constraints
  for (int i = num_og_constraints; i < extended->num_constraints; ++i)
    extended->constraints[i].lit = 1;
  // drop next liveness
  for (int i = num_og_justice; i < extended->num_justice; ++i)
    extended->justice[i].lits[0] = 0;
  std::vector<unsigned> S_copy{S};
  unsigned S_region = S_copy.empty() ? 1 : disj(extended, S_copy);
  LV5(S_region);
  if (extended->num_bad)
    extended->bad[0].lit = aiger_not(S_region);
  else
    aiger_add_bad(extended, aiger_not(S_region), "liveness");
  add_shoal_comparator(extended, S, Sn);
  witness = extended;
  aiger_reencode(witness);
  for (auto &l : latches(witness)) {
    assert(l.name == nullptr);
    if (aiger_is_constant((l.next))) continue;
    l.name = strdup(("<" + std::to_string(l.next)).c_str());
  }
  unsigned i{};
  for (auto &l : inputs(witness) | std::views::take(num_og_inputs)) {
    assert(l.name == nullptr);
    l.name = strdup(("<" + std::to_string(next_inputs[i++])).c_str());
  }
  return false;
}
