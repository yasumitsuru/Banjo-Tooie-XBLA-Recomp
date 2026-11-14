#!/usr/bin/env python3
"""Run the native display-mode matrix in disposable, isolated profiles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone


APP_ROOT = Path(__file__).resolve().parents[1]
RESOLUTIONS = {
    # M04 (C-22): 1080p desenha a 2x (2560x1440) e a apresentacao reduz para 1920x1080
    "1080p": ("R1080p", 2560, 1440),
    "1440p": ("R1440p", 2560, 1440),
    "4k": ("R2160p", 3840, 2160),
}
MODES = ("Janela", "SemBordas", "TelaCheia")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def check_originals(baseline: dict) -> dict:
    root = APP_ROOT.parent / "Banjo Tooie BR"
    rows = baseline.get("original_assets", [])
    results = []
    for row in rows:
        path = root.joinpath(*Path(row["path"]).parts)
        actual = sha256(path) if path.is_file() else None
        results.append(
            {
                "path": row["path"],
                "expected_sha256": row["expected_sha256"],
                "actual_sha256": actual,
                "matched": actual == row["expected_sha256"],
            }
        )
    return {
        "count": len(results),
        "matched": sum(item["matched"] for item in results),
        "ok": len(results) == 36 and all(item["matched"] for item in results),
        "files": results,
    }


def write_initial_settings(profile: Path, resolution: str, mode: str) -> Path:
    user_root = profile / "userdata"
    user_root.mkdir(parents=True, exist_ok=True)
    settings = user_root / "pc_settings.toml"
    settings.write_text(
        "settings_version = 3\n"
        f'resolution = "{resolution}"\n'
        "resolution_auto = false\n"
        f'display_mode = "{mode}"\n',
        encoding="utf-8",
        newline="\n",
    )
    return settings


def run_process(
    exe_source: Path,
    game_root: Path,
    profile: Path,
    report_path: Path,
    *,
    transitions: bool = False,
    transition_input: bool = False,
    set_resolution: str | None = None,
    capture_files: bool = True,
    timeout: int = 120,
) -> dict:
    bin_dir = profile / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    exe = bin_dir / "banjotooie.exe"
    dll = bin_dir / "rexruntime.dll"
    if not exe.exists():
        shutil.copy2(exe_source, exe)
        shutil.copy2(exe_source.with_name("rexruntime.dll"), dll)

    user_root = profile / "userdata"
    cache_root = profile / "cache"
    log_root = profile / "logs"
    log_root.mkdir(parents=True, exist_ok=True)
    args = [
        str(exe),
        f"--game_data_root={game_root}",
        f"--user_data_root={user_root}",
        f"--cache_path={cache_root}",
        f"--log_file={log_root / 'game.log'}",
        f"--boot_report={log_root / 'boot.json'}",
        "--port_silent=true",
        "--probe-display",
        str(report_path),
    ]
    if transitions:
        args.append("--probe-display-transitions")
    if transition_input:
        args.append("--probe-display-transition-input")
    if set_resolution:
        args.extend(("--probe-display-set-resolution", set_resolution))
    if not capture_files:
        args.append("--probe-display-no-capture")
    args.extend(("--frames", "120"))

    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 4  # SW_SHOWNOACTIVATE: visible GUI without taking focus.
    log_path = log_root / f"{report_path.stem}.log"
    started = datetime.now(timezone.utc).isoformat(timespec="seconds")
    timed_out = False
    with log_path.open("wb") as log:
        process = subprocess.Popen(
            args,
            cwd=bin_dir,
            stdin=subprocess.DEVNULL,
            stdout=log,
            stderr=subprocess.STDOUT,
            startupinfo=startup,
            creationflags=subprocess.CREATE_NEW_PROCESS_GROUP,
        )
        try:
            return_code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
            return_code = process.wait(timeout=10)
    report = None
    if report_path.is_file():
        try:
            report = json.loads(report_path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError) as exc:
            report = {"report_parse_error": str(exc)}
    return {
        "started_at_utc": started,
        "command": [str(arg) for arg in args],
        "exit_code": return_code,
        "timed_out": timed_out,
        "report_path": str(report_path),
        "log_path": str(log_path),
        "report": report,
    }


def require_report(
    run: dict,
    resolution: str,
    mode: str,
    *,
    transitions: bool,
    transition_input: bool = False,
) -> None:
    report = run.get("report")
    if run["timed_out"]:
        raise RuntimeError(f"Probe timed out: {run['report_path']}")
    if run["exit_code"] != 0:
        raise RuntimeError(f"Probe exited {run['exit_code']}: {run['report_path']}")
    if not isinstance(report, dict):
        raise RuntimeError(f"Probe did not write a JSON report: {run['report_path']}")
    if not report.get("ok"):
        raise RuntimeError(f"Probe reported failure: {run['report_path']}")
    requested = report.get("requested", {})
    if requested.get("resolution") != resolution or requested.get("display_mode") != mode:
        raise RuntimeError(f"Probe applied a different request: {run['report_path']}")
    applied = report.get("applied", {})
    if applied.get("resolution") != resolution or applied.get("display_mode") != mode:
        raise RuntimeError(f"Requested settings were not applied in this process: {run['report_path']}")
    guest = report.get("guest_framebuffer", {})
    expected = RESOLUTIONS[resolution][1:]
    if (guest.get("width"), guest.get("height")) != expected:
        raise RuntimeError(
            f"Unexpected guest framebuffer for {resolution}: "
            f"{guest.get('width')}x{guest.get('height')} (expected {expected[0]}x{expected[1]})"
        )
    if not guest.get("captured") or not guest.get("non_blank"):
        raise RuntimeError(f"Guest capture invalid: {run['report_path']}")
    window = report.get("window", {})
    if not window.get("visible") or window.get("minimized"):
        raise RuntimeError(f"Game window is not visible: {run['report_path']}")
    if not window.get("physical_coordinates"):
        raise RuntimeError(f"Physical DPI coordinates are unavailable: {run['report_path']}")
    if not window.get("mode_match"):
        raise RuntimeError(f"Requested mode did not match live window state: {run['report_path']}")
    window_capture = window.get("capture", {})
    if not window_capture.get("captured") or not window_capture.get("non_blank"):
        raise RuntimeError(f"Window capture invalid: {run['report_path']}")
    if not window_capture.get("content_match"):
        raise RuntimeError(f"Window image does not correlate with the guest framebuffer: {run['report_path']}")
    surface = report.get("resolution_surface", {})
    if mode == "Janela" and surface.get("work_area_fits_requested") and not surface.get("client_matches_requested"):
        raise RuntimeError(f"Window client does not match the selected output size: {run['report_path']}")
    if report.get("frames", {}).get("advanced", 0) <= 0:
        raise RuntimeError(f"Game stopped presenting during probe: {run['report_path']}")
    if transitions:
        checks = report.get("transitions", [])
        if len(checks) != 8 or any(not check.get("mode_match") or check.get("frames_advanced", 0) <= 0 for check in checks):
            raise RuntimeError(f"Display transition sequence failed: {run['report_path']}")
        if not report.get("settings_saved"):
            raise RuntimeError(f"Display settings did not save: {run['report_path']}")
        if transition_input:
            input_checks = report.get("input", {})
            if not input_checks.get("navigation_reached") or not input_checks.get("before", {}).get("moved") or not input_checks.get("after", {}).get("moved"):
                raise RuntimeError(f"Game input failed before or after transitions: {run['report_path']}")


def require_resolution_save(run: dict, from_resolution: str, to_resolution: str) -> None:
    report = run.get("report")
    if run["timed_out"] or run["exit_code"] != 0 or not isinstance(report, dict) or not report.get("ok"):
        raise RuntimeError(f"Resolution setting probe failed: {run['report_path']}")
    change = report.get("resolution_change", {})
    if (
        change.get("from") != from_resolution
        or change.get("to") != to_resolution
        or not change.get("restart_required")
        or not change.get("saved")
        or not report.get("settings_saved")
    ):
        raise RuntimeError(f"Resolution did not persist with a restart requirement: {run['report_path']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=APP_ROOT / "out" / "native" / "banjotooie.exe")
    parser.add_argument("--game-data", type=Path, default=APP_ROOT.parent / "Banjo Tooie BR")
    parser.add_argument("--out", type=Path, default=None)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument("--limit", type=int, default=0, help="run only the first N matrix states")
    args = parser.parse_args()

    exe = args.exe.resolve()
    game_root = args.game_data.resolve()
    if os.name != "nt":
        parser.error("the display matrix requires Windows Win32 window APIs")
    if not exe.is_file() or not exe.with_name("rexruntime.dll").is_file():
        parser.error(f"built game and rexruntime.dll not found beside {exe}")
    if not game_root.is_dir():
        parser.error(f"game data root not found: {game_root}")

    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    run_root = (args.out or APP_ROOT / "_audit" / "mission03" / f"run-{timestamp}").resolve()
    if run_root.exists():
        parser.error(f"output directory already exists; choose a fresh --out path: {run_root}")
    run_root.mkdir(parents=True)
    baseline_path = APP_ROOT / "_audit" / "mission03" / "baseline.json"
    baseline = json.loads(baseline_path.read_text(encoding="utf-8-sig"))
    expected_states = {(resolution, mode) for resolution in RESOLUTIONS for mode in MODES}
    validated_states: set[tuple[str, str]] = set()
    validated_reopens: set[tuple[str, str]] = set()
    states = []
    matrix_complete = False
    result = {
        "schema": "m03-display-matrix-v1",
        "started_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "exe": str(exe),
        "exe_sha256": sha256(exe),
        "game_data_root": str(game_root),
        "run_root": str(run_root),
        "baseline_assets_before": check_originals(baseline),
        "states": [],
        "reopens": [],
        "validation_errors": [],
        "environment_limits": [],
        "status": "FAIL",
    }
    if not result["baseline_assets_before"]["ok"]:
        result["error"] = "The 36 read-only original asset hashes do not match the M00 baseline."
    else:
        states = [(resolution, mode) for resolution in RESOLUTIONS for mode in MODES]
        if args.limit:
            states = states[: args.limit]
        restart_sources = {"1080p": "1440p", "1440p": "1080p", "4k": "1440p"}
        for resolution, mode in states:
            profile = run_root / "profiles" / f"{resolution}-{mode}"
            source_resolution = restart_sources[resolution] if mode == "Janela" else resolution
            settings = write_initial_settings(profile, source_resolution, mode)
            resolution_setup = None
            if mode == "Janela":
                setup_report = profile / "reports" / "resolution-save.json"
                try:
                    setup = run_process(
                        exe,
                        game_root,
                        profile,
                        setup_report,
                        set_resolution=resolution,
                        capture_files=False,
                        timeout=args.timeout,
                    )
                    require_resolution_save(setup, source_resolution, resolution)
                    resolution_setup = setup
                except Exception as exc:
                    result["validation_errors"].append(
                        f"{resolution}/{mode} resolution save: {type(exc).__name__}: {exc}"
                    )

            first_report = profile / "reports" / "initial.json"
            check_input = mode == "Janela"
            try:
                first = run_process(
                    exe,
                    game_root,
                    profile,
                    first_report,
                    transitions=True,
                    transition_input=check_input,
                    timeout=max(args.timeout, 240),
                )
                first["profile_settings_path"] = str(settings)
                first["profile_settings_sha256"] = sha256(settings) if settings.is_file() else None
                try:
                    require_report(first, resolution, mode, transitions=True,
                                   transition_input=check_input)
                    validated_states.add((resolution, mode))
                except Exception as exc:
                    result["validation_errors"].append(
                        f"{resolution}/{mode} matrix: {type(exc).__name__}: {exc}"
                    )
                state_result = {"resolution": resolution, "mode": mode, **first}
                if resolution_setup:
                    state_result["resolution_setup"] = resolution_setup
                result["states"].append(state_result)
                surface = (first.get("report") or {}).get("resolution_surface", {})
                if surface.get("status") == "BLOCKED_WORK_AREA":
                    result["environment_limits"].append(
                        f"{resolution}/{mode}: requested client does not fit the monitor work area"
                    )
            except Exception as exc:
                result["validation_errors"].append(
                    f"{resolution}/{mode} launch: {type(exc).__name__}: {exc}"
                )

            reopen_report = profile / "reports" / "reopen.json"
            try:
                reopened = run_process(
                    exe,
                    game_root,
                    profile,
                    reopen_report,
                    capture_files=False,
                    timeout=args.timeout,
                )
                try:
                    require_report(reopened, resolution, mode, transitions=False)
                    validated_reopens.add((resolution, mode))
                except Exception as exc:
                    result["validation_errors"].append(
                        f"{resolution}/{mode} reopen: {type(exc).__name__}: {exc}"
                    )
                reopened["profile_settings_sha256"] = sha256(settings) if settings.is_file() else None
                result["reopens"].append({"resolution": resolution, "mode": mode, **reopened})
                surface = (reopened.get("report") or {}).get("resolution_surface", {})
                if surface.get("status") == "BLOCKED_WORK_AREA":
                    result["environment_limits"].append(
                        f"{resolution}/{mode} reopen: requested client does not fit the monitor work area"
                    )
            except Exception as exc:
                result["validation_errors"].append(
                    f"{resolution}/{mode} reopen launch: {type(exc).__name__}: {exc}"
                )
        matrix_complete = validated_states == expected_states and validated_reopens == expected_states
        result["status"] = (
            "FAIL" if result["validation_errors"]
            else "BLOCKED" if result["environment_limits"]
            else "PASS_PRELIMINARY" if matrix_complete
            else "PARTIAL"
        )

    result["matrix_coverage"] = {
        "expected_unique_states": len(expected_states),
        "selected_unique_states": len(set(states)),
        "validated_initial_states": len(validated_states),
        "validated_reopen_states": len(validated_reopens),
        "complete": matrix_complete,
    }
    result["baseline_assets_after"] = check_originals(baseline)
    result["finished_at_utc"] = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if not result["baseline_assets_after"]["ok"]:
        result["status"] = "FAIL"
    summary_path = run_root / "summary.json"
    summary_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(summary_path)
    print(f"status={result['status']} states={len(result['states'])} reopens={len(result['reopens'])}")
    if result.get("error"):
        print(result["error"], file=sys.stderr)
    if not result["baseline_assets_after"]["ok"]:
        print("The original asset hash check failed after the run.", file=sys.stderr)
    return 0 if result["status"] == "PASS_PRELIMINARY" else 2 if result["status"] in ("BLOCKED", "PARTIAL") else 1


if __name__ == "__main__":
    raise SystemExit(main())
