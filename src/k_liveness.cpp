#include "k_liveness.hpp"

#include "ic3.hpp"
#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
// Keep the old literal order and structure, drop fairness.
aiger *clone_base(aiger *model) {
  auto *safe = aiger_init();
  for (auto &i : inputs(model)) aiger_add_input(safe, i.lit, i.name);
  for (auto &l : latches(model)) {
    aiger_add_latch(safe, l.lit, l.next, l.name);
    aiger_add_reset(safe, l.lit, l.reset);
  }
  for (auto &c : constraints(model)) aiger_add_constraint(safe, c.lit, c.name);
  for (auto &a : ands(model))
    aiger_add_and(safe, a.lhs, a.rhs0, a.rhs1);
  return safe;
}

unsigned parse_k(const aiger_symbol &fair) {
  const char *env = std::getenv("VOIRAIG_K_LIVENESS");
  if (!env) env = std::getenv("VOIRAIG_K");
  unsigned k{};
  if (env) {
    std::from_chars(env, env + std::strlen(env), k);
    if (k) return k;
  }
  if (fair.name) {
    const char *p = std::strchr(fair.name, '=');
    if (p) std::from_chars(p + 1, fair.name + std::strlen(fair.name), k);
    if (k) return k;
  }
  return 10; // conservative default
}

void trim_cex(std::vector<std::vector<unsigned>> &cex, unsigned max_lit) {
  for (auto &cube : cex) {
    cube.erase(std::remove_if(cube.begin(), cube.end(),
                              [max_lit](unsigned l) { return ABS(l) > max_lit; }),
               cube.end());
  }
}
} // namespace

bool k_liveness(aiger *model, aiger *&witness,
                std::vector<std::vector<unsigned>> &cex) {
  assert(model);
  assert(model->num_fairness == 1);
  const aiger_symbol &fair = model->fairness[0];
  const unsigned k = std::max(1u, parse_k(fair));
  LI2(k) << "k-liveness with k=" << k;

  aiger *safe = clone_base(model);
  const unsigned original_max = 2 * model->maxvar + 1;

  std::vector<unsigned> flps(k);
  unsigned x = fair.lit;
  for (size_t i = 0; i < flps.size(); ++i) {
    flps[i] = size(safe);
    aiger_add_latch(safe, flps[i], flps[i], "k-live");
    aiger_add_reset(safe, flps[i], 0);
    const unsigned out = disj(safe, x, flps[i]);
    aiger_is_latch(safe, flps[i])->next = out;
    x = conj(safe, x, flps[i]);
  }

  const unsigned bad = NOT(x);
  aiger_add_bad(safe, bad, "k-liveness");

  const bool bug = ic3(safe, cex);
  trim_cex(cex, original_max);
  if (bug) {
    aiger_reset(safe);
    witness = nullptr;
  } else {
    witness = safe;
  }
  return bug;
}
