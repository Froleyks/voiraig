#!/usr/bin/env python3
"""Exercise generalized liveness engines and independently check their evidence."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

CHECKS = "Reset Transition Safety Liveness Base Inductive Decrease Closure Consistent".split()
LIVE = (("kliveness-stabilized", "--liveness=0", "--stabilize=true"),
        ("kliveness-plain", "--liveness=0", "--stabilize=false"),
        ("l2s", "--liveness=1"), ("rlive", "--liveness=2"))


def command(args, log, seconds=10, accepted=(0,), cwd=None):
    try:
        result = subprocess.run([str(arg) for arg in args], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=seconds,
                                cwd=cwd)
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        if isinstance(output, bytes):
            output = output.decode(errors="replace")
        Path(log).write_text(output)
        raise AssertionError(f"command exceeded {seconds} seconds; see {log}") from error
    Path(log).write_text(result.stdout)
    if result.returncode not in accepted:
        raise AssertionError(f"command failed ({result.returncode}); see {log}\n{result.stdout[-3000:]}")
    return result.stdout


def check_certificate(model, witness, workdir, tools_dir, seconds=10):
    """Require all nine named Certifaiger obligations to be UNSAT in CaDiCaL.

    This checks the generated CNFs with a SAT solver; it does not validate
    CaDiCaL's UNSAT proofs. Every generator and solver log is retained.
    """
    workdir = Path(workdir).resolve()
    workdir.mkdir(parents=True)
    check = workdir / "check.aig"
    command((tools_dir / "certifaiger", model, witness, check),
            workdir / "certifaiger.log", seconds)
    assert len(read_model(check)["outputs"]) == len(CHECKS), "expected nine certificate obligations"
    command((tools_dir / "aigsplit", "-n", check, "split_"),
            workdir / "aigsplit.log", seconds, cwd=workdir)
    obligations = {p.stem: p for p in workdir.glob("*.aig") if p != check}
    assert set(obligations) == set(CHECKS), f"unexpected obligations: {sorted(obligations)}"
    for name in CHECKS:
        aig = obligations[name]
        cnf = aig.with_suffix(".cnf")
        command((tools_dir / "aigtocnf", aig, cnf), workdir / f"{name}-cnf.log", seconds)
        output = command((tools_dir / "cadical", "--quiet", cnf),
                         workdir / f"{name}-sat.log", seconds, accepted=(20,))
        assert re.findall(r"^s (.+)$", output, re.M) == ["UNSATISFIABLE"], \
            f"{name}: missing unambiguous UNSAT result"


def aag(inputs=(), latches=(), bad=(), constraints=(), justice=(), fairness=()):
    """Construct a tiny gate-free fixture; latch tuples are (literal,next,reset)."""
    variables = len(inputs) + len(latches)
    lines = [f"aag {variables} {len(inputs)} {len(latches)} 0 0 {len(bad)} "
             f"{len(constraints)} {len(justice)} {len(fairness)}"]
    lines.extend(map(str, inputs))
    lines.extend(" ".join(map(str, latch)) for latch in latches)
    lines.extend(map(str, bad))
    lines.extend(map(str, constraints))
    lines.extend(str(len(group)) for group in justice)
    lines.extend(str(signal) for group in justice for signal in group)
    lines.extend(map(str, fairness))
    return "\n".join(lines) + "\n"


def fixtures():
    toggle = ((2, 3, 0),)
    return (
        ("constant-missing-signal", "unsat", aag(justice=((0, 1),))),
        ("multiple-justice-properties", "unsat", aag(justice=((0,), (0,)), fairness=(1,))),
        # Both literals recur but are never simultaneously true.
        ("alternating-justice-signals", "sat", aag(latches=toggle, justice=((2, 3),))),
        ("unfair-accepting-cycle", "unsat", aag(justice=((1, 1),), fairness=(0,))),
        ("alternating-fairness-justice", "sat", aag(latches=toggle, justice=((2,),), fairness=(3,))),
        ("transient-justice-signal", "unsat", aag(latches=((2, 0, 1),), justice=((2, 1),))),
        ("second-justice-accepts", "sat", aag(latches=toggle, justice=((0, 1), (2, 3)))),
        ("constraint-prevents-acceptance", "unsat", aag(inputs=(2,), constraints=(3,), justice=((2, 1),))),
        ("functional-reset-safe", "unsat", aag(latches=((2, 2, 4), (4, 4, 0)), justice=((2, 1),))),
        ("functional-reset-accepting", "sat", aag(latches=((2, 2, 4), (4, 4, 4)), justice=((2, 1),))),
        ("mixed-safety-liveness-safe", "unsat", aag(bad=(0,), justice=((0, 1),))),
        ("mixed-safety-violation", "sat", aag(bad=(1,), justice=((0, 1),))),
        ("mixed-liveness-violation", "sat", aag(latches=toggle, bad=(0,), justice=((2, 3),))),
        ("empty-justice-unfair", "unsat", aag(justice=((),), fairness=(0,))),
        ("empty-justice-accepts", "sat", aag(justice=((),))),
    )


def read_model(path, workdir=None):
    """Read the ASCII and binary AIGER formats without external programs."""
    with Path(path).open("rb") as stream:
        header = stream.readline().split()
        assert 6 <= len(header) <= 10 and header[0] in (b"aag", b"aig"), "invalid AIGER header"
        binary = header[0] == b"aig"
        counts = list(map(int, header[1:])) + [0] * (10 - len(header))
        maximum, ni, nl, no, na, nb, nc, nj, nf = counts
        assert min(counts) >= 0, "negative AIGER count"
        if binary:
            assert maximum == ni + nl + na, "invalid binary AIGER variable count"

        def line():
            row = stream.readline()
            assert row.endswith(b"\n"), "truncated AIGER text section"
            return list(map(int, row.split()))

        def scalar():
            row = line()
            assert len(row) == 1, "expected AIGER literal/count"
            return row[0]

        inputs = [2 * (i + 1) for i in range(ni)] if binary else [scalar() for _ in range(ni)]
        latches = []
        for i in range(nl):
            row = line()
            if binary:
                row = [2 * (ni + i + 1), *row]
            assert len(row) in (2, 3), "invalid latch declaration"
            latches.append(tuple(row if len(row) == 3 else [*row, 0]))
        outputs = [scalar() for _ in range(no)]
        bad = [scalar() for _ in range(nb)]
        constraints = [scalar() for _ in range(nc)]
        sizes = [scalar() for _ in range(nj)]
        assert all(size >= 0 for size in sizes), "negative justice size"
        justice = [[scalar() for _ in range(size)] for size in sizes]
        fairness = [scalar() for _ in range(nf)]

        def delta():
            result = shift = 0
            while True:
                byte = stream.read(1)
                assert byte and shift < 35, "invalid binary AIGER delta"
                value = byte[0]
                result |= (value & 127) << shift
                if value < 128:
                    return result
                shift += 7

        gates = []
        for i in range(na):
            if binary:
                lhs = 2 * (ni + nl + i + 1)
                left = lhs - delta()
                row = (lhs, left, left - delta())
            else:
                row = tuple(line())
            assert len(row) == 3, "invalid AND declaration"
            lhs, left, right = row
            assert 0 <= left < lhs and 0 <= right < lhs and not lhs % 2, "non-topological AND gate"
            gates.append(row)
        literals = [*inputs, *outputs, *bad, *constraints, *fairness]
        literals.extend(lit for latch in latches for lit in latch)
        literals.extend(lit for group in justice for lit in group)
        literals.extend(lit for gate in gates for lit in gate)
        assert all(0 <= lit <= 2 * maximum + 1 for lit in literals), "literal exceeds AIGER maximum"
    return dict(maximum=maximum, inputs=inputs, latches=latches, outputs=outputs, bad=bad,
                constraints=constraints, justice=justice, fairness=fairness, gates=gates)


def check_trace(model, witness, workdir, aigsim=None):
    """Independently simulate a concrete AIGER trace against the source model."""
    circuit = read_model(model, workdir)
    rows = Path(witness).read_text().splitlines()
    assert len(rows) >= 4 and rows[0] == "1" and rows[-1] == ".", "invalid witness framing"
    label = re.fullmatch(r"([bj])(\d+)", rows[1])
    assert label, "missing original property index"
    kind, index = label.group(1), int(label.group(2))
    properties = circuit["justice"] if kind == "j" else circuit["bad"]
    assert index < len(properties), "trace labels an absent property"
    assert len(rows[2]) == len(circuit["latches"]), "wrong initial latch width"
    # AIGER's standard concrete simulation interprets unconstrained x as zero.
    def bits(row, width):
        assert len(row) == width and set(row) <= {"0", "1", "x"}, "invalid stimulus width/value"
        return tuple(character == "1" for character in row)
    current = bits(rows[2], len(circuit["latches"]))
    stimuli = [bits(row, len(circuit["inputs"])) for row in rows[3:-1]]
    assert stimuli, "empty stimulus"
    states, observed, bad_seen = [current], [], False
    signals = [*circuit["fairness"], *properties[index]] if kind == "j" else []
    for frame, stimulus in enumerate(stimuli):
        values = [False] * (circuit["maximum"] + 1)
        def value(literal):
            return values[literal // 2] ^ bool(literal & 1)
        for literal, bit in zip(circuit["inputs"], stimulus):
            values[literal // 2] = bit
        for (literal, _, _), bit in zip(circuit["latches"], current):
            values[literal // 2] = bit
        for literal, left, right in circuit["gates"]:
            values[literal // 2] = value(left) and value(right)
        if frame == 0:
            assert all(reset == literal or value(literal) == value(reset)
                       for literal, _, reset in circuit["latches"]), "trace violates initial resets"
        assert all(value(literal) for literal in circuit["constraints"]), "trace violates a constraint"
        if kind == "b":
            bad_seen |= value(properties[index])
        else:
            observed.append(tuple(value(literal) for literal in signals))
        current = tuple(value(next_lit) for _, next_lit, _ in circuit["latches"])
        states.append(current)
    if kind == "b":
        assert bad_seen, "trace never reaches its bad property"
    else:
        accepting = any(states[start] == states[end]
                        and all(any(observed[frame][signal] for frame in range(start, end))
                                for signal in range(len(signals)))
                        for end in range(1, len(states)) for start in range(end))
        assert accepting, "trace has no loop satisfying every fairness and selected justice signal"
    if aigsim:
        command((aigsim, "-c", model, witness), Path(workdir) / "aigsim.log")
    return kind, index


def verify_trace_checker(workdir):
    # Exercise binary AND deltas and extended-property text before relying on
    # this independent reader for retained native search artifacts.
    ascii_model, binary_model = workdir / "reader.aag", workdir / "reader.aig"
    ascii_model.write_text("aag 3 1 1 0 1 1 1 1 1\n2\n4 6 0\n6\n3\n2\n2\n4\n1\n6 4 2\n")
    binary_model.write_bytes(b"aig 3 1 1 0 1 1 1 1 1\n6 0\n6\n3\n2\n2\n4\n1\n\x02\x02")
    assert read_model(ascii_model) == read_model(binary_model), "binary AIGER reader mismatch"
    binary_model.write_bytes(binary_model.read_bytes()[:-1])
    try:
        read_model(binary_model)
    except AssertionError:
        pass
    else:
        raise AssertionError("AIGER reader accepted a truncated binary AND gate")
    model, trace = workdir / "trace-check.aag", workdir / "trace-check.sat"
    model.write_text(aag(latches=((2, 3, 0),), justice=((2, 3),)))
    trace.write_text("1\nj0\n0\n\n\n.\n")
    check_trace(model, trace, workdir)
    invalid = (
        (aag(latches=((2, 3, 0),), justice=((2, 3),)), "1\nj0\n0\n\n.\n", "nonrepeating path"),
        (aag(latches=((2, 3, 0),), justice=((2, 3),)), "1\nj0\n1\n\n\n.\n", "invalid reset"),
        (aag(inputs=(2,), constraints=(3,), justice=((1, 1),)), "1\nj0\n\n1\n.\n", "invalid constraint"),
        (aag(justice=((1, 1),), fairness=(0,)), "1\nj0\n\n\n.\n", "unfair loop"),
        (aag(justice=((0, 1),)), "1\nj0\n\n\n.\n", "missing justice signal"),
    )
    for source, stimulus, reason in invalid:
        model.write_text(source)
        trace.write_text(stimulus)
        try:
            check_trace(model, trace, workdir)
        except AssertionError:
            pass
        else:
            raise AssertionError(f"trace validator accepted {reason}")


def verify_output_flags(root, binary, seconds):
    """Disabled evidence options must cover original and native artifacts."""
    sat = aag(latches=((2, 3, 0),), justice=((2, 3),))
    unsat = aag(justice=((0, 1),))
    cases = (
        ("sat-no-trace", "sat", sat, ("--trace=false",), True),
        ("unsat-no-certificate", "unsat", unsat, ("--certificate=false",), True),
        ("sat-no-evidence", "sat", sat, ("--trace=false", "--certificate=false"), False),
        ("unsat-no-evidence", "unsat", unsat, ("--trace=false", "--certificate=false"), False),
    )
    for name, expected, source, flags, retain_model in cases:
        workdir = root / "output-flags" / name
        workdir.mkdir(parents=True)
        model, trace, witness = (workdir / filename for filename in
                                 ("model.aag", "trace.sat", "certificate.aig"))
        model.write_text(source)
        output = command((binary, *flags, model, trace, witness),
                         workdir / "voiraig.log", seconds)
        assert re.findall(r"^(sat|unsat)$", output, re.M) == [expected], \
            f"{name}: unexpected result; see {workdir}"
        for artifact in (trace, witness,
                         Path(str(witness) + ".search-trace.sat"),
                         Path(str(witness) + ".search-certificate.aig")):
            assert not artifact.exists(), f"{name}: disabled output created {artifact}"
        search_model = Path(str(witness) + ".search-model.aig")
        assert search_model.exists() == retain_model, \
            f"{name}: unexpected normalized model preservation"
        if retain_model:
            read_model(search_model)
        if expected == "unsat":
            assert not re.search(r"^c certificate-backend:", output, re.M), \
                f"{name}: certificate backend ran with certificates disabled"
        print(f"PASS output-flags/{name}: disabled artifacts absent", flush=True)
    print(f"Verified {len(cases)} evidence-output flag regressions.", flush=True)


def verify(root, binary, seconds, aigsim=None, selected_cases=None, tools_dir=None):
    verify_trace_checker(root)
    count = 0
    cases = [(name, expected, source, LIVE) for name, expected, source in fixtures()]
    safety = (("ic3", "--safety=0"), ("kind", "--safety=1"))
    cases.extend((name, expected, aag(bad=bad), safety) for name, expected, bad in
                 (("multiple-bad-safe", "unsat", (0, 0)), ("second-bad-violated", "sat", (0, 1))))
    if selected_cases:
        unknown = set(selected_cases) - {case[0] for case in cases}
        if unknown:
            raise ValueError(f"unknown fixture names: {sorted(unknown)}")
        cases = [case for case in cases if case[0] in selected_cases]
    for name, expected, source, configurations in cases:
        for config, *flags in configurations:
            workdir = root / name / config
            workdir.mkdir(parents=True)
            model, trace, witness = (workdir / filename for filename in ("model.aag", "trace.sat", "certificate.aig"))
            model.write_text(source)
            output = command((binary, *flags, model, trace, witness), workdir / "voiraig.log", seconds)
            assert re.findall(r"^(sat|unsat)$", output, re.M) == [expected], f"{name}/{config}: unexpected result; see {workdir}"
            if configurations == LIVE:
                assert re.findall(r"^c search-engine: (.+)$", output, re.M) == [config], "selected engine was not reported"
                assert re.findall(r"^c search-result: (.+)$", output, re.M) == [expected], "search result was not retained"
                if expected == "unsat":
                    assert re.search(r"^c certificate-backend: generalized-acceptance-budget-ic3$", output, re.M), "missing certificate backend attribution"
            if expected == "sat":
                kind, index = check_trace(model, trace, workdir, aigsim)
                if name == "second-justice-accepts":
                    assert (kind, index) == ("j", 1), "lost original justice index"
                if name in {"second-bad-violated", "mixed-safety-violation"}:
                    assert kind == "b", "lost original bad-property trace"
            else:
                read_model(witness)
                if tools_dir:
                    check_certificate(model, witness, workdir / "original-check", tools_dir, seconds)
            search_model = Path(str(witness) + ".search-model.aig")
            search_certificate = Path(str(witness) + ".search-certificate.aig")
            search_trace = Path(str(witness) + ".search-trace.sat")
            if configurations == LIVE and name != "mixed-safety-violation":
                assert search_model.exists(), f"missing normalized search model: {workdir}"
                if expected == "unsat":
                    assert search_certificate.exists(), f"missing engine-native search certificate: {workdir}"
                    read_model(search_model)
                    read_model(search_certificate)
                    if tools_dir:
                        check_certificate(search_model, search_certificate, workdir / "search-check", tools_dir, seconds)
                else:
                    assert search_trace.exists(), f"missing normalized search trace: {workdir}"
                    check_trace(search_model, search_trace, workdir, aigsim)
            count += 1
            print(f"PASS {name}/{config}: {expected}; traces and artifacts checked", flush=True)
    evidence = ("all nine CNFs checked for every UNSAT artifact" if tools_dir else
                "certificate artifacts preserved; CNF checking disabled")
    print(f"Verified {count} solver/fixture combinations; {evidence}.", flush=True)
    verify_output_flags(root, binary, seconds)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin/voiraig")
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--tools-dir", type=Path,
                        help="enable certificate CNF checking using certifaiger, aigsplit, aigtocnf and cadical from this directory")
    parser.add_argument("--output", type=Path, help="retain fixture models, witnesses and audit logs here")
    parser.add_argument("--aigsim", type=Path, default=shutil.which("aigsim"))
    parser.add_argument("--case", action="append", help="run only this named fixture (repeatable)")
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("seconds must be positive")
    if args.tools_dir:
        args.tools_dir = args.tools_dir.resolve()
        for name in ("certifaiger", "aigsplit", "aigtocnf", "cadical"):
            if not shutil.which(str(args.tools_dir / name)):
                parser.error(f"missing executable: {args.tools_dir / name}")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        verify(args.output.resolve(), args.binary.resolve(), args.seconds, args.aigsim, args.case, args.tools_dir)
    else:
        with tempfile.TemporaryDirectory(prefix="verify-generalized-") as directory:
            verify(Path(directory), args.binary.resolve(), args.seconds, args.aigsim, args.case, args.tools_dir)


if __name__ == "__main__":
    main()
