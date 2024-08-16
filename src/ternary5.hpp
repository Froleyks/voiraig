enum ternary : TERNARY_TYPE { X0 = 0b000, X = 0b010, X1 = 0b110 };
static constexpr ternary NEG[] = {X0, X1, X, X, X, X, X1, X0};
#define ISA(L) ((L) != X)
#define STX(L) (((L) & 1u) ? X0 : X1)
// Ternary value to sign bit that can be or'ed to a variable to get a literal.
#define XTS(L) (assert(!ISX(L)), ((~(L)) & 2u) >> 1)
inline ternary sign(ternary t, unsigned l) { return NEG[t | (l & 1u)]; }
inline bool flipped(ternary c, ternary u) { return (c ^ u) && ISA(u); }

inline void propagate(aiger_and *ands, const unsigned num_ands,
                      std::vector<ternary> &s) {
#if ALGORITHM == 0 // negationTable
  for (unsigned int i = 0; i < num_ands; i++) {
    const unsigned L{ands[i].rhs0}, R{ands[i].rhs1};
    TERNARY_TYPE l = s[L >> 1];
    TERNARY_TYPE r = s[R >> 1];
    l |= L & 1u;
    r |= R & 1u;
    l = NEG[l];
    r = NEG[r];
    s[ands[i].lhs >> 1] = static_cast<ternary>(l & r);
  }
#elif ALGORITHM == 1 // combinedTable
  static constexpr ternary AND_NEG[] = {
      // 0   1   X   X           1   0
      X0, X0, X0, X0, X0, X0, X0, X0, // <= 0
      X0, X1, X,  X,  X,  X,  X1, X0, // <= 1
      X0, X,  X,  X,  X,  X,  X,  X0, // <= X
      X0, X,  X,  X,  X,  X,  X,  X0, // <= X
      X0, X,  X,  X,  X,  X,  X,  X0, //
      X0, X,  X,  X,  X,  X,  X,  X0, //
      X0, X1, X,  X,  X,  X,  X1, X0, // <= 1
      X0, X0, X0, X0, X0, X0, X0, X0  // <= 0
  };
  for (unsigned int i = 0; i < num_ands; i++) {
    const unsigned L{ands[i].rhs0}, R{ands[i].rhs1};
    unsigned l = s[L >> 1];
    unsigned r = s[R >> 1];
    l |= L & 1u;
    r |= R & 1u;
    s[ands[i].lhs >> 1] = AND_NEG[r + 8 * l];
  }
#else
#error Invalid ENCODING / ALGORITHM combination
#endif
}
