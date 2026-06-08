#include "backward.hpp"

#include "cadical.hpp"
#include "kind.hpp"
#include "ternary.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <vector>

namespace {

using Cube = std::vector<unsigned>;
using Trace = std::vector<std::vector<ternary>>;

static constexpr int SAT_FALSE = 1;
static constexpr int SAT_TRUE = -1;

ternary ternary_lit(const std::vector<ternary> &s, unsigned lit) {
  if (lit == 0) return X0;
  if (lit == 1) return X1;
  return sign(s[IDX(lit)], lit);
}

ternary ternary_cube(const std::vector<ternary> &s, const Cube &cube) {
  ternary res = X1;
  for (unsigned lit : cube)
    res = ternary_and(res, ternary_lit(s, lit));
  return res;
}

bool subsumes(const Cube &small, const Cube &big) {
  auto s = small.begin(), b = big.begin();
  const auto S = small.end(), B = big.end();
  while (s != S && b != B)
    s += (*s == *b++);
  return s == S;
}

struct SparsificationStats {
  unsigned flipping_attempts{};
  unsigned flipping_successes{};
  unsigned simulation_attempts{};
  unsigned simulation_successes{};
  unsigned simulation_reverts{};
  unsigned already_x{};
};

struct LearnedCube {
  Cube cube;
  int last_indicator;
  std::vector<Cube> suffix;
};

class BackwardEncoding {
  aiger *model;
  const unsigned k;
  const unsigned original_bad;
  CaDiCaL::Solver solver;
  int next_var;
  int declared_vars{};
  std::vector<int> reset_state;
  std::vector<LearnedCube> learned;
  SparsificationStats stats;
  unsigned iterations{};

  void ensure_declared(int highest_var) {
    if (highest_var <= declared_vars) return;
    const int target = std::max(highest_var, declared_vars + 128);
    solver.declare_more_variables(target - declared_vars);
    declared_vars = target;
  }

  int new_var() {
    const int res = next_var++;
    ensure_declared(res);
    return res;
  }

  int lit(unsigned t, unsigned aig_lit) const {
    assert(t <= k);
    if (aig_lit <= 1) return aig_lit ? SAT_TRUE : SAT_FALSE;
    const int var = 2 + t * model->maxvar + (IDX(aig_lit) - 1);
    return SGN(aig_lit) ? -var : var;
  }

  void add_clause(std::initializer_list<int> clause) {
    for (int l : clause)
      solver.add(l);
    solver.add(0);
  }

  void add_clause(const std::vector<int> &clause) {
    for (int l : clause)
      solver.add(l);
    solver.add(0);
  }

  int and_lit(int lhs, int rhs) {
    if (lhs == SAT_FALSE || rhs == SAT_FALSE) return SAT_FALSE;
    if (lhs == SAT_TRUE) return rhs;
    if (rhs == SAT_TRUE) return lhs;
    if (lhs == rhs) return lhs;
    const int res = new_var();
    add_clause({-res, lhs});
    add_clause({-res, rhs});
    add_clause({res, -lhs, -rhs});
    return res;
  }

  int equiv_lit(int lhs, int rhs) {
    if (lhs == rhs) return SAT_TRUE;
    if (lhs == -rhs) return SAT_FALSE;
    const int res = new_var();
    add_clause({-res, -lhs, rhs});
    add_clause({-res, lhs, -rhs});
    add_clause({res, lhs, rhs});
    add_clause({res, -lhs, -rhs});
    return res;
  }

  void add_gate(unsigned t, const aiger_and &gate) {
    const int lhs = lit(t, gate.lhs);
    const int rhs0 = lit(t, gate.rhs0);
    const int rhs1 = lit(t, gate.rhs1);
    add_clause({-lhs, rhs0});
    add_clause({-lhs, rhs1});
    add_clause({lhs, -rhs0, -rhs1});
  }

  void add_equiv(int lhs, int rhs) {
    if (lhs == rhs) return;
    add_clause({-lhs, rhs});
    add_clause({lhs, -rhs});
  }

  int encode_reset_state(unsigned t) {
    int res = SAT_TRUE;
    for (const auto &latch : latches(model)) {
      const int latch_lit = lit(t, latch.lit);
      const int reset_lit = lit(t, latch.reset);
      res = and_lit(res, equiv_lit(latch_lit, reset_lit));
    }
    return res;
  }

