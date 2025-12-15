#include "k_liveness.hpp"

#include "aiger.h"
#include "aiger.hpp"
#include "cadical.hpp"
#include "ic3.hpp"
#include "stabilizers.hpp"
#include "ternary.hpp"
#include "utils.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

// Build a safety instance where the fairness literal may be violated at most
// 'k' times. The (k+1)th violation triggers bad.
std::tuple<aiger *, std::vector<unsigned>, unsigned, std::vector<unsigned>>
build_safety_instance(aiger *model, unsigned k,
                      const std::vector<unsigned> &stable) {
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
  std::vector<unsigned> stabilized;
  stabilized.reserve(stable.size());
  unsigned Q{1};
  for (auto i : stable) {
    if (aiger_symbol *l = aiger_is_latch(model, i)) {
      L5 << "adding stabilizer for latch" << i;
      L5 << map;
      assert(l);
      unsigned c{l->lit ^ (i & 1u)};
      unsigned n{l->next ^ (i & 1u)};
      assert(map[c] != INVALID_LIT);
      assert(map[n] != INVALID_LIT);
      unsigned stabilizer = eq(safety, map[c], map[n]);
      Q = conj(safety, Q, stabilizer);
      stabilized.push_back(map[c]);
    }
  }
  Q = impl(safety, Q, aiger_not(map[model->justice[0].lits[0]]));

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
  aiger_open_and_write_to_file(safety, "k_liveness.aag");
  return {safety, lives, map[model->justice[0].lits[0]], stabilized};
}

bool build_cex(aiger *model, std::vector<std::vector<unsigned>> &safety_cex,
               unsigned Q, unsigned og_num_latches) {
  L4 << "building liveness cex from safety cex size" << safety_cex.size();
  for (auto x : safety_cex)
    L5 << x;
  assert(model);
  assert(safety_cex.size() >= 2);

  struct TernaryVecHash {
    size_t operator()(const std::vector<ternary> &v) const {
      size_t h = v.size();
      for (auto t : v)
        h = h * 1315423911u + static_cast<unsigned>(t);
      return h;
    }
  };
  std::unordered_set<std::vector<ternary>, TernaryVecHash> visited;

  // TODO don't use ternary here
  // const unsigned violation = model->justice[0].lits[0];
  std::vector<ternary> s(model->maxvar + 1, X);
  s[0] = X0; // constant false
  for (auto l : safety_cex[0]) {
    L5 << "setting input" << l << "at index" << IDX(l) << "to" << (int)STX(l);
    s[IDX(l)] = STX(l);
  }
  safety_cex[0].resize(og_num_latches); // remove extra lives
  for (size_t i = 1; i < safety_cex.size(); ++i) {
    for (auto i : safety_cex[i])
      s[IDX(i)] = STX(i);
    propagate(model->ands, model->num_ands, s);
    L5 << s;

    if (sign(s[IDX(Q)], Q) == X1) {
      L4 << "Liveness violation";
      std::vector<ternary> s_latch;
      s_latch.reserve(model->num_inputs + model->num_latches);
      for (auto l : inputs(model) | lits)
        s_latch.push_back(s[IDX(l)]);
      for (auto l : latches(model) | lits | std::views::take(og_num_latches))
        s_latch.push_back(s[IDX(l)]);
      L5 << "inserting" << s_latch;
      if (!visited.insert(s_latch).second) {
        L4 << "repeated violation";
        safety_cex.resize(i + 1);

        return true;
      }
    }

    std::vector<std::pair<unsigned, ternary>> updates;
    updates.reserve(model->num_latches);
    for (auto [l, n] : latches(model) | nexts)
      updates.emplace_back(IDX(l), sign(s[IDX(n)], n));
    for (auto [i, v] : updates)
      s[i] = v;
  }

  return false;
}

void build_witness(aiger *&witness, aiger *kWit, aiger *model, unsigned k,
                   const std::vector<unsigned> &lives,
                   const std::vector<unsigned> &stable) {
  L5 << "building witness for k =" << k;
  L5 << stable;
  witness = kWit;
  unsigned equally_stable{1}, less_stable{0};
  for (unsigned c : stable) {
    aiger_symbol *l = aiger_is_latch(witness, c);
    assert(l);
    l->next = conj(witness, l->next, l->next); // alias
    unsigned n{l->next ^ (c & 1u)};
    L5 << "comparator" <<  c << "<=" << n;
    less_stable =
        disj(witness, less_stable,
             conj(witness, equally_stable, conj(witness, aiger_not(c), n)));
    equally_stable = conj(witness, equally_stable, eq(witness, c, n));
  }
  unsigned less_live{};
  for (unsigned i : lives) {
    aiger_symbol *l = aiger_is_latch(witness, i);
    assert(l);
    unsigned c{l->lit ^ (i & 1u)};
    unsigned n{l->next ^ (i & 1u)};
    less_live = disj(witness, less_live, conj(witness, c, aiger_not(n)));
  }
  unsigned decreased =
      disj(witness, less_stable, conj(witness, equally_stable, less_live));
  L1 << "liveness decrease literal" << decreased;
  unsigned violations[] = {decreased};
  aiger_add_justice(witness, 1, violations, nullptr);
}

bool k_liveness(aiger *model, aiger *&witness,
                std::vector<std::vector<unsigned>> &cex) {
  L1 << "k-liveness";
  assert(model);
  assert(model->num_justice == 1);
  assert(model->justice[0].size == 1);

  const std::vector<unsigned> stable = stabilizers(model);

  for (unsigned k = 0;; ++k) {
    L2 << "k-liveness trial k =" << k;
    auto [safety, lives, Q, stabilized] =
        build_safety_instance(model, k, stable);

    std::vector<std::vector<unsigned>> safety_cex;
    const bool bug = ic3(safety, safety_cex);
    if (bug) {
      L3 << "sat for k =" << k;
      if (build_cex(safety, safety_cex, Q, model->num_latches)) {
        L3 << "liveness cex build for k =" << k;
        cex.swap(safety_cex);
        aiger_reset(safety);
        return true;
      }
      aiger_reset(safety);
    } else {
      L3 << "unsat for k =" << k;
      build_witness(witness, safety, model, k, lives, stabilized);
      return false;
    }
  }
  // Unreachable, but placate compilers.
  assert(false);
  witness = nullptr;
  return true;
}
