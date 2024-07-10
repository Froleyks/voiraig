#pragma once

#include "aiger.hpp"

#include "utils.hpp"

#include <cstdint>

// Some notes on AIGER:
// Paper: Biere, Heljanko & Wieringa - AIGER 1.9 and Beyond
// In this format for And-Inverter-Graphs
// truth-constants (always 0 and 1),
// inputs (model non-determinism),
// latches (store a value for one step),
// ands (compute a value from two [possibly negated] literals)
// make up the set of *variables* (positive half of the literals) and are
// enumerated (if the circuit is reencoded in this order) with 0, 2, 4, 6,...
// The negation of each literal is obtained by flipping the least significant
// bit (4->5, 7->6) see aiger.hpp.
// Additionaly there are outputs (we will only have 1, if it's true we found a
// bad state),
// We often have two representations of expanded state / (partial) cubes: one is
// the set of literals that are true and the other is a vector with size equal
// to the number of variables that allows random access to the value of a
// certain variable/literal.
// Something I stumbled over multiple times when switching between these
// representations: When the negated literal is true we have a sign bit of 1 and
// in the expanded representation whe have a truth value of 0.

// X is falsy and actual values are truthy
enum ternary : uint_fast8_t { X = 0b000, X1 = 0b100, X0 = 0b110 };
#define STX(L) static_cast<ternary>((((L) & 1u) << 1) | 0b100)
#define XTS(L) (((L) & 2u) >> 1)
inline ternary sign(ternary t, unsigned l) {
  static constexpr ternary NEG[] = {X, X, X, X, X1, X0, X0, X1};
  return NEG[t | (l & 1u)];
}
inline bool flipped(ternary c, ternary u) { return (c ^ u) && u; }

// this is supposed to be independent of the encoding
bool validSimulation(aiger_and ands[], const unsigned num_ands,
                     const std::vector<ternary> &s);

#ifdef LOG
// Define the printing of your ternary value. Especially if you have chars which
// otherwise print invisibly.
std::ostream &operator<<(std::ostream &out, const std::vector<ternary> &v);
#endif // QUIET

// Given a ternary state s in expanded representation vec size = num_variables,
// the value of all and-gates, given by *ands and num_ands, is computed and
// written to s. The vector s should be sized correctly before this is called.
void propagate(aiger_and *ands, const unsigned num_ands, std::vector<ternary> &s);
std::vector<unsigned> reduce(aiger *model, const std::vector<unsigned> &obligations,
           std::vector<ternary> &s);