  void forbid_cube(unsigned t, const Cube &cube) {
    std::vector<int> clause;
    clause.reserve(cube.size());
    for (unsigned l : cube)
      clause.push_back(lit(t, NOT(l)));
    add_clause(clause);
  }

  bool solver_lit_true(int solver_lit) {
    return solver.val(solver_lit) == solver_lit;
  }

  bool cube_exists_or_is_subsumed(const Cube &cube) const {
    return std::any_of(learned.begin(), learned.end(), [&](const auto &l) {
      return subsumes(l.cube, cube);
    });
  }

  void add_last_indicator(const Cube &cube, std::vector<Cube> suffix) {
    const int indicator = new_var();
    for (unsigned l : cube)
      add_clause({-indicator, lit(k, l)});
    learned.push_back({cube, indicator, std::move(suffix)});
  }

  ternary bad_value(const std::vector<ternary> &s) const {
    ternary res = ternary_lit(s, original_bad);
    for (const auto &l : learned)
      res = ternary_or(res, ternary_cube(s, l.cube));
    return res;
  }

  void simulate_suffix(Trace &trace, unsigned first) const {
    for (unsigned t = first; t <= k; ++t) {
      propagate(model->ands, model->num_ands, trace[t]);
      if (t == k) break;
      for (auto [l, n] : latches(model) | nexts)
        trace[t + 1][IDX(l)] = ternary_lit(trace[t], n);
    }
  }

  Cube state_cube(const std::vector<ternary> &s) const {
    Cube cube;
    cube.reserve(model->num_latches);
    for (unsigned l : latches(model) | lits) {
      const ternary v = s[IDX(l)];
      if (!v) continue;
      cube.push_back(l | XTS(v));
    }
    assert(std::is_sorted(cube.begin(), cube.end()));
    return cube;
  }

  Cube solver_state_cube(unsigned t) {
    Cube cube;
    cube.reserve(model->num_latches);
    for (unsigned l : latches(model) | lits) {
      const int sat_lit = lit(t, l);
      cube.push_back(l | (solver_lit_true(sat_lit) ? 0u : 1u));
    }
    return cube;
  }

  Cube solver_input_cube(unsigned t) {
    Cube cube;
    cube.reserve(model->num_inputs);
    for (unsigned l : inputs(model) | lits) {
      const int sat_lit = lit(t, l);
      cube.push_back(l | (solver_lit_true(sat_lit) ? 0u : 1u));
    }
    return cube;
  }

  Cube trace_input_cube(const Trace &trace, unsigned t) const {
    Cube cube;
    cube.reserve(model->num_inputs);
    for (unsigned l : inputs(model) | lits) {
      const ternary v = trace[t][IDX(l)];
      assert(v);
      cube.push_back(l | XTS(v));
    }
    return cube;
  }

  const LearnedCube *learned_terminal(const Trace &trace) const {
    for (const auto &learned_cube : learned)
      if (ternary_cube(trace[k], learned_cube.cube) == X1)
        return &learned_cube;
    return nullptr;
  }

  std::vector<Cube> trace_suffix(const Trace &trace, unsigned first) const {
    std::vector<Cube> suffix;
    if (ternary_lit(trace[k], original_bad) == X1) {
      suffix.reserve(k - first + 1);
      for (unsigned t = first; t <= k; ++t)
        suffix.push_back(trace_input_cube(trace, t));
      return suffix;
    }

    const LearnedCube *terminal = learned_terminal(trace);
    if (!terminal) die("backward trace terminal is not known bad");

    suffix.reserve(k - first + terminal->suffix.size());
    for (unsigned t = first; t < k; ++t)
      suffix.push_back(trace_input_cube(trace, t));
    suffix.insert(suffix.end(), terminal->suffix.begin(),
                  terminal->suffix.end());
    return suffix;
  }

public:
  BackwardEncoding(aiger *model, unsigned k)
      : model(model), k(k), original_bad(output(model)),
        next_var(2 + (k + 1) * model->maxvar) {
    ensure_declared(next_var - 1);
    add_clause({SAT_TRUE});

    reset_state.reserve(k + 1);
    for (unsigned t = 0; t <= k; ++t) {
      for (const auto &gate : ands(model))
        add_gate(t, gate);
      for (const auto &constraint : constraints(model))
        add_clause({lit(t, constraint.lit)});
      reset_state.push_back(encode_reset_state(t));
    }

    for (unsigned t = 0; t < k; ++t) {
      for (const auto &latch : latches(model))
        add_equiv(lit(t + 1, latch.lit), lit(t, latch.next));
      add_clause({lit(t, NOT(original_bad))});
    }
  }

