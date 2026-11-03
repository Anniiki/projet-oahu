"""Exercise pre-resume registration, save locks and owned timeout on fixtures."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def check(value, message):
    if not value:
        raise AssertionError(message)


def independently_observed_process(pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.GetProcessTimes.argtypes = (wintypes.HANDLE,) + (ctypes.POINTER(wintypes.FILETIME),) * 4
    kernel.GetProcessTimes.restype = wintypes.BOOL
    kernel.GetExitCodeProcess.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
    kernel.GetExitCodeProcess.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x1000, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    values = [wintypes.FILETIME() for _ in range(4)]
    if not kernel.GetProcessTimes(handle, *(ctypes.byref(value) for value in values)):
        error = ctypes.get_last_error()
        kernel.CloseHandle(handle)
        raise ctypes.WinError(error)
    return kernel, handle, (values[0].dwHighDateTime << 32) | values[0].dwLowDateTime


def main():
    parser = argparse.ArgumentParser()
    for name in ("launcher", "witness", "dll", "work-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    # Each run gets a new tree; diagnostics never overwrite prior evidence.
    root = args.work_root / (str(time.time_ns()))
    source = root / "source"
    saves = source / "saves"
    saves.mkdir(parents=True)
    canary = saves / "canary.bin"
    canary.write_bytes(b"abc")
    digest = hashlib.sha256(b"abc").hexdigest()
    observed_job_events = {}
    for mode in ("exit", "timeout", "system", "play", "abnormal-code"):
        private = root / mode
        private.mkdir()
        pid_file = private / "pid.json"
        resume_file = private / "resume"
        report_file = private / "report.json"
        early_marker = private / "injected-before-main.txt"
        command = [str(args.launcher), "--exe", str(args.witness.resolve()),
                   "--dll", str(args.dll.resolve()),
                   "--source-documents", str(source.resolve()),
                   "--private-documents", str(private.resolve()),
                   "--source-saves", str(saves.resolve()),
                   "--log", str((private / "observer.jsonl").resolve()),
                   "--report", str(report_file.resolve()),
                   "--pid-file", str(pid_file.resolve()),
                   "--resume-file", str(resume_file.resolve()),
                   "--max-seconds", "1"]
        config = None
        if mode == "play":
            command += ["--session-mode", "play"]
        if mode == "system":
            config = (private / "SystemPC with spaces.ini").resolve()
            config.write_bytes(b"native private video fixture\r\n")
            command += ["--system-config", str(config)]
        env = dict(os.environ, TDUR_LAUNCH_FIXTURE_MODE=mode,
                   TDUR_RETAIL_EXECUTABLE_VERIFIED="1")
        process = subprocess.Popen(command, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        observed_process = None
        try:
            until = time.monotonic() + 10
            while not pid_file.exists():
                if process.poll() is not None:
                    raise AssertionError("launcher stopped before registration: " +
                                         str(process.communicate()))
                check(time.monotonic() < until, "no suspended PID publication")
                time.sleep(0.025)
            state = json.loads(pid_file.read_text(encoding="utf-8"))
            check(state["suspended"] and state["pid"] != process.pid,
                  "did not publish a separate suspended target")
            kernel, observed_process, created_at = independently_observed_process(state["pid"])
            check(created_at > 0 and state["creation_filetime"] == created_at,
                  "creation FILETIME differs from independent suspended-process query")
            check(not report_file.exists(), "target ran before resume marker")
            check(not early_marker.exists(), "constructor ran while suspended")
            resume_file.write_text("fixture registration complete\n", encoding="ascii")
            stdout, stderr = process.communicate(timeout=10)
            check(process.returncode == 0, stdout + stderr)
            report = json.loads(report_file.read_text(encoding="utf-8"))
            check(report["pid"] == state["pid"], "wrong process reported")
            check(report["creation_filetime"] == created_at,
                  "process creation identity changed in final report")
            check(report["executable_locked"] and
                  report["executable_sha256"] ==
                  hashlib.sha256(args.witness.read_bytes()).hexdigest() and
                  not report["retail_executable_verified"],
                  "private executable identity was not verified independently")
            check(report["system_config"] == (str(config) if config else None),
                  "private configuration argument was not preserved")
            check(report["source_saves_unchanged"], "source save was modified")
            check(report["locked_save_files"] == 1, "source file not locked")
            check(report["save_sha256_before"] == {"canary.bin": digest},
                  "SHA-256 differs from independent known vector")
            check(report["timed_out"] == (mode == "timeout"),
                  "wrong lifetime result")
            check(report["session_mode"] == ("play" if mode == "play" else "diagnostic"),
                  "wrong session mode")
            check(not report["window_text_collected"] and report["visible_windows"] == [],
                  "launcher collected window text in a diagnostic or play session")
            check("TDUR_WITNESS_PRIVATE_DIAGNOSTIC_TEXT" not in report_file.read_text("utf-8"),
                  "owned private window canary appeared in the report")
            if mode == "play":
                check(report["seconds"] >= 1.3 and not report["window_text_collected"]
                      and report["visible_windows"] == [],
                      "play session expired or collected private window text")
            expected_exit = (1460 if mode == "timeout" else
                             0xc0000005 if mode == "abnormal-code" else 37)
            check(report["exit_code"] == expected_exit,
                  "fixture exit or owned timeout status differs")
            exited = [wintypes.FILETIME() for _ in range(4)]
            observed_code = wintypes.DWORD()
            check(kernel.GetProcessTimes(observed_process, *(ctypes.byref(v) for v in exited)) and
                  kernel.GetExitCodeProcess(observed_process, ctypes.byref(observed_code)),
                  "independent owned process exit query failed")
            exit_stamp = (exited[1].dwHighDateTime << 32) | exited[1].dwLowDateTime
            check(exit_stamp > 0 and report["process_exit_filetime"] == str(exit_stamp) and
                  observed_code.value == expected_exit,
                  "exit timestamp/code differs from independent still-open process handle")
            notifications = report["job_notifications"]
            check(notifications["method"] == "completion-port" and
                  notifications["delivery_guaranteed"] is False and
                  notifications["read_error"] == 0 and notifications["dropped"] == 0,
                  "job observation contract or error state differs")
            events = notifications["events"]
            observed_job_events[mode] = [event["kind"] for event in events]
            # Windows may omit any of these job messages, including EXIT_PROCESS
            # after an owned timeout. Liveness, exit code and exit FILETIME above
            # are checked through an independently retained process handle.
            exit_events = [e for e in events if e["kind"] in ("exit_process", "abnormal_exit_process")]
            ticks = [e["observed_tick_ms"] for e in events]
            check(ticks == sorted(ticks), "job dequeue timestamps are not monotonic")
            for event in events:
                check(event["pid"] in (0, state["pid"]) and
                      isinstance(event["observed_utc_filetime"], str) and
                      int(event["observed_utc_filetime"]) > 0 and
                      event["query_error"] == 0,
                      "foreign process or invalid job observation metadata")
                if event["exit_code"] is not None:
                    check(event in exit_events and event["pid"] == state["pid"] and
                          event["exit_code"] == expected_exit,
                          "notification exit code does not match the owned process")
                if event["kind"] == "abnormal_exit_process":
                    check(mode == "abnormal-code",
                          "normal fixture reported as an abnormal exit")
            check(canary.read_bytes() == b"abc", "canary bytes changed")
            check(early_marker.read_bytes() == b"pre-main\n",
                  "injected dynamic folder hook did not run before main")
            log = (private / "observer.jsonl").read_text(encoding="utf-8")
            check("hooks-installed-pre-entry" in log and
                  "folder-a-redirected" in log,
                  "missing independently injected hook evidence")
        finally:
            if observed_process is not None:
                kernel.CloseHandle(observed_process)
            if process.poll() is None:
                # Only the launcher started above. Its job closes with it,
                # killing the exact owned fixture even on a failed assertion.
                process.kill()
                process.wait(timeout=5)
    print("Observed supplementary job messages: " +
          json.dumps(observed_job_events, sort_keys=True))
    print("PASS launcher: suspended registration, save lock, known SHA-256, "
          "normal/abnormal exit code and precise exit time, job notifications, "
          "owned timeout, quoted private INI and private play session")


if __name__ == "__main__":
    main()
