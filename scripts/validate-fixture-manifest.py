#!/usr/bin/env python3
"""Run the real validator over every controlled fixture with explicit expectations."""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: validate-fixture-manifest.py <DaedalusAssetValidator>", file=sys.stderr)
        return 2
    executable = Path(sys.argv[1]).resolve()
    root = Path(__file__).resolve().parents[1]
    asset_root = root / "tests" / "assets"
    manifest = json.loads((asset_root / "manifest.json").read_text(encoding="utf-8"))
    failures: list[str] = []
    for category, expectation in (("valid", "--expect-success"), ("invalid", "--expect-failure")):
        for name in manifest[category]:
            result = subprocess.run(
                [str(executable), str(asset_root / category / name), expectation],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                check=False,
            )
            if result.returncode != 0:
                failures.append(f"{category}/{name}: exit {result.returncode}\n{result.stdout}")
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(f"Validated {len(manifest['valid'])} valid/degraded and {len(manifest['invalid'])} invalid fixtures")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
