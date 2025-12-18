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
  const unsigned store{input(safety, "store")};
  std::vector<unsigned> original, copy;
  original.reserve(model->num_latches);
  copy.reserve(model->num_latches);
  for (auto l : latches(model) | lits)
    original.push_back(m(l, latch(safety)));
  for (auto l : latches(model))
    copy.push_back(latch(safety, "copy"));
  unsigned stored{latch(safety, "stored")};
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  assert(original.size() == model->num_latches &&
         copy.size() == model->num_latches);
  for (int i = 0; i < model->num_latches; ++i) {
    aiger_symbol *m = model->latches + i;
    aiger_symbol *o = aiger_is_latch(safety, original[i]);
    aiger_symbol *c = aiger_is_latch(safety, copy[i]);
    assert(map[m->reset] != INVALID_LIT);
    assert(map[m->next] != INVALID_LIT);
    o->reset = map[m->reset];
    o->next = map[m->next];
    c->next = ite(safety, store, o->lit, c->lit);
  }
  {
    aiger_symbol *s = aiger_is_latch(safety, stored);
    assert(s);
    s->next = disj(safety, stored, store);
  }

  for (auto &c : constraints(model)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(safety, map[c.lit], c.name);
  }
  const unsigned J{map[model->justice[0].lits[0]]};
  assert(J != INVALID_LIT);

  unsigned B = conj(safety, stored, J);
  for (size_t i = 0; i < original.size(); ++i)
    B = conj(safety, B, eq(safety, original[i], copy[i]));
  aiger_add_output(safety, B, "bad");
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
                            unsigned store, unsigned stored, unsigned og_J,
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
  assert((store & 1u) == 0); // store must be even
  for (auto l : inputs(safety)) {
    // if (l.lit == store) continue;
    m(l.lit, input(witness, l.name));
  }
  for (auto l : original)
    m(l, latch(witness));
  m(stored, 1);
  for (int i = 0; i < og_gates; ++i) {
    unsigned a = safety->ands[i].lhs;
    unsigned x = safety->ands[i].rhs0;
    unsigned y = safety->ands[i].rhs1;
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]));
  }
  for (int i = 0; i < original.size(); ++i) {
    aiger_symbol *sl = aiger_is_latch(safety, original[i]);
    assert(sl);
    aiger_symbol *wl = aiger_is_latch(witness, map[original[i]]);
    assert(wl);
    wl->reset = map[sl->reset];
    wl->next = conj(witness, map[sl->next], map[sl->next]);
  }
  for (auto &c : constraints(safety)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(witness, map[c.lit], c.name);
  }

  assert(original.size() == copy.size());
  for (int i = 0; i < original.size(); ++i) {
    aiger_symbol *wl = witness->latches + i;
    m(copy[i], wl->lit, true);
    m(original[i], wl->next, true);
  }

  // reencode all ands as the invariant could use original gates
  for (auto [a, x, y] : ands(safety)) {
    L5 << a << "(" << (2 * witness->maxvar + 2) << ") =" << x << "(" << map[x]
       << ") & " << y << "(" << map[y] << ")";
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]), true);
  }

  // {
  //   aiger_symbol *sl = aiger_is_latch(safety, stored);
  //   assert(sl);
  //   aiger_symbol *wl = aiger_is_latch(witness, map[stored]);
  //   assert(wl);
  //   assert(map[sl->reset] != INVALID_LIT);
  //   assert(map[sl->next] != INVALID_LIT);
  //   // wl->reset = map[sl->reset];
  //   // wl->next = map[sl->next];
  //   wl->reset = 1;
  //   wl->next = 1;
  // }

  unsigned J{map[safety->outputs[0].lit]};
  unsigned violations[] = {J};
  aiger_add_justice(witness, 1, violations, "Plts_Lc/L_L/L1");

  return witness;
}

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex) {
  L3 << "Reducing liveness to safety";
  auto [safety, store, stored, J, original, copy] = safety_reduction(model);
  aiger_open_and_write_to_file(safety, "l2s.aag");

  bool bug = ic3(safety, cex);
  aiger_open_and_write_to_file(safety, "l2s_wit.aag");
  if (bug)
    cex_construction(model, cex);
  else
    witness = witness_construction(model, safety, cex, store, stored, J,
                                   model->num_ands, original, copy);
  return bug;
}
