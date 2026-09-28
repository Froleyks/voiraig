# Witness symbol-map regressions

Run `make verify-mappings` from the repository root. The check exercises ASCII,
binary and stdout circuit output for IC3, nontrivial regular and unique
k-induction, and single/generalized liveness with all four engine configurations.
It also checks retained native generalized-liveness certificates and empty maps.

Every original input/latch must occur exactly once as an `=` target. Every
nonconstant witness latch next function must have the matching `<` intervention;
RLIVE's explicit next-input interventions must survive adding model maps.
Gate-bearing fixtures and nonidentity k-induction mappings catch stale literals
and overwritten explicit maps. The normalized search models remain raw models.

Use `make verify-mappings MAPPINGS_TOOLS=/path/to/checker/bin` to require all nine
Certifaiger obligations to be UNSAT in CaDiCaL for every certificate. The directory
must contain `certifaiger`, `aigsplit`, `aigtocnf` and `cadical`. This checks the
CNFs, not the solver's UNSAT proofs. The suite runs 39 solver/format combinations
and checks 47 original/native certificates.

The standalone script accepts `--binary PATH`, `--seconds N`, `--tools-dir PATH`
and `--output DIRECTORY` (a fresh output location for retained evidence). The
Make target's timeout is controlled by `MAPPINGS_SECONDS`, defaulting to 10.