  void assume_last_bad() {
    solver.constrain(lit(k, original_bad));
    for (const auto &l : learned)
      solver.constrain(l.last_indicator);
    solver.constrain(0);
  }

  int solve() {
    iterations++;
    assume_last_bad();
    return solver.solve();
  }

  bool reset_reaches_bad(std::vector<std::vector<unsigned>> &cex) const {
    assert(cex.empty());
    for (unsigned depth = 0; depth <= k; ++depth) {
      CaDiCaL::Solver checker;
      int next_checker_var = 2 + (depth + 1) * model->maxvar;
      int declared_checker_vars = 0;

      auto checker_ensure_declared = [&checker, &declared_checker_vars](
                                         int highest_var) {
        if (highest_var <= declared_checker_vars) return;
        const int target = std::max(highest_var, declared_checker_vars + 128);
        checker.declare_more_variables(target - declared_checker_vars);
        declared_checker_vars = target;
      };
      checker_ensure_declared(next_checker_var - 1);

      auto checker_lit = [this, depth](unsigned t, unsigned aig_lit) -> int {
        assert(t <= depth);
        if (aig_lit <= 1) return aig_lit ? SAT_TRUE : SAT_FALSE;
        const int var = 2 + t * model->maxvar + (IDX(aig_lit) - 1);
        return SGN(aig_lit) ? -var : var;
      };
      auto checker_clause = [&checker](std::initializer_list<int> clause) {
        for (int l : clause)
          checker.add(l);
        checker.add(0);
      };
      auto checker_vector_clause = [&checker](const std::vector<int> &clause) {
        for (int l : clause)
          checker.add(l);
        checker.add(0);
      };
      auto checker_new_var = [&checker_ensure_declared, &next_checker_var]() {
        const int res = next_checker_var++;
        checker_ensure_declared(res);
        return res;
      };
      auto checker_and = [&](int lhs, int rhs) {
        if (lhs == SAT_FALSE || rhs == SAT_FALSE) return SAT_FALSE;
        if (lhs == SAT_TRUE) return rhs;
        if (rhs == SAT_TRUE) return lhs;
        if (lhs == rhs) return lhs;
        const int res = checker_new_var();
        checker_clause({-res, lhs});
        checker_clause({-res, rhs});
        checker_clause({res, -lhs, -rhs});
        return res;
      };
      auto checker_equiv = [&](int lhs, int rhs) {
        if (lhs == rhs) return SAT_TRUE;
        if (lhs == -rhs) return SAT_FALSE;
        const int res = checker_new_var();
        checker_clause({-res, -lhs, rhs});
        checker_clause({-res, lhs, -rhs});
        checker_clause({res, lhs, rhs});
        checker_clause({res, -lhs, -rhs});
        return res;
      };

      checker_clause({SAT_TRUE});
      for (unsigned t = 0; t <= depth; ++t) {
        for (const auto &gate : ands(model)) {
          const int lhs = checker_lit(t, gate.lhs);
          const int rhs0 = checker_lit(t, gate.rhs0);
          const int rhs1 = checker_lit(t, gate.rhs1);
          checker_clause({-lhs, rhs0});
          checker_clause({-lhs, rhs1});
          checker_clause({lhs, -rhs0, -rhs1});
        }
        for (const auto &constraint : constraints(model))
          checker_clause({checker_lit(t, constraint.lit)});
      }
      for (unsigned t = 0; t < depth; ++t)
        for (const auto &latch : latches(model)) {
          checker_clause(
              {-checker_lit(t + 1, latch.lit), checker_lit(t, latch.next)});
          checker_clause(
              {checker_lit(t + 1, latch.lit), -checker_lit(t, latch.next)});
        }

      int reset = SAT_TRUE;
      for (const auto &latch : latches(model))
        reset = checker_and(
            reset, checker_equiv(checker_lit(0, latch.lit),
                                 checker_lit(0, latch.reset)));
      checker_clause({reset});

      std::vector<int> bad_clause{checker_lit(depth, original_bad)};
      bad_clause.reserve(learned.size() + 1);
      std::vector<std::pair<int, const LearnedCube *>> learned_indicators;
      learned_indicators.reserve(learned.size());
      for (const auto &learned_cube : learned) {
        const int indicator = checker_new_var();
        for (unsigned l : learned_cube.cube)
          checker_clause({-indicator, checker_lit(depth, l)});
        bad_clause.push_back(indicator);
        learned_indicators.push_back({indicator, &learned_cube});
      }
      checker_vector_clause(bad_clause);

      const int res = checker.solve();
      if (res == 20) continue;
      if (res != 10) die("backward bounded reset checker returned unknown");

      auto checker_lit_true = [&checker](int l) { return checker.val(l) == l; };
      cex.emplace_back();
      cex.back().reserve(model->num_latches);
      for (unsigned l : latches(model) | lits)
        cex.back().push_back(
            l | (checker_lit_true(checker_lit(0, l)) ? 0u : 1u));

      auto push_input_cube = [&](unsigned t) {
        cex.emplace_back();
        cex.back().reserve(model->num_inputs);
        for (unsigned l : inputs(model) | lits)
          cex.back().push_back(
              l | (checker_lit_true(checker_lit(t, l)) ? 0u : 1u));
      };

      if (checker_lit_true(checker_lit(depth, original_bad))) {
        for (unsigned t = 0; t <= depth; ++t)
          push_input_cube(t);
        return true;
      }

      for (auto [indicator, learned_cube] : learned_indicators)
        if (checker_lit_true(indicator)) {
          for (unsigned t = 0; t < depth; ++t)
            push_input_cube(t);
          cex.insert(cex.end(), learned_cube->suffix.begin(),
                     learned_cube->suffix.end());
          return true;
        }
      die("backward bounded reset checker did not select a bad target");
    }
    return false;
  }

