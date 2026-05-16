#!/usr/bin/env python3
"""Run short R4 contract gates without starting long-lived services."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


UNIT_FILTER = (
    "ProtoSchemaTest.*:"
    "V2ServiceBoundaryTest.*Envelope*:"
    "V2ServiceBoundaryTest.DecodeHandlerPayloadExtractsTypedPayload:"
    "V2ServiceBoundaryTest.DecodeHandlerPayloadMarksLegacyRawJsonDeprecated:"
    "V2ServiceBoundaryTest.WrapTypedResponseLeavesLegacyPayloadRaw:"
    "V2ActorRuntimeTest.DispatchAllInterleavesReadyActorsFairly:"
    "V2ActorRuntimeTest.ShutdownDuringFairDispatchStopsOtherReadyActors:"
    "HealthCheckTest.BackendHeartbeatRestoresReadinessAfterUnhealthyMark"
)

INTEGRATION_FILTER = (
    "ServiceBusIntegrity.GatewayBridgeRoutePropagatesTraceAndErrorCode:"
    "ServiceBusIntegrity.GatewayBridgeTypedEnvelopePreservesTraceAndError:"
    "ServiceBusIntegrity.GatewayBridgeRecoversAfterBackendConfigUpdate:"
    "ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughLoginBackend:"
    "ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughRoomBackend:"
    "ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughBattleBackend:"
    "ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughMatchBackend:"
    "ServiceBusIntegrity.ProtoEnvelopeRoundTripsThroughLeaderboardBackend"
)


def exe_name(base: str) -> str:
    return f"{base}.exe" if os.name == "nt" else base


def find_executable(build_dir: Path, base_name: str) -> Path:
    names = {exe_name(base_name), base_name}
    matches = sorted(p for p in build_dir.rglob("*") if p.is_file() and p.name in names)
    if os.name == "nt":
        preferred = [
            p for p in matches
            if any(part.lower() in {"debug", "release", "relwithdebinfo", "minsizerel"} for part in p.parts)
        ]
        if preferred:
            matches = preferred
    if not matches:
        raise FileNotFoundError(f"{exe_name(base_name)} not found under {build_dir}")
    return matches[0]


def run_step(name: str, cmd: list[str], cwd: Path, timeout_seconds: int) -> None:
    print(f"==> {name}", flush=True)
    completed = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout_seconds,
        check=False,
    )
    if completed.stdout:
        print(completed.stdout, end="")
    if completed.stderr:
        print(completed.stderr, end="", file=sys.stderr)
    if completed.returncode != 0:
        raise subprocess.CalledProcessError(completed.returncode, cmd)


def cmake_build_args(args: argparse.Namespace, targets: list[str]) -> list[str]:
    cmd = ["cmake", "--build", str(args.build_dir)]
    if args.configuration:
        cmd.extend(["--config", args.configuration])
    cmd.extend(["--target", *targets])
    return cmd


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/windows-msvc-debug"))
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--skip-arch-baseline", action="store_true")
    parser.add_argument("--build-timeout-seconds", type=int, default=120)
    parser.add_argument("--test-timeout-seconds", type=int, default=60)
    parser.add_argument("--baseline-timeout-seconds", type=int, default=45)
    parser.add_argument("--baseline-iterations", type=int, default=2000)
    parser.add_argument("--baseline-actors", type=int, default=2000)
    parser.add_argument("--baseline-actor-limit", type=int, default=10000)
    parser.add_argument("--baseline-battles", type=int, default=100)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = Path(__file__).resolve().parent.parent
    build_dir = args.build_dir.resolve()

    try:
        run_step(
            "v3 proto schema and transport contract",
            [
                sys.executable,
                str(root / "scripts" / "check_v3_proto_schema.py"),
                "--proto-dir",
                str(root / "proto" / "v3"),
                "--require-transport-contract",
            ],
            root,
            args.test_timeout_seconds,
        )

        if not args.skip_build:
            run_step(
                "build R4 focused targets",
                cmake_build_args(
                    args,
                    [
                        "check_v3_proto_transport_contract",
                        "project_v2_unit_tests",
                        "project_v2_integration_tests",
                        "v2_arch_benchmark",
                    ],
                ),
                root,
                args.build_timeout_seconds,
            )

        unit_tests = find_executable(build_dir, "project_v2_unit_tests")
        integration_tests = find_executable(build_dir, "project_v2_integration_tests")
        run_step(
            "R4 unit gates",
            [str(unit_tests), f"--gtest_filter={UNIT_FILTER}"],
            unit_tests.parent,
            args.test_timeout_seconds,
        )
        run_step(
            "R4 integration gates",
            [str(integration_tests), f"--gtest_filter={INTEGRATION_FILTER}"],
            integration_tests.parent,
            args.test_timeout_seconds,
        )

        if not args.skip_arch_baseline:
            run_step(
                "short architecture baseline",
                [
                    sys.executable,
                    str(root / "scripts" / "collect_v2_arch_baseline.py"),
                    "--build-dir",
                    str(build_dir),
                    "--output-root",
                    str(root / "runtime" / "perf" / "v2-arch-baseline"),
                    "--iterations",
                    str(args.baseline_iterations),
                    "--actors",
                    str(args.baseline_actors),
                    "--actor-limit",
                    str(args.baseline_actor_limit),
                    "--battles",
                    str(args.baseline_battles),
                    "--timeout-seconds",
                    str(args.baseline_timeout_seconds),
                ],
                root,
                args.baseline_timeout_seconds + 10,
            )
    except (FileNotFoundError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as exc:
        print(f"R4 contract verification failed: {exc}", file=sys.stderr)
        return 1

    print("R4 contract verification completed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
