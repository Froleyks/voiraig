enum ternary : TERNARY_TYPE { X = 1, X0 = 2, X1 = 3 };
#define ISA(L) ((L) >> 1)
#define STX(L) (static_cast<ternary>((~(L) & 1u) | 2u))
#define XTS(L) (assert(ISA(L)), (~(L)) & 1u)
inline ternary sign(ternary t, unsigned l) {
  return static_cast<ternary>(t ^ ((t >> 1) & l));
}
inline bool flipped(ternary c, ternary u) {
  unsigned char f = c ^ u;
  f |= ~c >> 1;
  f &= u >> 1;
  // f &= (~f >> 1) & 1;
  // f = -f;
  LV5((int)c, (int)u, (int)f);
  return f;
}

inline void propagate(aiger_and *ands, const unsigned num_ands,
                      std::vector<ternary> &s) {
#if ALGORITHM == 0 // ternaryOp10
  for (unsigned int i = 0; i < num_ands; i++) {
    TERNARY_TYPE l = s[ands[i].rhs0 >> 1];
    TERNARY_TYPE r = s[ands[i].rhs1 >> 1];
    l ^= (l >> 1) & ands[i].rhs0;
    r ^= (r >> 1) & ands[i].rhs1;
    l &= r;
    l = l ? l : 2;
    s[ands[i].lhs >> 1] = static_cast<ternary>(l);
  }
#elif ALGORITHM == 1 // andFix13
  for (unsigned int i = 0; i < num_ands; i++) {
    TERNARY_TYPE l = s[ands[i].rhs0 >> 1];
    TERNARY_TYPE r = s[ands[i].rhs1 >> 1];
    l ^= (l >> 1) & ands[i].rhs0;
    r ^= (r >> 1) & ands[i].rhs1;
    l &= r;
    l |= (~l & 1u) << 1;
    s[ands[i].lhs >> 1] = static_cast<ternary>(l);
  }
#else
#error Invalid ENCODING / ALGORITHM combination
#endif
}
