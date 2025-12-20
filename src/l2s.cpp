#include "l2s.hpp"
#include "aiger.h"
#include "aiger.hpp"
#include "ic3.hpp"

std::tuple<aiger *, unsigned, unsigned, unsigned, std::vector<unsigned>,
           std::vector<unsigned>, std::vector<unsigned>, std::vector<unsigned>>
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
  std::vector<unsigned> original_inputs, original_latches;
  std::vector<unsigned> copy_inputs, copy_latches;
  original_inputs.reserve(model->num_inputs);
  original_latches.reserve(model->num_latches);
  copy_inputs.reserve(model->num_inputs);
  copy_latches.reserve(model->num_latches);
  for (auto l : inputs(model) | lits)
    original_inputs.push_back(m(l, input(safety)));
  const unsigned store{input(safety, "store")};
  for (auto l : latches(model) | lits)
    original_latches.push_back(m(l, latch(safety)));
  for (auto l : inputs(model) | lits)
    copy_inputs.push_back(latch(safety, "i_copy"));
  for (auto l : latches(model) | lits)
    copy_latches.push_back(latch(safety, "l_copy"));
  assert(original_inputs.size() == model->num_inputs);
  assert(original_latches.size() == model->num_latches);
  assert(copy_inputs.size() == model->num_inputs);
  assert(copy_latches.size() == model->num_latches);
  unsigned stored{latch(safety, "stored")};
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  for (int i = 0; i < model->num_latches; ++i) {
    aiger_symbol *m = model->latches + i;
    aiger_symbol *o = aiger_is_latch(safety, original_latches[i]);
    aiger_symbol *c = aiger_is_latch(safety, copy_latches[i]);
    assert(m && o && c);
    assert(map[m->reset] != INVALID_LIT);
    assert(map[m->next] != INVALID_LIT);
    o->reset = map[m->reset];
    o->next = map[m->next];
  }
  for (size_t i = 0; i < copy_inputs.size(); ++i) {
    aiger_symbol *c = aiger_is_latch(safety, copy_inputs[i]);
    assert(c);
    c->next = ite(safety, store, original_inputs[i], copy_inputs[i]);
  }
  for (size_t i = 0; i < copy_latches.size(); ++i) {
    aiger_symbol *c = aiger_is_latch(safety, copy_latches[i]);
    assert(c);
    c->next = ite(safety, store, original_latches[i], copy_latches[i]);
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
  for (size_t i = 0; i < original_inputs.size(); ++i)
    B = conj(safety, B, eq(safety, original_inputs[i], copy_inputs[i]));
  for (size_t i = 0; i < original_latches.size(); ++i)
    B = conj(safety, B, eq(safety, original_latches[i], copy_latches[i]));
  // B = conj(safety, B, eq(safety, nexts[i], copy[i]));
  aiger_add_output(safety, B, "bad");
  return {safety,           store,       stored,      J, original_inputs,
          original_latches, copy_inputs, copy_latches};
}

void cex_construction(aiger *model, std::vector<std::vector<unsigned>> &cex) {
  L3 << "Constructing liveness counterexample from safety counterexample";
  for (auto x : cex) {
    L5 << x;
  }
  cex[0].resize(model->num_latches);
  for (auto &i : cex | std::views::drop(1))
    i.resize(model->num_inputs);
  // cex.pop_back();
}

aiger *witness_construction(aiger *model, aiger *safety,
                            std::vector<std::vector<unsigned>> &cex,
                            unsigned store, unsigned stored, unsigned og_J,
                            unsigned og_gates,
                            const std::vector<unsigned> &original_inputs,
                            const std::vector<unsigned> &original_latches,
                            const std::vector<unsigned> &copy_inputs,
                            const std::vector<unsigned> &copy_latches) {
  // return safety; // I cannot find this bug with fuzzing
  L3 << "Constructing liveness witness from safety invariant";
  assert(safety->num_inputs == original_inputs.size() + 1);
  assert(safety->num_latches == original_latches.size() + copy_inputs.size() +
                                    copy_latches.size() + 1);
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
  for (auto l : original_inputs)
    m(l, input(witness));
  std::vector<unsigned> new_I;
  new_I.reserve(original_inputs.size());
  for (auto _ : original_inputs)
    new_I.push_back(input(witness));
  m(store, input(witness, "store"));

  for (auto l : original_latches)
    m(l, latch(witness));
  m(stored, 1);

  for (auto [a, x, y] : ands(safety) | std::views::take(og_gates)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]));
  }

  std::vector<unsigned> nexts;
  nexts.reserve(original_latches.size());
  for (size_t i = 0; i < original_latches.size(); ++i) {
    aiger_symbol *sl = aiger_is_latch(safety, original_latches[i]);
    assert(sl);
    aiger_symbol *wl = aiger_is_latch(witness, map[original_latches[i]]);
    assert(wl);
    wl->reset = map[sl->reset];
    wl->next = conj(witness, map[sl->next], map[sl->next]); // alias
    nexts.push_back(wl->next);
  }

  for (auto &c : constraints(safety)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(witness, map[c.lit], c.name);
  }

  assert(original_inputs.size() == copy_inputs.size());
  for (int i = 0; i < copy_inputs.size(); ++i)
    m(copy_inputs[i], map[original_inputs[i]]);
  assert(original_latches.size() == copy_latches.size());
  for (int i = 0; i < copy_latches.size(); ++i)
    m(copy_latches[i], map[original_latches[i]]);
  assert(original_inputs.size() == new_I.size());
  for (int i = 0; i < original_inputs.size(); ++i)
    m(original_inputs[i], new_I[i], true);
  assert(original_latches.size() == witness->num_latches);
  for (int i = 0; i < original_latches.size(); ++i)
    m(original_latches[i], nexts[i], true);

  // reencode all ands as the invariant could use original gates
  for (auto [a, x, y] : ands(safety)) {
    L5 << a << "(" << (2 * witness->maxvar + 2) << ") =" << x << "(" << map[x]
       << ") & " << y << "(" << map[y] << ")";
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]), true);
  }

  unsigned J{map[safety->outputs[0].lit]};
  aiger_add_bad(witness, J, "Plts_Lc/L_L/L1");
  unsigned violations[] = {J};
  aiger_add_justice(witness, 1, violations, "Plts_Lc/L_L/L1");

  return witness;
}

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex) {
  L3 << "Reducing liveness to safety";
  auto [safety, store, stored, J, original_inputs, original_latches,
        copy_inputs, copy_latches] = safety_reduction(model);
  aiger_open_and_write_to_file(safety, "l2s.aag");

  bool bug = ic3(safety, cex);
  if (bug) {
    write_witness(safety, cex, "l2s_wit.aag");
    cex_construction(model, cex);
  } else {
    aiger_open_and_write_to_file(safety, "l2s_wit.aag");
    witness = witness_construction(model, safety, cex, store, stored, J,
                                   model->num_ands, original_inputs,
                                   original_latches, copy_inputs, copy_latches);
  }
  return bug;
}
