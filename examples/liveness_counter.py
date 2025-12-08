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
Lnext = []
A = ""
for i in range(n):
    pass

print(f"""
aag {v/2} {len(I)} {len(L)} {1}
{'\n'.join(str(l) + ' ' for l in I)}
{'\n'.join(str(l) + ' ' for l in L)}
{O}
{A}
""")
