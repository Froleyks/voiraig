enum ternary : TERNARY_TYPE { X = 0b000, X1 = 0b100, X0 = 0b110 };
#define ISA(L) (L)
#define STX(L) static_cast<ternary>((((L) & 1u) << 1) | 0b100)
#define XTS(L) ((((L)) & 2u) >> 1)
inline ternary sign(ternary t, unsigned l) {
  static constexpr ternary NEG[] = {X, X, X, X, X1, X0, X0, X1};
  return NEG[t | (l & 1u)];
}
inline bool flipped(ternary c, ternary u) { return (c ^ u) && ISA(u); }

inline void propagate(aiger_and *ands, const unsigned num_ands,
                      std::vector<ternary> &s) {
#if ALGORITHM == 0 // preShiftLookup
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
#else
#error Invalid ENCODING / ALGORITHM combination
#endif
}
