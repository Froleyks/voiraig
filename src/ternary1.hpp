enum ternary : TERNARY_TYPE { X = 2, X0 = 0, X1 = 3 };
#define ISA(L) ((L) != X)
// Literal to its ternary value, only the sign bit is considered.
#define STX(L) (static_cast<ternary>(((~(L) & 1u) << 1) xor (~(L) & 1u)))
#define XTS(L) (assert(ISA(L)), (~(L)) & 1u)
inline ternary sign(ternary t, unsigned l) {
  TERNARY_TYPE a = static_cast<TERNARY_TYPE>(t);
  TERNARY_TYPE b = a << 1;
  TERNARY_TYPE c = (b ^ a);
  TERNARY_TYPE d = c & 0b10;
  TERNARY_TYPE e = (d ^ (d >> 1));
  return static_cast<ternary>((e | STX(l)) ^ a ^ 0b11);
}
inline bool flipped(ternary c, ternary u) { return (c ^ u) && ISA(u); }

inline void propagate(aiger_and *ands, const unsigned num_ands,
                      std::vector<ternary> &s) {
#if ALGORITHM == 0 // singleAnd
  for (unsigned int i = 0; i < num_ands; i++) {
    unsigned int lhs_index = (ands[i].lhs >> 1);
    unsigned int rhs0_index = (ands[i].rhs0 >> 1);
    unsigned int rhs1_index = (ands[i].rhs1 >> 1);
    // std::cerr << "Value 1: " << static_cast<int>(s[rhs0_index]) << " and
    // value 2: " << static_cast<int>(s[rhs1_index]) << "\n"; std::cerr << "Use:
    // " << ands[i].lhs << " " << ands[i].rhs0 << " " << ands[i].rhs1 << "\n";
    s[lhs_index] = static_cast<ternary>(sign(s[rhs0_index], ands[i].rhs0) &
                                        sign(s[rhs1_index], ands[i].rhs1));
    // std::cerr << "Result: " << static_cast<int>(s[lhs_index]) << "\n";
  }
#else
#error Invalid ENCODING / ALGORITHM combination
#endif
}
