#include "generalized.hpp"
#include "complete_liveness.hpp"
#include "general.hpp"
#include "k_liveness.hpp"
#include "l2s.hpp"
#include "rlive.hpp"
#include "safety.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <string>

namespace {
using Clock = std::chrono::steady_clock;
double seconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

// GF of the disjunction of finitely many round-completion pulses is equivalent
// to at least one justice group and every fairness signal recurring forever.
// Original inputs and latches remain a prefix, so traces project by literal.
aiger *normalize(aiger *model) {
  aiger *aig = aiger_init();
  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto bind = [&](unsigned from, unsigned to) {
    map[from] = to;
    map[NOT(from)] = NOT(to);
  };
  bind(0, 0);
  for (auto &i : inputs(model))
    bind(i.lit, input(aig));
  for (auto &l : latches(model))
    bind(l.lit, latch(aig));
  std::vector<std::vector<unsigned>> phases(model->num_justice);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    unsigned count = model->num_fairness + model->justice[j].size;
    if (count > 1)
      for (unsigned i = 0; i < count; ++i)
        phases[j].push_back(latch(aig));
  }
  // RLIVE splices inputs between finite traces. Lower functional resets to
  // self resets plus an initial-only constraint before invoking that engine.
  bool functional = std::any_of(
      latches(model).begin(), latches(model).end(),
      [](const auto &l) { return l.reset > 1 && l.reset != l.lit; });
  unsigned initial = functional ? latch(aig) : 0;
  for (auto &a : ands(model))
    bind(a.lhs, conj(aig, map[a.rhs0], map[a.rhs1]));
  for (auto &l : latches(model)) {
    auto *copy = aiger_is_latch(aig, map[l.lit]);
    copy->next = map[l.next];
    copy->reset = map[l.reset];
    if (l.reset > 1 && l.reset != l.lit) {
      copy->reset = copy->lit;
      aiger_add_constraint(
          aig, impl(aig, initial, eq(aig, copy->lit, map[l.reset])), nullptr);
    }
  }
  if (initial) {
    auto *init = aiger_is_latch(aig, initial);
    init->reset = 1;
    init->next = 0;
  }
  for (auto &c : constraints(model))
    aiger_add_constraint(aig, map[c.lit], nullptr);
  unsigned acceptance = 0;
  for (unsigned j = 0; j < model->num_justice; ++j) {
    std::vector<unsigned> signals;
    for (unsigned i = 0; i < model->num_fairness; ++i)
      signals.push_back(map[model->fairness[i].lit]);
    for (unsigned i = 0; i < model->justice[j].size; ++i)
      signals.push_back(map[model->justice[j].lits[i]]);
    unsigned complete = signals.empty() ? 1 : signals.back();
    if (!phases[j].empty()) {
      complete = conj(aig, phases[j].back(), complete);
      for (unsigned i = 0; i < signals.size(); ++i) {
        unsigned previous = i ? i - 1 : signals.size() - 1;
        auto *p = aiger_is_latch(aig, phases[j][i]);
        p->reset = i == 0;
        p->next = disj(aig, conj(aig, phases[j][i], NOT(signals[i])),
                       conj(aig, phases[j][previous], signals[previous]));
      }
    }
    acceptance = disj(aig, acceptance, complete);
  }
  aiger_add_justice(aig, 1, &acceptance, nullptr);
  return aig;
}

void artifact(aiger *aig, const options &opts, const char *suffix) {
  if (opts.witness_uns)
    write_witness(aig, (std::string(opts.witness_uns) + suffix).c_str());
}
} // namespace

bool generalized_search(aiger *model, const options &opts, aiger *&witness,
                        std::vector<std::vector<unsigned>> &cex,
                        unsigned &property_index, bool &is_justice) {
  const char *engine =
      opts.liveness == 0
          ? (opts.stabilize ? "kliveness-stabilized" : "kliveness-plain")
      : opts.liveness == 1 ? "l2s"
                           : "rlive";
  std::cout << "c search-engine: " << engine << std::endl;
  const auto start = Clock::now();
  if (model->num_bad) {
    aiger *safety_witness = nullptr;
    bool bad = solve_safety(model, opts, safety_witness, cex, property_index);
    if (safety_witness) aiger_reset(safety_witness);
    if (bad) {
      is_justice = false;
      std::cout << "c search-result: sat\nc search-seconds: " << seconds(start)
                << std::endl;
      return true;
    }
    cex.clear();
  }
  aiger *normalized = normalize(model), *native = nullptr;
  if (opts.trace || opts.certificate)
    artifact(normalized, opts, ".search-model.aig");
  bool bug;
  if (opts.liveness == 0)
    bug = k_liveness(normalized, native, cex, opts.stabilize);
  else if (opts.liveness == 1)
    bug = lts(normalized, native, cex);
  else
    bug = rlive(normalized, native, cex);
  std::cout << std::setprecision(9)
            << "c search-result: " << (bug ? "sat" : "unsat")
            << "\nc search-seconds: " << seconds(start) << std::endl;
  if (bug) {
    if (opts.trace && opts.witness_uns)
      write_witness(
          normalized, cex,
          (std::string(opts.witness_uns) + ".search-trace.sat").c_str(), 'j',
          0);
    // Engine cubes can be sparse. Complete them against the ORIGINAL reset,
    // transitions, constraints and a genuine generalized accepting loop.
    for (unsigned t = 0; t < cex.size(); ++t)
      std::erase_if(cex[t], [&](unsigned lit) {
        if ((lit >> 1) > model->maxvar) return true;
        return t ? !aiger_is_input(model, lit & ~1u)
                 : !aiger_is_latch(model, lit & ~1u);
      });
    if (!complete_liveness_trace(model, cex, property_index)) {
      // k-liveness may include the input at the repeated boundary state.
      // Removing that extra frame is safe only if the shorter trace passes
      // the same complete original-model lasso check.
      auto shorter = cex;
      if (shorter.size() > 2) shorter.pop_back();
      if (!complete_liveness_trace(model, shorter, property_index))
        die("selected engine produced no valid original-property lasso");
      cex.swap(shorter);
    }
    is_justice = true;
    if (native && native != normalized) aiger_reset(native);
    aiger_reset(normalized);
    return true;
  }
  if (opts.certificate)
    artifact(native ? native : normalized, opts, ".search-certificate.aig");
  if (native && native != normalized) aiger_reset(native);
  aiger_reset(normalized);
  if (!opts.certificate) return false;

  // A normalized engine rank has only one signal; copying it into each source
  // signal would not preserve the checker's componentwise obligations. Use a
  // separately identified certificate backend over the unchanged source.
  std::cout << "c certificate-backend: generalized-acceptance-budget-ic3"
            << std::endl;
  const auto certificate_start = Clock::now();
  std::vector<std::vector<unsigned>> other_cex;
  unsigned other_index = 0;
  bool other_justice = true;
  if (general_liveness(model, witness, other_cex, other_index, other_justice))
    die("selected liveness engine and original-property certificate backend "
        "disagree");
  std::cout << "c certificate-seconds: " << seconds(certificate_start)
            << std::endl;
  return false;
}
