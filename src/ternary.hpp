#pragma once

#include "aiger.hpp"
#include "utils.hpp"
#include <cstdint>

#ifndef ENCODING
#define ENCODING 0
#endif
#ifndef ALGORITHM
#define ALGORITHM 0
#endif
#ifndef TERNARY_TYPE
#define TERNARY_TYPE uint_fast8_t
#endif

#if ENCODING == 0
#include "ternary0.hpp"
#elif ENCODING == 1
#include "ternary1.hpp"
#elif ENCODING == 2
#include "ternary2.hpp"
#elif ENCODING == 3
#include "ternary3.hpp"
#elif ENCODING == 4
#include "ternary4.hpp"
#elif ENCODING == 5
#include "ternary5.hpp"
#endif

#ifdef LOG
    inline std::ostream &
    operator<<(std::ostream &out, const std::vector<ternary> &v) {
  static constexpr char repr[] =
#if ENCODING == 0
      {'0', '1', 'X'};
#elif ENCODING == 1
      {'0', '_', 'X', '1'};
#elif ENCODING == 2
      {'_', 'X', '0', '1'};
#elif ENCODING == 3
      {'0', '1', 'X', '_'};
#elif ENCODING == 4
      {'X', '_', '_', '_', '1', '_', '0'};
#elif ENCODING == 5
      {'0', '_', 'X', '_', '_', '_', '1'};
#endif
  out << "(";
  for (auto x : v)
    out << repr[x] << " ";
  if (v.size()) out << "\b";
  out << ")";
  return out;
}
#endif // LOG

static_assert(ISA(X0));
static_assert(ISA(X1));
static_assert(!ISA(X));

// this is supposed to be independent of the encoding
bool validSimulation(aiger_and ands[], const unsigned num_ands,
                     const std::vector<ternary> &s);

std::vector<unsigned> reduce(aiger *model,
                             const std::vector<unsigned> &obligations,
                             std::vector<ternary> &s);
