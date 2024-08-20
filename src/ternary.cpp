#include "ternary.hpp"

#include <algorithm>
#include <iostream>

bool validSimulation(aiger_and ands[], const unsigned num_ands,
                     const std::vector<ternary> &s) {
  for (unsigned int i = 0; i < num_ands; i++) {
    ternary lv = s[ands[i].rhs0 >> 1];
    ternary rv = s[ands[i].rhs1 >> 1];
    bool ls = ands[i].rhs0 & 1u;
    bool rs = ands[i].rhs1 & 1u;
    if (ls) {
      if (lv == X0)
        lv = X1;
      else if (lv == X1)
        lv = X0;
    }
    if (rs) {
      if (rv == X0)
        rv = X1;
      else if (rv == X1)
        rv = X0;
    }
    ternary res;
    if (lv == X0 || rv == X0)
      res = X0;
    else if (lv == X || rv == X)
      res = X;
    else
      res = X1;
    if (s[IDX(ands[i].lhs)] != res) return false;
  }
  return true;
}
#if ENCODING != 3
std::vector<unsigned> reduce(aiger *model,
                             const std::vector<unsigned> &obligations,
                             std::vector<ternary> &s) {
  const unsigned lBegin = model->num_inputs + 1; // const 1
  const unsigned lEnd = lBegin + model->num_latches;
  for (unsigned i = lBegin; i < lEnd; ++i) {
    // TODO skip those that are in obligations?
    const ternary v = s[i];
    assert(ISA(v));
    s[i] = X;
    L3 << "try to eliminate latch" << (i << 1);
    propagate(model->ands, model->num_ands, s);
    const bool covered =
        std::none_of(obligations.begin(), obligations.end(),
                     [&s](const unsigned l) { return !ISA(s[IDX(l)]); });
    LI3(covered) << "eliminated" << (i << 1);
    if (!covered) s[i] = v;
  }

  std::vector<unsigned> cube;
  cube.reserve(model->num_latches);
  for (unsigned i = lBegin; i < lEnd; ++i) {
    if (!ISA(s[i])) continue;
    cube.push_back((i << 1) | XTS(s[i]));
  }
  return cube;
}
#endif
