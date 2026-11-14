"""Exercise the actual injected display gate on a source-owned D3D9 fixture."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import time


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def visible_owned_windows(pids):
    """Read-only check restricted to this fixture and its loader error title."""
    user = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user.EnumWindows.restype = wintypes.BOOL
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.IsWindowVisible.restype = wintypes.BOOL
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    found = []

    @callback_type
    def inspect(window, _):
        if not user.IsWindowVisible(window):
            return True
        pid = wintypes.DWORD()
        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
        title = ctypes.create_unicode_buffer(512)
        user.GetWindowTextW(window, title, len(title))
        if pid.value in pids or "tdur_display_startup_witness.exe" in title.value:
            found.append(int(window))
        return True

    if not user.EnumWindows(inspect, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    return found


def main():
    parser = argparse.ArgumentParser()
    for name in ("launcher", "witness", "dll", "work-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", choices=("borderless", "exclusive"), default="borderless")
    parser.add_argument("--frametime", default="off", choices=(
        "off", "enabled", "invalid", "missing-display", "missing-installation",
        "host-log", "source-log"))
    # C2: TDUR_BORDERLESS_CONSTRAINT absent (default), "0" (off) or invalid.
    parser.add_argument("--constraint", default="default",
                        choices=("default", "off", "invalid"))
    # TDUR_RETAIL_FINISH_BAND absent (on by default), "0" or invalid.
    parser.add_argument("--finish-band", default="default",
                        choices=("default", "off", "invalid"))
    args = parser.parse_args()
    if args.constraint != "default" and args.frametime != "off":
        parser.error("--constraint is exercised with --frametime off only")
    witness = args.witness.resolve(strict=True)
    if witness.name != "tdur_display_startup_witness.exe":
        raise RuntimeError("Only the owned startup fixture may execute")
    root = args.work_root.resolve() / str(time.time_ns())
    source = root / "source-documents"
    saves = source / "saves"
    private = root / "private-documents"
    installation = root / "source-installation"
    saves.mkdir(parents=True)
    private.mkdir()
    installation.mkdir()
    if args.frametime == "source-log":
        private = installation / "private-documents"
        private.mkdir()
    if args.frametime == "host-log":
        target = private / witness.name
        shutil.copyfile(witness, target)
        witness = target
    (installation / "canary.bin").write_bytes(b"source-owned installation fixture\n")
    for index in range(17):
        (saves / f"canary{index}.sav").write_bytes(f"save fixture {index}\n".encode())
    before = hashes(source)
    installation_before = hashes(installation)
    pid_file = private / "pid.json"
    resume = private / "resume"
    report = private / "startup-report.json"
    log = private / "observer.log"
    env = dict(os.environ, TDUR_SOURCE_INSTALLATION=str(installation),
               TDUR_D3D12_DISPLAY="1", TDUR_DISPLAY_MODE=args.mode,
               TDUR_RETAIL_BACKGROUND="1",
               TDUR_RETAIL_MAX_GRAPHICS="1")
    env.pop("TDUR_FRAMETIME_DIAGNOSTICS", None)
    env.pop("TDUR_BORDERLESS_CONSTRAINT", None)
    if args.constraint == "off":
        env["TDUR_BORDERLESS_CONSTRAINT"] = "0"
    elif args.constraint == "invalid":
        env["TDUR_BORDERLESS_CONSTRAINT"] = "2"
    env.pop("TDUR_RETAIL_FINISH_BAND", None)
    if args.finish_band == "off":
        env["TDUR_RETAIL_FINISH_BAND"] = "0"
    elif args.finish_band == "invalid":
        env["TDUR_RETAIL_FINISH_BAND"] = "yes"
    band_rejected = args.finish_band == "invalid"
    rejected = args.frametime not in ("off", "enabled")
    if args.frametime != "off":
        env["TDUR_FRAMETIME_DIAGNOSTICS"] = "1"
    if args.frametime == "invalid":
        env["TDUR_FRAMETIME_DIAGNOSTICS"] = "yes"
    if args.frametime == "missing-display":
        env.pop("TDUR_D3D12_DISPLAY")
    if args.frametime == "missing-installation":
        env.pop("TDUR_SOURCE_INSTALLATION")
    command = [str(args.launcher.resolve(strict=True)), "--exe", str(witness),
               "--dll", str(args.dll.resolve(strict=True)),
               "--source-documents", str(source), "--private-documents", str(private),
               "--source-saves", str(saves), "--log", str(log), "--report", str(report),
               "--pid-file", str(pid_file), "--resume-file", str(resume), "--max-seconds", "15"]
    # This owned process and its children only: loader refusal must return an
    # exit status instead of waiting for the unattended Windows error dialog.
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.SetErrorMode.argtypes = [ctypes.c_uint]
    kernel.SetErrorMode.restype = ctypes.c_uint
    kernel.GetErrorMode.restype = ctypes.c_uint
    previous_error_mode = kernel.SetErrorMode(kernel.GetErrorMode() | 0x8003)
    child = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, text=True,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        deadline = time.monotonic() + 10
        while not pid_file.exists():
            if child.poll() is not None or time.monotonic() > deadline:
                raise AssertionError("No suspended fixture PID; inspect " + str(root))
            time.sleep(0.025)
        identity = json.loads(pid_file.read_text(encoding="utf-8"))
        assert identity["suspended"]
        assert identity["inherited_error_mode"] & 0x8003 == 0x8003
        assert not (private / "display-startup-proof.json").exists()
        resumed_at = time.monotonic()
        resume.write_text("source-owned fixture registered\n", encoding="ascii")
        window_checks = 0
        while True:
            window_checks += 1
            assert not visible_owned_windows({identity["pid"], child.pid}), "owned fixture exposed a window"
            if child.poll() is not None:
                break
            assert time.monotonic() - resumed_at < 20, "fixture deadline exceeded"
            time.sleep(0.01)
        stdout, stderr = child.communicate(timeout=1)
        elapsed = time.monotonic() - resumed_at
        assert child.returncode == 0, stdout + stderr
        result = json.loads(report.read_text(encoding="utf-8"))
        assert result["pid"] == identity["pid"] and not result["timed_out"]
        if args.constraint == "invalid":
            # The bridge refuses explicitly: a logged refusal, no factory,
            # so the owned witness fails; no accepted constraint line.
            assert result["exit_code"] == 1, "unexpected exit; inspect " + str(root)
            assert result["locked_save_files"] == 17 and result["source_saves_unchanged"]
            assert hashes(source) == before
            assert hashes(installation) == installation_before
            trace = log.read_text(encoding="utf-8")
            assert " event=display-borderless-constraint-invalid error=13 " in trace
            assert " event=display-deferred-initialization-failed " in trace
            assert " event=display-borderless-constraint " not in trace
            assert " event=display-deferred-initialization " not in trace
            assert not (private / "display-startup-proof.json").exists()
            (root / "result.json").write_text(json.dumps({
                "game_executed": False, "success": True,
                "refusal": "borderless-constraint", "exit_code": result["exit_code"],
                "visible_owned_windows": 0, "window_checks": window_checks,
                "evidence_directory": str(root)}, indent=2) + "\n", encoding="utf-8")
            print("PASS invalid borderless constraint refused and logged; " + str(root))
            return
        refused = rejected or band_rejected
        assert (result["exit_code"] != 0) == refused, "unexpected exit; inspect " + str(root)
        assert result["locked_save_files"] == 17 and result["source_saves_unchanged"]
        assert hashes(source) == before
        if args.frametime != "source-log":
            assert hashes(installation) == installation_before
        else:
            assert hashlib.sha256((installation / "canary.bin").read_bytes()).hexdigest() == installation_before["canary.bin"]
        if band_rejected:
            # Refused by the display gate, after the diagnostic log opened.
            assert result["exit_code"] == 0xC0000142
            assert "event=retail-finish-band-invalid-selection" in log.read_text(encoding="utf-8")
            assert not (private / "display-startup-proof.json").exists()
            (root / "result.json").write_text(json.dumps({
                "game_executed": False, "success": True, "refusal": "finish-band-invalid",
                "exit_code": result["exit_code"], "elapsed_seconds": elapsed,
                "window_checks": window_checks, "visible_owned_windows": 0,
                "evidence_directory": str(root)}, indent=2) + "\n", encoding="utf-8")
            print("PASS finish-band selection refused at load; " + str(root))
            return
        if rejected:
            assert result["exit_code"] == 0xC0000142
            assert not log.exists(), "refused configuration opened its diagnostic log"
            assert not (private / "display-startup-proof.json").exists()
            (root / "result.json").write_text(json.dumps({
                "game_executed": False, "success": True, "refusal": args.frametime,
                "exit_code": result["exit_code"], "diagnostic_file_created": False,
                "elapsed_seconds": elapsed, "window_checks": window_checks,
                "visible_owned_windows": 0, "error_mode": identity["inherited_error_mode"],
                "evidence_directory": str(root)}, indent=2) + "\n", encoding="utf-8")
            print("PASS refusal before diagnostics: " + args.frametime + "; " + str(root))
            return
        proof = json.loads((private / "display-startup-proof.json").read_text(encoding="utf-8"))
        assert proof["success"] and proof["native_d3d12"] and proof["presents"] == 3
        assert proof["error_mode"] & 0x8003 == 0x8003
        trace = log.read_text(encoding="utf-8")
        summaries = [line for line in trace.splitlines() if "event=frametime-summary " in line]
        assert bool(summaries) == (args.frametime == "enabled")
        if summaries:
            assert len(summaries) == 1
            fields = dict(part.split("=", 1) for part in summaries[0].split() if "=" in part)
            assert fields["calls"] == "3" and fields["durations"] == "3"
            assert fields["intervals"] == "2" and fields["failed"] == "0"
            assert fields["invalid_clock"] == "0" and fields["reports_left"] == "127"
            assert log.stat().st_size < 16384
        events = ["hooks-installed-pre-entry", "display-public-factory-gate-armed",
                  "display-deferred-initialization", "display-first-dxgi-frame"]
        positions = [trace.index("event=" + event) for event in events]
        assert positions == sorted(positions), "factory setup was not deferred in order"
        assert "display-first-dxgi-frame on12=1" in trace and "focus=0" in trace
        constraint_lines = [line for line in trace.splitlines()
                            if " event=display-borderless-constraint " in line]
        expected_constraint = {
            "default": " reason=enabled=1 source=default ",
            "off": " reason=enabled=0 source=environment "}[args.constraint]
        assert len(constraint_lines) == 1, "one borderless constraint line"
        assert expected_constraint in constraint_lines[0], constraint_lines[0]
        assert "retail-video-defaults-identity-refused" in trace
        assert "retail-background-identity-refused" in trace
        assert "retail-background-branches-applied" not in trace
        assert "retail-video-defaults-applied" not in trace
        # On by default, never applied outside the audited image.
        assert "retail-finish-band-applied" not in trace
        assert "retail-finish-band-invalid-selection" not in trace
        if args.finish_band == "off":
            assert trace.count("event=retail-finish-band-disabled") == 1
            assert "retail-finish-band-phase-not-admitted" not in trace
        else:
            assert "retail-finish-band-disabled" not in trace
            assert trace.count("event=retail-finish-band-phase-not-admitted") == 1
        first = next(line for line in trace.splitlines()
                     if "event=display-first-dxgi-frame" in line)
        assert "exclusive=0" in first
        assert ("exclusive_pending=1" in first) == (args.mode == "exclusive")
        assert ("mode=2" in first) == (args.mode == "exclusive")
        (root / "result.json").write_text(json.dumps({
            "game_executed": False, "success": True, "proof": proof,
            "requested_mode": args.mode,
            "frametime": args.frametime, "summary_count": len(summaries),
            "borderless_constraint": args.constraint,
            "finish_band": args.finish_band,
            "visible_owned_windows": 0, "window_checks": window_checks,
            "exclusive_pending": args.mode == "exclusive",
            "source_files_unchanged": True,
            "dll_sha256": hashlib.sha256(args.dll.read_bytes()).hexdigest(),
            "evidence_directory": str(root)}, indent=2) + "\n", encoding="utf-8")
        print("PASS injected deferred D3D9On12 startup; 17 source canaries unchanged; " + str(root))
    finally:
        kernel.SetErrorMode(previous_error_mode)
        if child.poll() is None:
            child.kill()  # Only this owned launcher; its Job closes the fixture.
            child.wait(timeout=5)


if __name__ == "__main__":
    main()
