// Implementation of the rlive algorithm
// Xia et al. - 2024 - Avoiding the Shoals - A New Approach to Liveness
// Checking.pdf
#include "rlive.hpp"

#include "aiger.h"
#include "ic3.hpp"
#include "utils.hpp"

#include <cstdint>

static std::vector<bool>
last_state(aiger *model, const std::vector<std::vector<unsigned>> &cex) {
  assert(model);
  assert(!cex.empty());

  std::vector<uint8_t> values(model->maxvar + 1, 0);
  values[0] = 0;

  auto lit_value = [&values](unsigned lit) -> bool {
    const bool v = values[IDX(lit)];
    return aiger_sign(lit) ? !v : v;
  };

  // Initialize latches from reset values when available.
  for (auto &l : latches(model)) {
    if (l.reset == 1)
      values[IDX(l.lit)] = 1;
    else if (l.reset == 0 || l.reset == l.lit)
      values[IDX(l.lit)] = 0;
    else
      values[IDX(l.lit)] = static_cast<uint8_t>(lit_value(l.reset));
  }

  // Override with the initial latch assignment from the cex.
  for (unsigned lit : cex[0])
    values[IDX(lit)] = STV(lit);

  const unsigned bad = output(model);

  auto propagate_ands = [&]() {
    for (auto &a : ands(model)) {
      const bool v = lit_value(a.rhs0) && lit_value(a.rhs1);
      values[IDX(a.lhs)] = static_cast<uint8_t>(v);
    }
  };

  auto capture_state = [&]() -> std::vector<bool> {
    std::vector<bool> state;
    state.reserve(model->num_latches);
    for (auto l : latches(model) | lits)
      state.push_back(values[IDX(l)]);
    return state;
  };

  for (size_t step = 1; step < cex.size(); ++step) {
    for (auto l : inputs(model) | lits)
      values[IDX(l)] = 0;
    for (unsigned lit : cex[step])
      values[IDX(lit)] = STV(lit);

    propagate_ands();
    if (lit_value(bad))
      return capture_state();

    std::vector<uint8_t> next_vals;
    next_vals.reserve(model->num_latches);
    for (auto &l : latches(model))
      next_vals.push_back(static_cast<uint8_t>(lit_value(l.next)));
    for (size_t i = 0; i < model->num_latches; ++i)
      values[IDX(model->latches[i].lit)] = next_vals[i];
  }

  if (cex.size() == 1) {
    propagate_ands();
    if (lit_value(bad))
      return capture_state();
  }

  assert(false && "cex does not reach a bad state");
  return {};
}

aiger *encode(aiger *model, const std::vector<unsigned> &S,
              const std::vector<unsigned> &Sn) {
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
  for (auto l : latches(model) | lits)
    m(l, latch(safety));
  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  for (auto l : latches(model)) {
    assert(map[l.lit] != INVALID_LIT);
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    aiger_symbol *s = aiger_is_latch(safety, map[l.lit]);
    assert(s);
    s->reset = map[l.reset];
    s->next = map[l.next];
  }
  for (auto l : constraints(model)) {
    assert(map[l.lit] != INVALID_LIT);
    aiger_add_constraint(safety, map[l.lit], l.name);
  }

  // Observation: Given that we have the list of shoals encoded over current and
  // next state literals, we can simply add the constraint that both are true to
  // ensure that only transitions in the deep (i.e., outside shoals) are
  // considered and that the bad is also within there. I am using a (possibly)
  // slightly modified understanding of shoal where the bad is added to the
  // shoal manually.
  unsigned deep = aiger_not(conj(safety, S));
  unsigned deep_n = aiger_not(conj(safety, Sn));
  aiger_add_constraint(safety, conj(safety, deep, deep_n), "deep");
  const unsigned J{map[model->justice[0].lits[0]]};
  assert(J != INVALID_LIT);
  aiger_add_output(safety, J, "bad");

  return safety;
}


bool rlive(aiger *model, aiger *&witness,
           std::vector<std::vector<unsigned>> &cex) {
  std::vector<unsigned> S, Sn;
  std::vector<std::vector<unsigned>> trace;
  aiger *safety = encode(model, S, Sn);
  bool bug = ic3(safety, cex);
  if (bug) {
    L3 << "Liveness violation found";
    std::vector<bool> s = last_state(model, cex);

    return true;
  }
  aiger_reset(safety);
  return false;
}
