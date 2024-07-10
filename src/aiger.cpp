#include "aiger.hpp"

#include <charconv>
#include <cstring>

#include "utils.hpp"


bool is_input(aiger *aig, unsigned l) {
  return aiger_is_input(aig, l);
}

bool is_latch(aiger *aig, unsigned l) {
  return aiger_is_latch(aig, l);
}

unsigned simulates_lit(aiger *aig, unsigned l) {
  assert(is_input(aig, l) || is_latch(aig, l));
  const char *name = aiger_get_symbol(aig, l);
  if (!name || name[0] != '=') return INVALID_LIT;
  unsigned simulated_l;
  [[maybe_unused]] auto [_, err] =
      std::from_chars(name + 2, name + strlen(name), simulated_l);
  assert(err == std::errc());
  return simulated_l;
}

const aiger_symbol *simulates_input(aiger *model, aiger *witness,
                                    unsigned l) {
  const unsigned simulated_l = simulates_lit(witness, l);
  if (simulated_l == INVALID_LIT) return nullptr;
  return aiger_is_input(model, simulated_l);
}

const aiger_symbol *simulates_latch(aiger *model, aiger *witness,
                                    unsigned l) {
  const unsigned simulated_l = simulates_lit(witness, l);
  if (simulated_l == INVALID_LIT) return nullptr;
  return aiger_is_latch(model, simulated_l);
}

unsigned reset(aiger *aig, unsigned l) {
  assert(is_latch(aig, l));
  return aiger_is_latch(aig, l)->reset;
}

unsigned next(aiger *aig, unsigned l) {
  assert(is_latch(aig, l));
  return aiger_is_latch(aig, l)->next;
}

unsigned output(const aiger *aig) {
  if (aig->num_bad)
    return aig->bad[0].lit;
  else if (aig->num_outputs)
    return aig->outputs[0].lit;
  else
    return aiger_false;
}

unsigned size(const aiger *aig) { return (aig->maxvar + 1) * 2; }

unsigned input(aiger *aig) {
  const unsigned new_var{size(aig)};
  aiger_add_input(aig, new_var, nullptr);
  assert(size(aig) != new_var);
  return new_var;
}

unsigned conj(aiger *aig, unsigned x, unsigned y) {
  const unsigned new_var{size(aig)};
  aiger_add_and(aig, new_var, x, y);
  return new_var;
}
unsigned disj(aiger *model, unsigned x, unsigned y) {
  return aiger_not(conj(model, aiger_not(x), aiger_not(y)));
}
// consumes v
template <bool opAnd>
unsigned reduce(aiger *model, std::vector<unsigned> &v) {
  assert(v.size());
  L4 << "reducing" << v;
  const auto begin = v.begin();
  auto end = v.cend();
  bool odd = (begin - end) % 2;
  end -= odd;
  while (end - begin > 1) {
    auto j = begin, i = j;
    while (i != end)
      if constexpr (opAnd)
        *j++ = conj(model, *i++, *i++);
      else
        *j++ = disj(model, *i++, *i++);
    if (odd) *j++ = *end;
    odd = (begin - j) % 2;
    end = j - odd;
  }
  const unsigned res = *begin;
  v.clear();
  L4 << "reduced to" << res;
  return res;
}
unsigned conj(aiger *model, std::vector<unsigned> &v) {
  if (v.empty()) return 1;
  return reduce<true>(model, v);
}
unsigned disj(aiger *model, std::vector<unsigned> &v) {
  if (v.empty()) return 0;
  return reduce<false>(model, v);
}

unsigned impl(aiger *model, unsigned x, unsigned y) {
  return aiger_not(conj(model, x, aiger_not(y)));
}
unsigned eq(aiger *aig, unsigned x, unsigned y) {
  return aiger_not(conj(aig, aiger_not(conj(aig, x, y)),
                        aiger_not(conj(aig, aiger_not(x), aiger_not(y)))));
}
unsigned ite(aiger *model, unsigned i, unsigned t, unsigned e) {
  return disj(model, conj(model, i, t),
                    conj(model, aiger_not(i), e));
}
std::span<aiger_symbol> inputs(const aiger *aig) {
  return {aig->inputs, aig->num_inputs};
}

std::span<aiger_symbol> latches(const aiger *aig) {
  return {aig->latches, aig->num_latches};
}

std::span<aiger_and> ands(const aiger *aig) {
  return {aig->ands, aig->num_ands};
}

bool inputs_latches_reencoded(aiger *aig) {
  unsigned v{2};
  for (unsigned l : inputs(aig) | lits) {
    if (l != v) return false;
    v += 2;
  }
  for (unsigned l : latches(aig) | lits) {
    if (l != v) return false;
    v += 2;
  }
  return true;
}
