#!/usr/bin/env python3
"""Check explicit witness maps in every circuit output format and solver engine."""
import argparse
from pathlib import Path
import re
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "checks/generalized"))
from verify import LIVE, aag, check_certificate, command, read_model, verify_trace_checker


def check_maps(model, witness, rlive=False):
    original, circuit = read_model(model), read_model(witness)
    mappings, interventions = {}, {}
    for (kind, index), name in circuit["symbols"].items():
        if kind not in "il":
            continue
        literal = circuit["inputs"][index] if kind == "i" else circuit["latches"][index][0]
        for marker, target in re.findall(r"([=<])\s*(\d+)", name):
            result = mappings if marker == "=" else interventions
            assert literal not in result, f"duplicate {marker} annotation in {witness}"
            result[literal] = int(target)
    model_interface = original["inputs"] + [latch[0] for latch in original["latches"]]
    assert sorted(mappings.values()) == sorted(model_interface), \
        f"missing or incorrect explicit model map (=) in {witness}: {mappings}"
    latch_interventions = {lit: nxt for lit, nxt, _ in circuit["latches"] if nxt > 1}
    actual_latch_maps = {lit: target for lit, target in interventions.items()
                         if lit not in circuit["inputs"]}
    assert actual_latch_maps == latch_interventions, \
        f"missing or incorrect explicit latch intervention (<) in {witness}"
    input_maps = {lit: target for lit, target in interventions.items()
                  if lit in circuit["inputs"]}
    if rlive:
        # RLIVE explicitly substitutes fresh next inputs as well as latch
        # next functions. Adding '=' must preserve these existing '<' maps.
        assert set(input_maps) == set(circuit["inputs"][:len(original["inputs"])]), \
            f"lost explicit RLIVE input interventions in {witness}"
        assert all(target in circuit["inputs"] and target != lit
                   for lit, target in input_maps.items()), "invalid RLIVE next-input map"
    else:
        assert not input_maps, f"unexpected default input intervention in {witness}"
    return original, circuit, mappings


def verify(root, binary, seconds, tools_dir):
    verify_trace_checker(root)
    safety = "aag 4 1 2 0 1 1\n2\n4 0 0\n6 8 0\n6\n8 4 2\n"
    live = "aag 3 1 1 0 1 0 0 1\n2\n4 6 0\n1\n4\n6 4 2\n"
    generalized = "aag 3 1 1 0 1 0 0 1\n2\n4 6 0\n2\n4\n1\n6 4 2\n"
    safety_configs = (("ic3", "--safety=0"),
                      ("kind", "--safety=1", "--paths=0"),
                      ("kind-unique", "--safety=1", "--paths=0", "--unique=true"))
    cases = (("safety", safety, safety_configs), ("liveness", live, LIVE),
             ("generalized", generalized, LIVE),
             ("empty-interface", aag(bad=(0,)), safety_configs[:1]),
             ("constant-next", aag(latches=((2, 0, 0),), bad=(2,)), safety_configs[:1]))
    runs = certificates = 0
    for name, source, configs in cases:
        for config, *flags in configs:
            for format in ("aag", "aig", "stdout"):
                workdir = root / name / config / format
                workdir.mkdir(parents=True)
                model = workdir / "model.aag"
                witness = workdir / ("certificate.aag" if format == "stdout" else f"certificate.{format}")
                model.write_text(source)
                args = (binary, *flags, model) if format == "stdout" else (binary, *flags, model, witness)
                output = command(args, workdir / "voiraig.log", seconds)
                assert re.findall(r"^(sat|unsat)$", output, re.M) == ["unsat"], \
                    f"unexpected result in {workdir}"
                if format == "stdout":
                    start = re.search(r"^aag ", output, re.M)
                    assert start, f"missing stdout circuit in {workdir}"
                    witness.write_text(output[start.start():output.rfind("unsat\n")])
                original, circuit, mappings = check_maps(model, witness, name == "liveness" and config == "rlive")
                if name == "safety" and config.startswith("kind"):
                    assert re.search(r"Voiraig: k: [2-9]\d*", output), "fixture did not reach nontrivial k-induction"
                    assert any(lit != target for lit, target in mappings.items()), \
                        "fixture did not exercise nonidentity k-induction mapping"
                    assert len(circuit["latches"]) > len(original["latches"]) or \
                        len(circuit["inputs"]) > len(original["inputs"]), "missing auxiliary history/oracle variables"
                pairs = [(model, witness, "original-check")]
                if name == "generalized" and format != "stdout":
                    search_model = Path(str(witness) + ".search-model.aig")
                    search_witness = Path(str(witness) + ".search-certificate.aig")
                    check_maps(search_model, search_witness, config == "rlive")
                    assert not read_model(search_model)["symbols"], "raw search model acquired certificate mappings"
                    pairs.append((search_model, search_witness, "native-check"))
                for checked_model, checked_witness, label in pairs:
                    if tools_dir:
                        check_certificate(checked_model, checked_witness, workdir / label, tools_dir, seconds)
                    certificates += 1
                runs += 1
                print(f"PASS {name}/{config}/{format}: explicit mappings and interventions", flush=True)
    evidence = "; all nine CNFs UNSAT per certificate" if tools_dir else "; CNF checking disabled"
    print(f"Verified {runs} runs and {certificates} circuit certificates{evidence}.", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "bin/voiraig")
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--tools-dir", type=Path, help="directory containing certifaiger, aigsplit, aigtocnf and cadical")
    parser.add_argument("--output", type=Path, help="retain fixtures, certificates and logs in this new directory")
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
        verify(args.output.resolve(), args.binary.resolve(), args.seconds, args.tools_dir)
    else:
        with tempfile.TemporaryDirectory(prefix="verify-mappings-") as directory:
            verify(Path(directory), args.binary.resolve(), args.seconds, args.tools_dir)


if __name__ == "__main__":
    main()
