#include "complete_liveness.hpp"

#include "cadical.hpp"

#include <climits>
#include <initializer_list>
#include <map>
#include <stdexcept>

bool complete_liveness_trace(aiger *model,
                             std::vector<std::vector<unsigned>> &cex,
                             unsigned &justice_index) {
  if (!model || !model->num_justice || cex.size() < 2) return false;
  const size_t frames = cex.size() - 1;
  const size_t width = static_cast<size_t>(model->maxvar) + 1;
  if (frames >= static_cast<size_t>(INT_MAX) ||
      width > static_cast<size_t>(INT_MAX) / (frames + 1))
    return false;

  // Reject incorrectly projected cubes instead of silently discarding them.
  for (unsigned lit : cex[0])
    if ((lit >> 1) > model->maxvar || !aiger_is_latch(model, lit & ~1u))
      return false;
  for (size_t t = 0; t < frames; ++t)
    for (unsigned lit : cex[t + 1])
      if ((lit >> 1) > model->maxvar || !aiger_is_input(model, lit & ~1u))
        return false;

  CaDiCaL::Solver solver;
  solver.declare_more_variables(static_cast<int>((frames + 1) * width));
  int variables = static_cast<int>((frames + 1) * width);
  auto fresh = [&]() {
    if (variables == INT_MAX)
      throw std::overflow_error(
          "liveness trace completion has too many variables");
    ++variables;
    solver.declare_more_variables(1);
    return variables;
  };
  auto literal = [width](size_t t, unsigned lit) {
    int v = static_cast<int>(t * width + (lit >> 1) + 1);
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
  auto land = [&](int a, int b) {
    int result = fresh();
    clause({-result, a});
    clause({-result, b});
    clause({result, -a, -b});
    return result;
  };
  auto lor = [&](int a, int b) {
    int result = fresh();
    clause({result, -a});
    clause({result, -b});
    clause({-result, a, b});
    return result;
  };

  for (size_t t = 0; t < frames; ++t) {
    clause({literal(t, 1)});
    for (unsigned i = 0; i < model->num_ands; ++i) {
      const auto &a = model->ands[i];
      clause({-literal(t, a.lhs), literal(t, a.rhs0)});
      clause({-literal(t, a.lhs), literal(t, a.rhs1)});
      clause({literal(t, a.lhs), -literal(t, a.rhs0), -literal(t, a.rhs1)});
    }
    for (unsigned i = 0; i < model->num_latches; ++i) {
      const auto &l = model->latches[i];
      equivalent(literal(t + 1, l.lit), literal(t, l.next));
      if (!t) equivalent(literal(0, l.lit), literal(0, l.reset));
    }
    for (unsigned i = 0; i < model->num_constraints; ++i)
      clause({literal(t, model->constraints[i].lit)});
    for (unsigned lit : cex[t + 1])
      clause({literal(t, lit)});
  }
  for (unsigned lit : cex[0])
    clause({literal(0, lit)});

  // Select exactly one earlier state. Prefix ORs indicate whether each input
  // frame belongs to the loop, and implement at-most-one in linear space.
  std::vector<int> in_loop;
  in_loop.reserve(frames);
  int previous = literal(0, 0); // constant false
  for (size_t t = 0; t < frames; ++t) {
    int selected = fresh();
    clause({-previous, -selected});
    int member = lor(previous, selected);
    in_loop.push_back(member);
    previous = member;
    for (unsigned i = 0; i < model->num_latches; ++i) {
      unsigned l = model->latches[i].lit;
      clause({-selected, -literal(frames, l), literal(t, l)});
      clause({-selected, literal(frames, l), -literal(t, l)});
    }
  }
  clause({in_loop.back()});

  // The terminal state shares the selected loop state's inputs on the next
  // repetition. Thus its constraints are already checked at the selected
  // frame; there is no unconstrained terminal input to check separately.
  std::map<unsigned, int> occurred;
  auto seen = [&](unsigned lit) {
    auto found = occurred.find(lit);
    if (found != occurred.end()) return found->second;
    int result = literal(0, 0);
    for (size_t t = 0; t < frames; ++t)
      result = lor(result, land(in_loop[t], literal(t, lit)));
    occurred.emplace(lit, result);
    return result;
  };
  for (unsigned i = 0; i < model->num_fairness; ++i)
    clause({seen(model->fairness[i].lit)});
  std::vector<int> selected_justice;
  selected_justice.reserve(model->num_justice);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    int selected = fresh();
    selected_justice.push_back(selected);
    for (unsigned i = 0; i < model->justice[j].size; ++i)
      clause({-selected, seen(model->justice[j].lits[i])});
  }
  for (int selected : selected_justice)
    solver.add(selected);
  solver.add(0);
  if (solver.solve() != 10) return false;

  unsigned property = 0;
  while (property < selected_justice.size() &&
         solver.val(selected_justice[property]) < 0)
    ++property;
  if (property == selected_justice.size()) return false;

  std::vector<std::vector<unsigned>> completed(cex.size());
  for (unsigned i = 0; i < model->num_latches; ++i) {
    unsigned l = model->latches[i].lit;
    completed[0].push_back(l | (solver.val(literal(0, l)) < 0));
  }
  for (size_t t = 0; t < frames; ++t)
    for (unsigned i = 0; i < model->num_inputs; ++i) {
      unsigned input = model->inputs[i].lit;
      completed[t + 1].push_back(input | (solver.val(literal(t, input)) < 0));
    }
  cex.swap(completed);
  justice_index = property;
  return true;
}
