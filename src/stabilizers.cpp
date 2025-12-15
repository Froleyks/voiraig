#include "stabilizers.hpp"

#include "cadical.hpp"
#include "utils.hpp"

#include <array>
#include <vector>

std::vector<unsigned> stabilizers(aiger *model) {

  L3 << "stabilizer extraction";
  assert(model);
  const unsigned n = size(model);
  std::vector<unsigned> stable;
  stable.reserve(n - 2);
  std::array<std::vector<int>, 2> u;
  int var = 1;
  CaDiCaL::Solver s;

  for (auto &f : u)
    f.resize(n);
  for (int i = 0; i < 2; ++i) {
    auto &f = u[i];
    f[0] = 1;
    f[1] = -1;
    for (unsigned l = 2; l < n; l += 2) {
      if (f[l]) continue;
      const int v = var++;
      f[l] = v;
      f[l + 1] = -v;
      L5 << "setting f for latch" << l << "to" << v;
    }
    if (!i)
      for (auto [l, n] : latches(model) | nexts) {
        std::cout << "latch " << l << " next " << n << " f[n] " << f[n]
                  << std::endl;
        u[1][l] = f[n];
        u[1][l ^ 1] = f[n ^ 1];
      }
    for (auto [a, x, y] : ands(model)) {
      L5 << "and" << a << x << y;
      L5 << "f[x]" << f[x] << "f[y]" << f[y] << "f[a]" << f[a];
      s.clause(-f[x], -f[y], f[a]);
      s.clause(-f[a], f[x]);
      s.clause(-f[a], f[y]);
    }
    for (unsigned c : constraints(model) | lits)
      s.clause(f[c]);
  }

  std::vector<unsigned> candidates;
  candidates.reserve(model->num_latches);
  for (int i = model->num_latches - 1; i >= 0; --i)
    candidates.push_back(model->latches[i].lit);
  assert(candidates.size() == model->num_latches);
  // candidates.reserve(model->maxvar);
  // for (size_t i = 1; i <= model->maxvar; ++i)
  // candidates.push_back(2 * i);
  // for (size_t i = model->maxvar; i > 1; --i)
  // candidates.push_back(2 * i);
  // assert(candidates.size() == model->maxvar);
  bool progress{true};
  for (int round = 0; candidates.size() && progress; ++round) {
    L4 << "beginning round" << round << "with" << candidates.size()
       << "candidates";
    progress = false;
    size_t w{};
    for (int i = 0; i < candidates.size(); ++i) {
      bool stabilized{};
      for (unsigned sign = 0; sign < 2; ++sign) {
        unsigned c = candidates[i] ^ sign;
        L5 << "checking candidate" << c;
        s.assume(u[0][c]);
        s.assume(-u[1][c]);
        if (s.solve() != 20) continue;
        L5 << "found stabilizer" << c;
        progress = true;
        stabilized = true;
        stable.push_back(c);
        // Add both directions!
        s.clause(-u[0][c], u[1][c]);
        s.clause(-u[1][c], u[0][c]);
        break;
      }
      if (!stabilized) {
        candidates[w++] = candidates[i];
        continue;
      }
    }
    candidates.resize(w);
  }

  return stable;
}
