#!/usr/bin/env python3
"""Record or verify the checked-in Project Parallax decisive-flow traces."""

from __future__ import annotations

import argparse
import filecmp
import json
import shutil
import sys
import tempfile
from pathlib import Path

from doodad_cli.contract import DoodadError
from doodad_cli.scene_trace import (
    record_flow_trace,
    verify_trace_bundle_fresh,
)


ROOT = Path(__file__).resolve().parents[1]
SUITE = ROOT / "apps" / "conformance-suite.json"
FLOWS = ROOT / "apps" / "conformance-flows.json"
OUTPUT = ROOT / "reference" / "traces"


def _different_files(left: Path, right: Path) -> list[str]:
    comparison = filecmp.dircmp(left, right)
    differences = [
        *(f"only in generated traces: {name}" for name in comparison.left_only),
        *(f"only in checked-in traces: {name}" for name in comparison.right_only),
        *(
            f"changed trace artifact: {(left / name).relative_to(left)}"
            for name in comparison.diff_files
        ),
        *(
            f"unreadable trace artifact: {(left / name).relative_to(left)}"
            for name in comparison.funny_files
        ),
    ]
    for name in comparison.common_dirs:
        differences.extend(
            f"{name}/{item}"
            for item in _different_files(left / name, right / name)
        )
    return differences


def generate(output: Path, *, replay: bool) -> tuple[int, int]:
    suite = json.loads(SUITE.read_text(encoding="utf-8"))["apps"]
    flows = json.loads(FLOWS.read_text(encoding="utf-8"))["flows"]
    output.mkdir(parents=True, exist_ok=True)
    entries = 0
    checkpoints = 0
    for app in suite:
        slug = app["slug"]
        print(f"parallax trace: {slug}", flush=True)
        bundle = record_flow_trace(
            ROOT,
            slug,
            flows[slug],
            output / slug / "decisive",
        )
        if replay:
            proof = verify_trace_bundle_fresh(ROOT, bundle.directory)
            entries += int(proof["entries"])
            checkpoints += int(proof["checkpoints"])
        else:
            entries += len(bundle.trace["entries"])
            checkpoints += len(bundle.checkpoints["checkpoints"])
    return entries, checkpoints


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--check",
        action="store_true",
        help="regenerate in a temporary directory and compare byte for byte",
    )
    parser.add_argument(
        "--skip-replay",
        action="store_true",
        help="record artifacts without running the fresh-process replay gate",
    )
    options = parser.parse_args()
    try:
        if options.check:
            with tempfile.TemporaryDirectory(
                prefix="parallax-traces-",
                dir=ROOT / "target",
            ) as temporary:
                generated = Path(temporary) / "traces"
                counts = generate(
                    generated,
                    replay=not options.skip_replay,
                )
                if not OUTPUT.is_dir():
                    raise DoodadError("checked-in Parallax traces are missing")
                differences = _different_files(generated, OUTPUT)
                if differences:
                    raise DoodadError("\n".join(differences))
        else:
            temporary = OUTPUT.with_name("traces.next")
            if temporary.exists():
                shutil.rmtree(temporary)
            counts = generate(
                temporary,
                replay=not options.skip_replay,
            )
            if OUTPUT.exists():
                shutil.rmtree(OUTPUT)
            temporary.replace(OUTPUT)
    except (DoodadError, OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(
        f"Parallax traces passed: 20 apps, "
        f"{counts[1]} checkpoints, {counts[0]} accepted operations"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
