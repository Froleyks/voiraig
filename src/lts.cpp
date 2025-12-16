#include "lts.hpp"
#include "aiger.h"
#include "aiger.hpp"
#include "ic3.hpp"

std::tuple<aiger *, unsigned, unsigned, unsigned>
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
  return {safety, store, stored, J};
}

void cex_construction(aiger *model, std::vector<std::vector<unsigned>> &cex) {
  L3 << "Constructing liveness counterexample from safety counterexample";
  cex[0].resize(model->num_latches);
  for (auto &i : cex | std::views::drop(1))
    i.resize(model->num_inputs);
}

aiger *witness_construction(aiger *model, aiger *safety,
                            std::vector<std::vector<unsigned>> &cex,
                            unsigned store, unsigned stored, unsigned J) {
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
  for (auto [a, x, y] : ands(safety)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(witness, map[x], map[y]));
  }
  for (auto &l : latches(safety)) {
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    aiger_symbol *wl = aiger_is_latch(witness, map[l.lit]);
    assert(wl);
    wl->reset = map[l.reset];
    wl->next = map[l.next];
  }
  for (auto &c : constraints(safety)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(witness, map[c.lit], c.name);
  }
  // store exactly when encountering an unlive state
  assert(map[J] != INVALID_LIT);
  m(store, map[J], true);
  const unsigned B{map[safety->outputs[0].lit]};
  assert(B != INVALID_LIT);
  aiger_add_output(witness, B, nullptr);


  // Add extra counter with L bits, starting once stored, it should saturate
  // when reaching all one
  const unsigned L = model->num_latches;
  std::vector<unsigned> counter_bits;
  counter_bits.reserve(L);
  for (unsigned i = 0; i < L; ++i)
    counter_bits.push_back(latch(witness));

  // Check if all bits are one (saturated)
  unsigned saturated = conj(witness, counter_bits);

  // Increment counter when stored is true and not saturated
  unsigned increment = conj(witness, map[stored], aiger_not(saturated));

  // Compute next state for each counter bit (ripple carry adder)
  std::vector<unsigned> next_counter_bits;
  next_counter_bits.reserve(L);
  unsigned carry = increment;
  for (unsigned i = 0; i < L; ++i) {
    L5 << "encoding counter bit" << counter_bits[i];
    aiger_symbol *bit_l = aiger_is_latch(witness, counter_bits[i]);
    assert(bit_l);
    // XOR for sum: bit XOR carry
    unsigned xor_bit =
        disj(witness, conj(witness, counter_bits[i], aiger_not(carry)),
             conj(witness, aiger_not(counter_bits[i]), carry));
    // next = xor_bit (when incrementing) OR current (when not incrementing or
    // saturated)
    unsigned next_bit = ite(witness, increment, xor_bit, counter_bits[i]);
    bit_l->next = next_bit;
    next_counter_bits.push_back(next_bit);
    // carry_out = current AND carry
    carry = conj(witness, counter_bits[i], carry);
  }

  // Binary comparator: increase is true iff next > current
  // Compare from MSB to LSB: next > current iff exists i where next[i]=1,
  // current[i]=0, and all higher bits are equal
  unsigned increase = 0;                  // false
  unsigned all_higher_equal = aiger_true; // true
  for (int i = L - 1; i >= 0; --i) {
    // next[i] > current[i] means next[i]=1 and current[i]=0
    unsigned bit_greater =
        conj(witness, next_counter_bits[i], aiger_not(counter_bits[i]));
    // This bit position makes next > current if all higher bits were equal
    unsigned this_makes_greater = conj(witness, all_higher_equal, bit_greater);
    increase = disj(witness, increase, this_makes_greater);
    // Update equality: all_higher_equal AND (next[i] == current[i])
    unsigned bits_equal =
        disj(witness, conj(witness, next_counter_bits[i], counter_bits[i]),
             conj(witness, aiger_not(next_counter_bits[i]),
                  aiger_not(counter_bits[i])));
    all_higher_equal = conj(witness, all_higher_equal, bits_equal);
  }

  // I could have done the counter the other way around to be less confusing,
  // but I think its fine
  unsigned violations[] = {increase};
  aiger_add_justice(witness, 1, violations, nullptr);

  // invariant is negation of bad output
  // aiger_symbol *bad_l = aiger_is_output(safety, 0);
  // assert(bad_l);
  // aiger_add_output(witness, aiger_not(map[bad_l->lit]), "witness_invariant");
  return witness;
}

bool lts(aiger *model, aiger *&witness,
         std::vector<std::vector<unsigned>> &cex) {
  L3 << "Reducing liveness to safety";
  auto [safety, store, stored, J] = safety_reduction(model);

  bool bug = ic3(safety, cex);
  if (bug)
    cex_construction(model, cex);
  else
    witness = witness_construction(model, safety, cex, store, stored, J);
  return bug;
}
