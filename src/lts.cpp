#include "lts.hpp"
#include "aiger.h"
#include "ic3.hpp"

aiger *safety_reduction(aiger *model) {
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
  const unsigned store{input(safety)};
  std::vector<unsigned> original, copy;
  original.reserve(model->num_latches);
  copy.reserve(model->num_latches);
  for (auto l : latches(model) | lits)
    original.push_back(m(l, latch(safety)));
  for (auto l : latches(model) | lits)
    copy.push_back(m(l, latch(safety)));
  const unsigned stored{latch(safety)}, seen{latch(safety)};

  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  for (auto &l : latches(model)) {
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    aiger_symbol *sl = aiger_is_latch(safety, map[l.lit]);
    assert(sl);
    sl->reset = map[l.reset];
    sl->next = map[l.next];
  }
  for (auto &c : constraints(model)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(safety, map[c.lit], c.name);
  }
  const unsigned J{map[model->justice[0].lits[0]]};
  assert(J != INVALID_LIT);

  aiger_symbol *stored_l = aiger_is_latch(safety, stored);
  assert(stored_l);
  stored_l->next = disj(safety, stored, store);

  aiger_symbol *seen_l = aiger_is_latch(safety, seen);
  assert(seen_l);
  seen_l->next = disj(safety, seen, conj(safety, stored, J));

  for (int i = 0; i < copy.size(); ++i) {
    aiger_symbol *l = aiger_is_latch(safety, copy[i]);
    assert(l);
    unsigned set = conj(safety, stored, copy[i]);
    unsigned unset = conj(safety, aiger_not(stored), original[i]);
    l->next = disj(safety, set, unset);
  }

  assert(original.size() == copy.size()); // Ensure sizes match before zipping
  unsigned B{seen};
  for (size_t i = 0; i < original.size(); ++i)
    B = conj(safety, B, eq(safety, original[i], copy[i]));
  aiger_add_output(safety, B, "lts_bad");
  return safety;
}

void cex_construction(aiger *model, aiger *safety,
                      std::vector<std::vector<unsigned>> &cex) {
  L3 << "Constructing liveness counterexample from safety counterexample";
  const size_t steps = cex.size();
  cex.resize(steps);
  for (size_t t = 0; t < steps; ++t) {
    const auto &safety_state = cex[t];
    std::vector<unsigned> liveness_state;
    liveness_state.reserve(model->num_latches);
    for (auto &l : latches(model)) {
      aiger_symbol *safety_l = aiger_is_latch(safety, l.lit);
      assert(safety_l);
      if (safety_l->next == disj(safety, safety_l->lit, input(safety))) {
        // stored latch
        continue;
      } else if (safety_l->next == disj(safety, safety_l->lit,
                                        conj(safety, latch(safety), input(safety)))) {
        // seen latch
        continue;
      } else {
        liveness_state.push_back(l.lit);
      }
    }
    cex[t] = liveness_state;
  }
}

aiger *witness_construction(aiger *model, aiger *safety,
                             std::vector<std::vector<unsigned>> &cex) {
  L3 << "Constructing liveness witness from safety invariant";
  aiger *witness = aiger_init();
  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) -> unsigned {
    assert(map[from] == INVALID_LIT);
    assert(from != INVALID_LIT && to != INVALID_LIT);
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
    return to;
  };
  m(0, 0);
  for (auto l : inputs(model) | lits)
    m(l, input(witness));
  for (auto l : latches(model) | lits)
    m(l, latch(witness));
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]));
  }
  for (auto &l : latches(model)) {
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    aiger_symbol *wl = aiger_is_latch(witness, map[l.lit]);
    assert(wl);
    wl->reset = map[l.reset];
    wl->next = map[l.next];
  }
  for (auto &c : constraints(model)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(witness, map[c.lit], c.name);
  }
  // invariant is negation of bad output
  // aiger_symbol *bad_l = aiger_is_output(safety, 0);
  // assert(bad_l);
  // aiger_add_output(witness, aiger_not(map[bad_l->lit]), "witness_invariant");
  return witness;
}

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex) {
  L3 << "Reducing liveness to safety";
  aiger *safety = safety_reduction(model);

  bool bug = ic3(safety, cex);
  if (bug)
    cex_construction(model, safety, cex);
  else
    witness = witness_construction(model, safety, cex);
  return bug;
}
