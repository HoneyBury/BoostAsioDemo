#!/usr/bin/env python3
"""Tank Battle Performance Smoke Test.

Runs the C++ perf benchmark (2/20/100 instances, 100-300 ticks each)
and validates against conservative CI/developer-machine thresholds.

Usage:
    python3 demo/games/tank_battle/scripts/perf_smoke_test.py --build-dir build
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--config", default="Debug")
    args = parser.parse_args()

    build_dir = args.build_dir.resolve()
    repo_root = Path(__file__).resolve().parent.parent.parent.parent

    # Find perf test executable
    perf_exe = build_dir / "demo" / "games" / "tank_battle" / "tests" / args.config / "tank_battle_perf_test.exe"
    if not perf_exe.is_file():
        print(f"ERROR: perf test not found at {perf_exe}")
        print("Build it with: cmake --build <build-dir> --config <config> --target tank_battle_perf_test")
        return 1

    print(f"=== Tank Battle Perf Smoke Test ===")
    print(f"Executable: {perf_exe}")
    print()

    try:
        result = subprocess.run(
            [str(perf_exe)],
            capture_output=True, text=True, timeout=120,
        )
    except subprocess.TimeoutExpired:
        print("ERROR: perf test timed out (120s)")
        return 1

    if result.returncode != 0:
        print(f"ERROR: perf test exited with code {result.returncode}")
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        return 1

    # Parse JSON output
    try:
        report = json.loads(result.stdout)
    except json.JSONDecodeError as e:
        print(f"ERROR: failed to parse perf output: {e}")
        print(result.stdout)
        return 1

    all_passed = report.get("all_passed", False)
    results = report.get("results", [])

    print(f"{'Instances':>10}  {'Ticks':>6}  {'Duration (ms)':>14}  {'Avg Tick (us)':>14}  {'TPS':>10}  {'Status'}")
    print("-" * 70)
    for r in results:
        status = "PASS" if r.get("passed") else "FAIL"
        print(f"{r['num_instances']:>10}  {r['ticks_per_instance']:>6}  "
              f"{r['total_duration_ms']:>14.1f}  {r['avg_tick_duration_us']:>14.1f}  "
              f"{r['ticks_per_second']:>10.0f}  {status}")

    print()
    if all_passed:
        print("=== OVERALL: PASS ===")
        return 0
    else:
        print("=== OVERALL: FAIL ===")
        return 1


if __name__ == "__main__":
    sys.exit(main())
