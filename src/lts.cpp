#include "lts.hpp"
#include "aiger.h"
#include "aiger.hpp"
#include "ic3.hpp"

std::tuple<aiger *, unsigned, unsigned, unsigned, std::vector<unsigned>,
           std::vector<unsigned>>
safety_reduction(aiger *model) {
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
    copy.push_back(latch(safety));
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
  return {safety, store, stored, J, original, copy};
}

void cex_construction(aiger *model, std::vector<std::vector<unsigned>> &cex) {
  L3 << "Constructing liveness counterexample from safety counterexample";
  cex[0].resize(model->num_latches);
  for (auto &i : cex | std::views::drop(1))
    i.resize(model->num_inputs);
}

aiger *witness_construction(aiger *model, aiger *safety,
                            std::vector<std::vector<unsigned>> &cex,
                            unsigned store, unsigned stored, unsigned J,
                            unsigned og_gates,
                            const std::vector<unsigned> &original,
                            const std::vector<unsigned> &copy) {
  L3 << "Constructing liveness witness from safety invariant";
  aiger *witness = aiger_init();
  std::vector<unsigned> map(size(safety), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to,
                  bool override = false) -> unsigned {
    assert(override || map[from] == INVALID_LIT);
    assert(from != INVALID_LIT && to != INVALID_LIT);
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
    return to;
  };
  m(0, 0);
  for (auto l : inputs(safety) | lits)
    m(l, input(witness));
  for (auto l : latches(safety) | lits)
    m(l, latch(witness));
  for (int i = 0; i < og_gates; ++i) {
    aiger_and *a = safety->ands + i;
    assert(map[a->lhs] == INVALID_LIT);
    assert(map[a->rhs0] != INVALID_LIT);
    assert(map[a->rhs1] != INVALID_LIT);
    m(a->lhs, conj(witness, map[a->rhs0], map[a->rhs1]));
  }
  assert(original.size() == copy.size());
  for (int i = 0; i < original.size(); ++i) {
    aiger_symbol *l = aiger_is_latch(witness, map[original[i]]);
    assert(l);
    aiger_symbol *ol = aiger_is_latch(safety, original[i]);
    assert(ol);
    assert(map[ol->next] != INVALID_LIT);
    l->next = map[ol->next];
    m(copy[i], l->next);
  }
  for (int i = og_gates; i < safety->num_ands; ++i) {
    aiger_and *a = safety->ands + i;
    assert(map[a->lhs] == INVALID_LIT);
    assert(map[a->rhs0] != INVALID_LIT);
    assert(map[a->rhs1] != INVALID_LIT);
    m(a->lhs, conj(witness, map[a->rhs0], map[a->rhs1]));
  }
  unsigned B{map[safety->outputs[0].lit]};
  // B after the intervention has the following semantics:
  // B for some state s -> s is not in a loop
  unsigned violations[] = {B};
  aiger_add_justice(witness, 1, violations, nullptr);

  return witness;
}

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex) {
  L3 << "Reducing liveness to safety";
  auto [safety, store, stored, J, original, copy] = safety_reduction(model);

  bool bug = ic3(safety, cex);
  if (bug)
    cex_construction(model, cex);
  else
    witness = witness_construction(model, safety, cex, store, stored, J,
                                   model->num_ands, original, copy);
  return bug;
}