  bool reset_is_bad(std::vector<std::vector<unsigned>> &cex) const {
    CaDiCaL::Solver checker;
    int next_checker_var = 2 + model->maxvar;
    int declared_checker_vars = 0;

    auto checker_ensure_declared = [&checker, &declared_checker_vars](
                                       int highest_var) {
      if (highest_var <= declared_checker_vars) return;
      const int target = std::max(highest_var, declared_checker_vars + 128);
      checker.declare_more_variables(target - declared_checker_vars);
      declared_checker_vars = target;
    };
    checker_ensure_declared(next_checker_var - 1);

    auto checker_lit = [this](unsigned aig_lit) -> int {
      if (aig_lit <= 1) return aig_lit ? SAT_TRUE : SAT_FALSE;
      const int var = 2 + (IDX(aig_lit) - 1);
      return SGN(aig_lit) ? -var : var;
    };
    auto checker_clause = [&checker](std::initializer_list<int> clause) {
      for (int l : clause)
        checker.add(l);
      checker.add(0);
    };
    auto checker_vector_clause = [&checker](const std::vector<int> &clause) {
      for (int l : clause)
        checker.add(l);
      checker.add(0);
    };
    auto checker_new_var = [&checker_ensure_declared, &next_checker_var]() {
      const int res = next_checker_var++;
      checker_ensure_declared(res);
      return res;
    };
    auto checker_and = [&](int lhs, int rhs) {
      if (lhs == SAT_FALSE || rhs == SAT_FALSE) return SAT_FALSE;
      if (lhs == SAT_TRUE) return rhs;
      if (rhs == SAT_TRUE) return lhs;
      if (lhs == rhs) return lhs;
      const int res = checker_new_var();
      checker_clause({-res, lhs});
      checker_clause({-res, rhs});
      checker_clause({res, -lhs, -rhs});
      return res;
    };
    auto checker_equiv = [&](int lhs, int rhs) {
      if (lhs == rhs) return SAT_TRUE;
      if (lhs == -rhs) return SAT_FALSE;
      const int res = checker_new_var();
      checker_clause({-res, -lhs, rhs});
      checker_clause({-res, lhs, -rhs});
      checker_clause({res, lhs, rhs});
      checker_clause({res, -lhs, -rhs});
      return res;
    };

    checker_clause({SAT_TRUE});
    for (const auto &gate : ands(model)) {
      const int lhs = checker_lit(gate.lhs);
      const int rhs0 = checker_lit(gate.rhs0);
      const int rhs1 = checker_lit(gate.rhs1);
      checker_clause({-lhs, rhs0});
      checker_clause({-lhs, rhs1});
      checker_clause({lhs, -rhs0, -rhs1});
    }
    for (const auto &constraint : constraints(model))
      checker_clause({checker_lit(constraint.lit)});

    int reset = SAT_TRUE;
    for (const auto &latch : latches(model))
      reset = checker_and(
          reset, checker_equiv(checker_lit(latch.lit),
                               checker_lit(latch.reset)));
    checker_clause({reset});

    std::vector<int> bad_clause{checker_lit(original_bad)};
    bad_clause.reserve(learned.size() + 1);
    std::vector<std::pair<int, const LearnedCube *>> learned_indicators;
    learned_indicators.reserve(learned.size());
    for (const auto &learned_cube : learned) {
      const int indicator = checker_new_var();
      for (unsigned l : learned_cube.cube)
        checker_clause({-indicator, checker_lit(l)});
      bad_clause.push_back(indicator);
      learned_indicators.push_back({indicator, &learned_cube});
    }
    checker_vector_clause(bad_clause);

    const int res = checker.solve();
    if (res == 20) return false;
    if (res != 10) die("backward reset checker returned unknown");

    auto checker_lit_true = [&checker](int l) { return checker.val(l) == l; };
    assert(cex.empty());
    cex.emplace_back();
    cex.back().reserve(model->num_latches);
    for (unsigned l : latches(model) | lits)
      cex.back().push_back(l | (checker_lit_true(checker_lit(l)) ? 0u : 1u));
    if (!checker_lit_true(checker_lit(original_bad))) {
      for (auto [indicator, learned_cube] : learned_indicators)
        if (checker_lit_true(indicator)) {
          cex.insert(cex.end(), learned_cube->suffix.begin(),
                     learned_cube->suffix.end());
          return true;
        }
      die("backward reset checker did not select a bad target");
    }
    cex.emplace_back();
    cex.back().reserve(model->num_inputs);
    for (unsigned l : inputs(model) | lits)
      cex.back().push_back(l | (checker_lit_true(checker_lit(l)) ? 0u : 1u));
    return true;
  }

