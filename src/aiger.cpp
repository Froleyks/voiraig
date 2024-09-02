#include "aiger.hpp"

#include <charconv>
#include <cstring>
#include <fstream>

#include "utils.hpp"

bool is_input(aiger *aig, unsigned l) { return aiger_is_input(aig, l); }

bool is_latch(aiger *aig, unsigned l) { return aiger_is_latch(aig, l); }

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

const aiger_symbol *simulates_input(aiger *model, aiger *witness, unsigned l) {
  const unsigned simulated_l = simulates_lit(witness, l);
  if (simulated_l == INVALID_LIT) return nullptr;
  return aiger_is_input(model, simulated_l);
}

const aiger_symbol *simulates_latch(aiger *model, aiger *witness, unsigned l) {
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

unsigned input(aiger *aig, const char *name) {
  const unsigned new_var{size(aig)};
  aiger_add_input(aig, new_var, name);
  assert(size(aig) != new_var);
  return new_var;
}
unsigned oracle(aiger *aig) { return input(aig, "oracle"); }
unsigned latch(aiger *aig, const char *name) {
  const unsigned new_var{size(aig)};
  aiger_add_latch(aig, new_var, new_var, name);
  assert(size(aig) != new_var);
  return new_var;
}

void simulates(aiger *witness, unsigned model_lit, unsigned witness_lit) {
  L4 << witness_lit << "simulates" << model_lit;
  assert(model_lit != INVALID_LIT && witness_lit != INVALID_LIT);
  static constexpr unsigned MAX_NAME_SIZE = 14;
  aiger_symbol *l = aiger_is_input(witness, witness_lit);
  if (!l) l = aiger_is_latch(witness, witness_lit);
  assert(l);
  l->name = static_cast<char *>(malloc(MAX_NAME_SIZE));
  assert(l->name);
  std::snprintf(l->name, MAX_NAME_SIZE, "= %d", model_lit);
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
template <bool opAnd> unsigned reduce(aiger *model, std::vector<unsigned> &v) {
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
  return disj(model, conj(model, i, t), conj(model, aiger_not(i), e));
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
std::span<aiger_symbol> constraints(const aiger *aig) {
  return {aig->constraints, aig->num_constraints};
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

void write_witness(aiger *circuit, const char *path) {
  int err;
  if (path)
    err = aiger_open_and_write_to_file(circuit, path);
  else
    err = aiger_write_to_file(circuit, aiger_ascii_mode, stdout);
  if (!err) // the write proctions return zero on error...
    die("failed to write witness");
}

void expand(std::ostream &o, const std::vector<unsigned> &c,
            aiger_symbol *first_symbol, unsigned n) {
  if (!first_symbol) return;
  L3 << "expand" << c << "from" << first_symbol->lit << "to length" << n;
  unsigned i = (first_symbol->lit / 2) + 1;
  for (auto l : c) {
    const unsigned v = l / 2;
    for (; i < v; i++)
      o << 'x';
    o << (~l & 1u);
    i += 1;
  }
  for (; i < n; i++)
    o << 'x';
}

// The cex format:
// 0: Cube that contains the literals for the uninitialized latches the value of
// which is necessary for the trace. May be bigger.
// rest: Cube of inputs from initial to bad necessary for the trace.
void write_witness(aiger *model, const std::vector<std::vector<unsigned>> &cex,
                   const char *path) {
  L1 << "writing counter example";
  std::ofstream f;
  if (path) {
    f.open(path);
    if (!f.is_open()) die("cannot write %s", path);
  }
  std::ostream &o = (path ? f : std::cout);
  o << "1\nb0\n";
  expand(o, cex[0], model->latches, model->num_inputs + model->num_latches);
  o << "\n";
  for (unsigned i = 1; i < cex.size(); ++i) {
    expand(o, cex[i], model->inputs, model->num_inputs);
    o << "\n";
  }
  o << "." << std::endl;
}
