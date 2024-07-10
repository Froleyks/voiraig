#include "ternary.hpp"

#include <algorithm>
#include <iostream>

#ifdef LOG
std::ostream &operator<<(std::ostream &out, const std::vector<ternary> &v) {
  static constexpr char repr[] = {'X', '_', '_', '_', '1', '_', '0'};
  out << "(";
  for (auto x : v)
    out << repr[x] << " ";
  if (v.size()) out << "\b";
  out << ")";
  return out;
}
#endif // QUIET

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

void propagate(aiger_and *ands, const unsigned num_ands,
               std::vector<ternary> &s) {
  // X  -X    X  -X   1  -1   0  -0
  static constexpr ternary AND_NEG[] = {
      X,  X,  X,  X,  X,  X0, X0, X,  //  X
      X,  X,  X,  X,  X,  X0, X0, X,  // -X
      X,  X,  X,  X,  X,  X0, X0, X,  //  X
      X,  X,  X,  X,  X,  X0, X0, X,  // -X
      X,  X,  X,  X,  X1, X0, X0, X1, //  1
      X0, X0, X0, X0, X0, X0, X0, X0, // -1
      X0, X0, X0, X0, X0, X0, X0, X0, //  0
      X,  X,  X,  X,  X1, X0, X0, X1, // -0
  };
  for (unsigned int i = 0; i < num_ands; i++) {
    const unsigned L{ands[i].rhs0}, R{ands[i].rhs1};
    const unsigned l = s[L >> 1] | (L & 1u);
    const unsigned r = s[R >> 1] | (R & 1u);
    s[ands[i].lhs >> 1] = AND_NEG[r + 8 * l];
  }
}
std::vector<unsigned> reduce(aiger *model,
                             const std::vector<unsigned> &obligations,
                             std::vector<ternary> &s) {
  const unsigned lBegin = model->num_inputs + 1; // const 1
  const unsigned lEnd = lBegin + model->num_latches;
  for (unsigned i = lBegin; i < lEnd; ++i) {
    // TODO skip those that are in obligations?
    const ternary v = s[i];
    assert(v);
    s[i] = X;
    L3 << "try to eliminate latch" << (i << 1);
    propagate(model->ands, model->num_ands, s);
    const bool covered =
        std::none_of(obligations.begin(), obligations.end(),
                     [&s](const unsigned l) { return !(s[IDX(l)]); });
    LI3(covered) << "eliminated" << (i << 1);
    if (!covered) s[i] = v;
  }

  std::vector<unsigned> cube;
  cube.reserve(model->num_latches);
  for (unsigned i = lBegin; i < lEnd; ++i) {
    if (!(s[i])) continue;
    cube.push_back((i << 1) | XTS(s[i]));
  }
  return cube;
}
