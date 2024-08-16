enum ternary : TERNARY_TYPE { X0, X1, X };
#define ISA(L) ((L) != X)
#define STX(L) static_cast<ternary>((~(L)) & 1u)
// Ternary value to sign bit that can be or'ed to a variable to get a literal.
#define XTS(L) (assert(ISA(L)), (~(L) & 1u))
inline ternary sign(ternary t, unsigned l) {
  return (t == X) ? X : static_cast<ternary>(t ^ (l & 1u));
}
inline bool flipped(ternary c, ternary u) { return (c ^ u) && ISA(u); }

inline void propagate(aiger_and *ands, const unsigned num_ands,
                      std::vector<ternary> &s) {
#if ALGORITHM == 0 // baseline
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
    s[ands[i].lhs >> 1] = res;
    // L5 << "set" << ands[i].lhs << "to" << (int)res;
  }
#elif ALGORITHM == 1 // intLookup
  for (unsigned int i = 0; i < num_ands; i++) {
    TERNARY_TYPE l = s[ands[i].rhs0 >> 1];
    TERNARY_TYPE r = s[ands[i].rhs1 >> 1];
    l ^= (ands[i].rhs0 & 1u);
    r ^= (ands[i].rhs1 & 1u);
    const TERNARY_TYPE lookup_index = ((l << 2) | r) << 1;
    constexpr uint32_t lookup_table = 0b10101000101010001010010000000000;
    const TERNARY_TYPE lookup_result = (lookup_table >> lookup_index) & 3;
    s[ands[i].lhs >> 1] = static_cast<ternary>(lookup_result);
  }
#elif ALGORITHM == 2 // tableLookup
  for (unsigned int i = 0; i < num_ands; i++) {
    TERNARY_TYPE l = s[ands[i].rhs0 >> 1];
    TERNARY_TYPE r = s[ands[i].rhs1 >> 1];
    l ^= (ands[i].rhs0 & 1u);
    r ^= (ands[i].rhs1 & 1u);
    const TERNARY_TYPE lookup_index = (l << 2) | r;
    static constexpr ternary lookup_table[] = {X0, X0, X0, X0, X0, X1, X, X,
                                               X0, X,  X,  X,  X0, X,  X, X};
    const TERNARY_TYPE lookup_result = lookup_table[lookup_index];
    s[ands[i].lhs >> 1] = static_cast<ternary>(lookup_result);
  }
#else
#error Invalid ENCODING / ALGORITHM combination
#endif
}
