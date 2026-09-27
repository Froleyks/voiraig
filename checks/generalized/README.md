# Generalized liveness regressions

From the repository root:

```sh
make verify-generalized
```

The default Python check runs 64 deterministic fixture/configuration pairs:
k-liveness with and without stabilizers, L2S, and RLIVE on 15 liveness cases,
and IC3 and k-induction on two safety cases. It checks selected-engine reporting,
expected SAT/UNSAT outcomes, concrete SAT traces against the original models,
property-index preservation, and retained engine-native models and witnesses.
The independent trace checker also has deliberately invalid traces to reject.
ASCII and binary AIGER are read directly; no experiment workspace is needed.
Four additional output-option regressions check that `--trace=false` and
`--certificate=false` suppress original and native evidence, that disabling
certificates skips their backend, and that disabling both emits no artifacts.

Use `--binary PATH` for another Voiraig executable, `--seconds N` for the
per-command timeout, `--case NAME` to select a fixture, and `--output DIRECTORY`
to retain fixture models, evidence, and command logs. The output directory must
not contain results for the selected fixture/configuration pairs already.

To additionally check both original and native UNSAT certificates, provide a
directory containing `certifaiger`, `aigsplit`, `aigtocnf`, and `cadical`:

```sh
make verify-generalized GENERALIZED_TOOLS=/path/to/checker/bin
```

This requires exactly the nine named Certifaiger obligations and checks each
CNF with CaDiCaL. It does not independently verify CaDiCaL's UNSAT proofs. Without
`--tools-dir`, certificate syntax and preservation are checked, not validity.
If `aigsim` is on `PATH`, it also simulates SAT traces; `--aigsim PATH` selects
an explicit executable.

`complete_liveness.cpp` independently compares SAT-based trace completion with
exhaustive explicit-state enumeration on 800 small models from a fixed seed.
It covers gates, functional and self resets, constraints, multiple justice
properties, fairness, and sparse input/latch cubes. Extra cases reject
out-of-range and incorrectly projected literals before AIGER lookup and check
that failed completion preserves both the trace and property-index output.
`make verify-generalized` builds and runs the `verify-liveness-trace` CMake
target for this harness before running the Python fixtures. To run the Python
portion alone, use `python3 checks/generalized/verify.py`; its optional checker
directory argument is `--tools-dir /path/to/checker/bin`. The Make target's
per-command timeout is `GENERALIZED_SECONDS=10` by default.
