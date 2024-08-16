#pragma once

#include "aiger.hpp"

#include "utils.hpp"

#include <cstdint>
#include <iostream>

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


// The encoding of ternary values.
enum ternary : uint_fast8_t {
    X0=0,
    X1=3,
    X=2
};
// These macros depend on the encoding and are used globally so please define
// them.

#define ISX(L) ((L)==X)
#define ISA(L) (!ISX(L))
// Literal to its ternary value, only the sign bit is considered. (00 if L odd, 11 if L even)
#define STX(L) (static_cast<ternary>(((~(L)&1u) << 1) xor (~(L)&1u)))
// Ternary value to sign bit that can be or'ed to a variable to get a literal.
#define XTS(L) (assert(!ISX(L)), (~(L)) & 1u)

// Computes the complement of t conditionally.
// If stx = 11, leave the value as it is
// If stx = 00, then do a complement
ternary conditional_complement(ternary t, ternary stx);

// Make ternary usable like logical operators
// Maybe ~ and ^ shouldn't be used because some code may use ternary as int without casting.
ternary operator~(ternary t1);
ternary operator&&(ternary t1, ternary t2);
ternary operator||(ternary t1, ternary t2);
ternary operator^(ternary t1, ternary t2);

std::ostream& operator<<(std::ostream& os, ternary t);

#ifdef LOGGING
// Define the printing of your ternary value. Especially if you have chars which
// otherwise print invisibly.
std::ostream &operator<<(std::ostream &out, const std::vector<ternary> &v);
#endif // QUIET

// Given a ternary state s in expanded representation vec size = num_variables,
// the value of all and-gates, given by *ands and num_ands, is computed and
// written to s. The vector s should be sized correctly before this is called.
void propagate(aiger_and *ands, const unsigned num_ands,
              std::vector<ternary> &s);

// Given a ternary state s that implies the obligations (after simulate),
// return a minimal subset of the latche literals (!) in s that still
// imply the obligations. Note: when obligations represents a next state, they
// has to be expressed as the next-function of each latch.
std::vector<unsigned> reduce(aiger *model,
                             const std::vector<unsigned> &obligations,
                             std::vector<ternary> &s);
