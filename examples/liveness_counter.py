#!/usr/bin/env python3

from sys import argv, exit
if len(argv) != 3:
    print("Usage: ./liveness_counter.py <number of latches> <live bit>")
    exit(1)
n = int(argv[1])
live_bit = int(argv[2])
assert 0 <= live_bit < n

v = 2
I = [v]; v += 2
L = []
for _ in range(n):
    L.append(v); v += 2
O = L[live_bit]

gates = []
def gate(x, y):
    global v
    l = v
    v += 2
    gates.append(f"{l} {x} {y}")
    return l


# Ripple-carry add input
Lnext = []
carry = I[0]  # start with constant 1 to add one every step
inc_bits = []
for bit in L:
    # XOR via (bit & ~carry) | (~bit & carry)
    t1 = gate(bit, carry ^ 1)
    t2 = gate(bit ^ 1, carry)
    t3 = gate(t1 ^ 1, t2 ^ 1)
    inc = t3 ^ 1
    inc_bits.append(inc)
    carry = gate(bit, carry)

saturated = carry
for inc in inc_bits:
    t = gate(inc ^ 1, saturated ^ 1)
    Lnext.append(t ^ 1)
assert len(L) == len(Lnext) == n

M = (v - 2) // 2
print(f"aag {M} {len(I)} {len(L)} {1} {len(gates)}")
for l in I:
    print(l)
for l, n in zip(L, Lnext):
    print(f"{l} {n}")
print(O)
for a in gates:
    print(a)