  std::optional<unsigned> reset_position() {
    for (unsigned t = k + 1; t-- > 0;)
      if (solver_lit_true(reset_state[t])) return t;
    return {};
  }

  Trace trace() {
    Trace res(k + 1, std::vector<ternary>(model->maxvar + 1, X));
    for (unsigned t = 0; t <= k; ++t) {
      res[t][0] = X0;
      for (unsigned l : inputs(model) | lits)
        res[t][IDX(l)] = solver_lit_true(lit(t, l)) ? X1 : X0;
      for (unsigned l : latches(model) | lits)
        res[t][IDX(l)] = solver_lit_true(lit(t, l)) ? X1 : X0;
      propagate(model->ands, model->num_ands, res[t]);
    }
    return res;
  }

  void build_cex(std::vector<std::vector<unsigned>> &cex,
                 unsigned reset_at) {
    assert(cex.empty());
    cex.reserve(k - reset_at + 2);
    cex.push_back(solver_state_cube(reset_at));
    if (solver_lit_true(lit(k, original_bad))) {
      for (unsigned t = reset_at; t <= k; ++t)
        cex.push_back(solver_input_cube(t));
      return;
    }
    for (const auto &learned_cube : learned)
      if (solver_lit_true(learned_cube.last_indicator)) {
        for (unsigned t = reset_at; t < k; ++t)
          cex.push_back(solver_input_cube(t));
        cex.insert(cex.end(), learned_cube.suffix.begin(),
                   learned_cube.suffix.end());
        return;
      }
    die("backward solver did not select a bad target");
  }

