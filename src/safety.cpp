#include "safety.hpp"

#include "backward.hpp"
#include "cadical.hpp"
#include "ic3.hpp"
#include "kind.hpp"

namespace {
std::span<aiger_symbol> properties(aiger *model) {
  if (model->num_bad) return {model->bad, model->num_bad};
  if (!model->num_justice) return {model->outputs, model->num_outputs};
  return {};
}
} // namespace

bool complete_safety_trace(aiger *model,
                           std::vector<std::vector<unsigned>> &cex,
                           unsigned &property_index) {
  assert(cex.size() >= 2);
  const size_t frames = cex.size() - 1;
  const size_t width = model->maxvar + 1;
  if (frames * width >= INT_MAX) die("counterexample too large");
  CaDiCaL::Solver solver;
  solver.declare_more_variables(frames * width);
  auto literal = [width](size_t t, unsigned lit) {
    int v = t * width + (lit >> 1) + 1;
    return (lit & 1u) ? -v : v;
  };
  auto clause = [&solver](std::initializer_list<int> lits) {
    for (int lit : lits)
      solver.add(lit);
    solver.add(0);
  };
  auto equivalent = [&clause](int a, int b) {
    clause({-a, b});
    clause({a, -b});
  };
  for (size_t t = 0; t < frames; ++t) {
    clause({literal(t, 1)});
    for (auto [a, x, y] : ands(model)) {
      clause({-literal(t, a), literal(t, x)});
      clause({-literal(t, a), literal(t, y)});
      clause({literal(t, a), -literal(t, x), -literal(t, y)});
    }
    for (auto &l : latches(model))
      if (t)
        equivalent(literal(t, l.lit), literal(t - 1, l.next));
      else
        equivalent(literal(0, l.lit), literal(0, l.reset));
    for (auto &c : constraints(model))
      clause({literal(t, c.lit)});
    for (unsigned lit : cex[t + 1])
      if (aiger_is_input(model, lit & ~1u)) clause({literal(t, lit)});
  }
  for (unsigned lit : cex[0])
    if (aiger_is_latch(model, lit & ~1u)) clause({literal(0, lit)});
  for (auto &p : properties(model))
    solver.add(literal(frames - 1, p.lit));
  solver.add(0);
  if (solver.solve() != 10) return false;

  property_index = 0;
  for (auto &p : properties(model)) {
    const int lit = literal(frames - 1, p.lit);
    if (solver.val(lit) == lit) break;
    ++property_index;
  }
  for (auto &cube : cex)
    cube.clear();
  for (auto &l : latches(model))
    cex[0].push_back(l.lit | (solver.val(literal(0, l.lit)) < 0));
  for (size_t t = 0; t < frames; ++t)
    for (auto &i : inputs(model))
      cex[t + 1].push_back(i.lit | (solver.val(literal(t, i.lit)) < 0));
  return true;
}

bool solve_safety(aiger *model, const options &opts, aiger *&witness,
                  std::vector<std::vector<unsigned>> &cex,
                  unsigned &property_index) {
  aiger *safety = aiger_init();
  for (auto &i : inputs(model))
    aiger_add_input(safety, i.lit, nullptr);
  for (auto &l : latches(model)) {
    aiger_add_latch(safety, l.lit, l.next, nullptr);
    aiger_add_reset(safety, l.lit, l.reset);
  }
  for (auto [a, x, y] : ands(model))
    aiger_add_and(safety, a, x, y);
  for (auto &c : constraints(model))
    aiger_add_constraint(safety, c.lit, nullptr);
  // Without justice properties fairness imposes no liveness obligation. Keep
  // its shape in the certificate, with identically false negated rank signals.
  for (unsigned i = 0; i < model->num_fairness; ++i)
    aiger_add_fairness(safety, 1, nullptr);
  std::vector<unsigned> bad;
  for (auto &p : properties(model))
    bad.push_back(p.lit);
  aiger_add_bad(safety, disj(safety, bad), nullptr);

  bool bug;
  if (opts.safety == 0)
    bug = ic3(safety, cex);
  else if (opts.safety == 1)
    bug = kind(safety, witness, cex, opts.paths, opts.unique);
  else if (opts.safety == 2)
    bug = backward(safety, cex, witness, opts.backward_depth,
                   opts.backward_flipping, opts.backward_simulation);
  else
    die("invalid '--safety=%u' (expected 0..2)", opts.safety);

  if (bug) {
    if (!complete_safety_trace(model, cex, property_index))
      die("invalid safety counterexample");
    if (witness && witness != safety) aiger_reset(witness);
    witness = nullptr;
    aiger_reset(safety);
  } else {
    if (!witness) witness = safety;
    // Some engines construct a fresh witness without the vacuous fairness.
    while (witness->num_fairness < model->num_fairness)
      aiger_add_fairness(witness, 1, nullptr);
    if (witness != safety) aiger_reset(safety);
  }
  return bug;
}
