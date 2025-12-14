#include "k_liveness.hpp"

#include "aiger.h"
#include "aiger.hpp"
#include "ic3.hpp"
#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <vector>

// Build a safety instance where the fairness literal may be violated at most
// 'k' times. The (k+1)th violation triggers bad.
std::pair<aiger *, std::vector<unsigned>> build_safety_instance(aiger *model,
                                                                unsigned k) {
  L3 << "building safety instance for k =" << k;
  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto m = [&map](unsigned from, unsigned to) {
    assert(map[from] == INVALID_LIT);
    assert(from != INVALID_LIT && to != INVALID_LIT);
    map[from] = to;
    map[aiger_not(from)] = aiger_not(to);
  };
  m(0, 0);
  auto *safety = aiger_init();
  for (auto &i : inputs(model))
    m(i.lit, input(safety));
  for (auto &l : latches(model))
    m(l.lit, latch(safety));

  // add new extra lives
  std::vector<unsigned> lives;
  lives.reserve(k);
  for (int i = 0; i < k; ++i)
    lives.push_back(latch(safety));
  L4 << "added extra lives" << lives;

  for (auto [a, x, y] : ands(model)) {
    assert(map[a] == INVALID_LIT);
    assert(map[x] != INVALID_LIT);
    assert(map[y] != INVALID_LIT);
    m(a, conj(safety, map[x], map[y]));
  }
  // add back original latch transition and reset
  for (auto &l : latches(model)) {
    aiger_symbol *sl = aiger_is_latch(safety, map[l.lit]);
    assert(sl);
    assert(map[l.reset] != INVALID_LIT);
    assert(map[l.next] != INVALID_LIT);
    sl->reset = map[l.reset];
    sl->next = map[l.next];
  }
  for (auto &c : constraints(model)) {
    assert(map[c.lit] != INVALID_LIT);
    aiger_add_constraint(safety, map[c.lit], c.name);
  }

  const unsigned Q = aiger_not(map[model->justice[0].lits[0]]);
  assert(Q != INVALID_LIT);

  for (int i = 0; i < k; ++i) {
    aiger_symbol *l = aiger_is_latch(safety, lives[i]);
    assert(l);
    assert(l->lit == lives[i]);
    l->reset = 1;
    l->next = conj(safety, lives[i], disj(safety, Q, i ? lives[i - 1] : 0));
  }

  unsigned P = k ? disj(safety, lives.back(), Q) : Q;
  L4 << "Reduced to safety property" << P;
  aiger_add_bad(safety, aiger_not(P), "k-buffered");
  return {safety, lives};
}

bool build_cex(aiger *model, std::vector<std::vector<unsigned>> &safety_cex) {
  return false;
}

void build_witness(aiger *&witness, aiger *kWit, aiger *model, unsigned k,
                   const std::vector<unsigned> &lives) {
  witness = kWit;
  unsigned decreased = 0;
  for (auto i : lives) {
    aiger_symbol *l = aiger_is_latch(witness, i);
    assert(l);
    decreased = disj(witness, decreased, conj(witness, l->lit, aiger_not(l->next)));
  }
    L1 << "liveness decrease literal" << decreased;
  unsigned justice_lits[] = {decreased};
  aiger_add_justice(witness, 1, justice_lits, nullptr);
}

// namespace

bool k_liveness(aiger *model, aiger *&witness,
                std::vector<std::vector<unsigned>> &cex) {
  L1 << "k-liveness";
  assert(model);
  assert(model->num_justice == 1);
  assert(model->justice[0].size == 1);
  for (unsigned k = 0;; ++k) {
    L2 << "k-liveness trial k =" << k;
    auto [safety, lives] = build_safety_instance(model, k);

    std::vector<std::vector<unsigned>> safety_cex;
    const bool bug = ic3(safety, safety_cex);
    if (bug) {
      L3 << "sat for k =" << k;
      aiger_reset(safety);

      if (build_cex(model, safety_cex)) {
        L3 << "liveness cex built for k =" << k;
        cex.swap(safety_cex);
        return true;
      }
    } else {
      L3 << "unsat for k =" << k;
      build_witness(witness, safety, model, k, lives);
      return false;
    }
  }
  // Unreachable, but placate compilers.
  assert(false);
  witness = nullptr;
  return true;
}