  void sparsify_by_flipping(Trace &trace) {
    for (unsigned t = 0; t <= k; ++t) {
      for (unsigned l : latches(model) | lits) {
        ternary &value = trace[t][IDX(l)];
        if (!value) {
          stats.already_x++;
          continue;
        }
        stats.flipping_attempts++;
        if (!solver.flippable(std::abs(lit(t, l)))) continue;
        Trace candidate = trace;
        candidate[t][IDX(l)] = X;
        simulate_suffix(candidate, t);
        if (bad_value(candidate[k]) != X1) continue;
#ifndef NDEBUG
        assert(bad_value(candidate[k]) == X1);
#endif
        trace = std::move(candidate);
        stats.flipping_successes++;
      }
    }
  }

  void sparsify_by_simulation(Trace &trace) {
    std::vector<bool> seen_x(model->num_latches);
    for (unsigned t = 0; t <= k; ++t) {
      for (unsigned i = 0; i < model->num_latches; ++i) {
        const unsigned l = model->latches[i].lit;
        ternary &value = trace[t][IDX(l)];
        if (!value) {
          seen_x[i] = true;
          stats.already_x++;
          continue;
        }
        if (seen_x[i]) continue;
        stats.simulation_attempts++;
        Trace candidate = trace;
        candidate[t][IDX(l)] = X;
        simulate_suffix(candidate, t);
        if (bad_value(candidate[k]) != X1) {
          stats.simulation_reverts++;
          continue;
        }
        trace = std::move(candidate);
        seen_x[i] = true;
        stats.simulation_successes++;
      }
    }
  }

  unsigned add_trace_cubes(const Trace &trace) {
    unsigned added = 0;
    for (unsigned t = 0; t <= k; ++t) {
      Cube cube = state_cube(trace[t]);
      if (cube_exists_or_is_subsumed(cube)) continue;

      for (unsigned d = 0; d < k; ++d)
        forbid_cube(d, cube);
      add_last_indicator(cube, trace_suffix(trace, t));
      added++;
      L3 << "backward learned cube at trace state" << t << cube;
    }
    return added;
  }

  bool has_learned_cubes() const { return !learned.empty(); }

  void install_strengthened_property() {
    std::vector<unsigned> bads;
    bads.reserve(learned.size() + 1);
    bads.push_back(original_bad);
    for (const auto &learned_cube : learned) {
      if (learned_cube.cube.empty()) {
        set_property(model, 1, "backward");
        return;
      }
      Cube cube = learned_cube.cube;
      bads.push_back(conj(model, cube));
    }
    set_property(model, disj(model, bads), "backward");
  }

  void print_stats() const {
    L1 << "backward iterations" << iterations;
    L1 << "backward learned cubes" << learned.size();
    L1 << "backward flipping" << stats.flipping_successes << "/"
       << stats.flipping_attempts;
    L1 << "backward simulation" << stats.simulation_successes << "/"
       << stats.simulation_attempts << "reverted"
       << stats.simulation_reverts << "already-X" << stats.already_x;
  }
};

} // namespace

bool backward(aiger *model, std::vector<std::vector<unsigned>> &cex,
              aiger *&witness, unsigned k, bool use_flipping,
              bool use_simulation) {
  L1 << "backward with depth" << k;

  BackwardEncoding backward(model, k);
  for (;;) {
    if (backward.reset_reaches_bad(cex)) {
      L1 << "backward found reset-to-bad trace within depth" << k;
      backward.print_stats();
      return true;
    }

    if (backward.reset_is_bad(cex)) {
      L1 << "backward found reset state in bad set";
      backward.print_stats();
      return true;
    }

    const int res = backward.solve();
    if (res == 20) {
      L1 << "backward proved safety";
      if (backward.has_learned_cubes())
        backward.install_strengthened_property();
      else
        witness = build_k_induction_witness(model, k);
      backward.print_stats();
      return false;
    }
    if (res != 10) die("backward solver returned unknown");

    if (const auto reset_at = backward.reset_position()) {
      L1 << "backward found reset state at trace position" << *reset_at;
      backward.build_cex(cex, *reset_at);
      backward.print_stats();
      return true;
    }

    Trace trace = backward.trace();
    if (use_flipping) backward.sparsify_by_flipping(trace);
    if (use_simulation) backward.sparsify_by_simulation(trace);
    const unsigned added = backward.add_trace_cubes(trace);
    if (!added) die("backward made no progress");
  }
}
