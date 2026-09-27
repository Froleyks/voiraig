#include "complete_liveness.hpp"
#include <climits>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

// Independent explicit-state oracle: enumerate all initial states and input
// sequences compatible with a sparse trace, then inspect every possible loop.
unsigned accept(aiger *a, const std::vector<std::vector<unsigned>> &cubes,
                int target = -1) {
  unsigned K = cubes.size() - 1, I = a->num_inputs, L = a->num_latches;
  auto val = [](const auto &v, unsigned x) { return v[x >> 1] ^ (x & 1); };
  for (unsigned code = 0; code < (1u << (L + I * K)); ++code) {
    std::vector<std::vector<unsigned>> states(K + 1, std::vector<unsigned>(L));
    std::vector<std::vector<unsigned>> vals(
        K, std::vector<unsigned>(a->maxvar + 1));
    for (unsigned l = 0; l < L; ++l)
      states[0][l] = (code >> l) & 1;
    bool valid = true;
    for (unsigned t = 0; t < K && valid; ++t) {
      auto &v = vals[t];
      for (unsigned i = 0; i < I; ++i)
        v[a->inputs[i].lit >> 1] = (code >> (L + t * I + i)) & 1;
      for (unsigned l = 0; l < L; ++l)
        v[a->latches[l].lit >> 1] = states[t][l];
      for (unsigned g = 0; g < a->num_ands; ++g) {
        auto &and_ = a->ands[g];
        v[and_.lhs >> 1] = val(v, and_.rhs0) & val(v, and_.rhs1);
      }
      if (!t) {
        for (auto l : cubes[0])
          valid &= bool(val(v, l));
        for (unsigned l = 0; l < L; ++l)
          valid &= v[a->latches[l].lit >> 1] == val(v, a->latches[l].reset);
      }
      for (auto l : cubes[t + 1])
        valid &= bool(val(v, l));
      for (unsigned c = 0; c < a->num_constraints; ++c)
        valid &= bool(val(v, a->constraints[c].lit));
      for (unsigned l = 0; l < L; ++l)
        states[t + 1][l] = val(v, a->latches[l].next);
    }
    if (!valid)
      continue;
    for (unsigned begin = 0; begin < K; ++begin) {
      if (states[begin] != states[K])
        continue;
      auto seen = [&](unsigned lit) {
        for (unsigned t = begin; t < K; ++t)
          if (val(vals[t], lit))
            return true;
        return false;
      };
      bool fair = true;
      for (unsigned f = 0; f < a->num_fairness; ++f)
        fair &= seen(a->fairness[f].lit);
      if (!fair)
        continue;
      for (unsigned j = 0; j < a->num_justice; ++j) {
        if (target >= 0 && j != unsigned(target))
          continue;
        bool ok = true;
        for (unsigned q = 0; q < a->justice[j].size; ++q)
          ok &= seen(a->justice[j].lits[q]);
        if (ok)
          return j + 1;
      }
    }
  }
  return 0;
}

unsigned current_trial = 0;
void check(bool condition) {
  if (!condition)
    throw std::runtime_error("trace completion verification failed at trial " +
                             std::to_string(current_trial));
}

void check_projection_guards() {
  aiger *a = aiger_init();
  aiger_add_input(a, 2, nullptr);
  aiger_add_latch(a, 4, 4, nullptr);
  aiger_add_reset(a, 4, 0);
  unsigned never = 0, always = 1;
  aiger_add_justice(a, 1, &never, nullptr);
  aiger_add_justice(a, 1, &always, nullptr);
  // Projection may encounter monitor literals beyond the source maxvar.
  // Reject them before aiger_is_input/latch, which expect in-range literals.
  const std::vector<std::vector<std::vector<unsigned>>> invalid = {
      {{6}, {}}, {{UINT_MAX}, {}}, {{}, {6}}, {{}, {UINT_MAX}},
      {{2}, {}}, {{}, {4}}, {{0}, {}}, {{}, {1}}, {{4}, {2, 3}}};
  for (auto cubes : invalid) {
    auto original = cubes;
    unsigned index = 999;
    check(!complete_liveness_trace(a, cubes, index));
    check(cubes == original && index == 999);
  }
  std::vector<std::vector<unsigned>> cubes(2);
  unsigned index = 999;
  check(complete_liveness_trace(a, cubes, index));
  check(index == 1); // Preserve the original property index, not normalized j0.
  check(cubes[0] == std::vector<unsigned>{5});
  check(cubes[1].size() == 1);
  check(accept(a, cubes, index));
  aiger_reset(a);
}

int main() {
  check_projection_guards();
  // Fixed seed and small bounds keep this exhaustive check reproducible.
  std::mt19937 rng(671234);
  for (unsigned trial = 0; trial < 800; ++trial) {
    current_trial = trial;
    aiger *a = aiger_init();
    unsigned I = rng() % 3, L = rng() % 4, G = rng() % 5, K = 1 + rng() % 3;
    unsigned max = I + L + G;
    auto lit = [&]() { return rng() % (2 * (max + 1)); };
    for (unsigned i = 0; i < I; ++i)
      aiger_add_input(a, 2 * (i + 1), nullptr);
    for (unsigned l = 0; l < L; ++l)
      aiger_add_latch(a, 2 * (I + l + 1), lit(), nullptr);
    for (unsigned g = 0; g < G; ++g) {
      unsigned v = I + L + g + 1;
      aiger_add_and(a, 2 * v, rng() % (2 * v), rng() % (2 * v));
    }
    for (unsigned l = 0; l < L; ++l)
      aiger_add_reset(a, a->latches[l].lit, lit());
    for (unsigned f = 0, n = rng() % 3; f < n; ++f)
      aiger_add_fairness(a, lit(), nullptr);
    for (unsigned c = 0, n = rng() % 3; c < n; ++c)
      aiger_add_constraint(a, lit(), nullptr);
    for (unsigned j = 0, n = 1 + rng() % 3; j < n; ++j) {
      std::vector<unsigned> q(rng() % 4);
      for (auto &x : q)
        x = lit();
      aiger_add_justice(a, q.size(), q.data(), nullptr);
    }
    check(!aiger_check(a));
    std::vector<std::vector<unsigned>> cex(K + 1);
    for (unsigned l = 0; l < L; ++l)
      if (rng() % 3 == 0)
        cex[0].push_back(a->latches[l].lit | (rng() % 2));
    for (unsigned t = 0; t < K; ++t)
      for (unsigned i = 0; i < I; ++i)
        if (rng() % 3 == 0)
          cex[t + 1].push_back(a->inputs[i].lit | (rng() % 2));
    auto original = cex;
    auto expected = accept(a, cex);
    unsigned index = 999;
    bool got = complete_liveness_trace(a, cex, index);
    if (got != bool(expected)) {
      std::cerr << "mismatch trial " << trial << " expected " << expected
                << " got " << got << "\n";
      return 1;
    }
    if (got) {
      check(index < a->num_justice);
      check(accept(a, cex, index));
      check(cex[0].size() == L);
      for (unsigned t = 0; t < K; ++t)
        check(cex[t + 1].size() == I);
      for (unsigned t = 0; t <= K; ++t)
        for (auto l : original[t]) {
          bool found = false;
          for (auto x : cex[t])
            found |= l == x;
          check(found);
        }
    } else {
      check(cex == original);
      check(index == 999);
    }
    aiger_reset(a);
  }
  std::cout << "Projection guards and 800 exhaustive small-model comparisons passed\n";
}
