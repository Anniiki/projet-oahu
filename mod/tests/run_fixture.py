"""Run only the source-owned x86 observer witness; never a commercial game.

Coop-anything workflow applied: private copies, process identity, bounded job,
pre-start save locks, checksums before/after, and a guard mutation twin.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import uuid

K = C.WinDLL("kernel32", use_last_error=True)
INVALID = C.c_void_p(-1).value


class Startup(C.Structure):
    _fields_ = [("cb", W.DWORD), ("reserved", W.LPWSTR), ("desktop", W.LPWSTR),
                ("title", W.LPWSTR), ("x", W.DWORD), ("y", W.DWORD),
                ("width", W.DWORD), ("height", W.DWORD), ("chars_x", W.DWORD),
                ("chars_y", W.DWORD), ("fill", W.DWORD), ("flags", W.DWORD),
                ("show", W.WORD), ("reserved_bytes", W.WORD),
                ("reserved_data", C.c_void_p), ("input", W.HANDLE),
                ("output", W.HANDLE), ("error", W.HANDLE)]


class Process(C.Structure):
    _fields_ = [("process", W.HANDLE), ("thread", W.HANDLE),
                ("pid", W.DWORD), ("tid", W.DWORD)]


class BasicLimits(C.Structure):
    _fields_ = [("process_time", C.c_int64), ("job_time", C.c_int64),
                ("flags", W.DWORD), ("min_working", C.c_size_t),
                ("max_working", C.c_size_t), ("active", W.DWORD),
                ("affinity", C.c_size_t), ("priority", W.DWORD), ("scheduling", W.DWORD)]


class IoCounters(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]


class ExtendedLimits(C.Structure):
    _fields_ = [("basic", BasicLimits), ("io", IoCounters),
                ("process_memory", C.c_size_t), ("job_memory", C.c_size_t),
                ("peak_process", C.c_size_t), ("peak_job", C.c_size_t)]


K.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
K.CreateFileW.restype = W.HANDLE
K.CloseHandle.argtypes = [W.HANDLE]
K.CloseHandle.restype = W.BOOL
K.CreateJobObjectW.argtypes = [C.c_void_p, W.LPCWSTR]
K.CreateJobObjectW.restype = W.HANDLE
K.SetInformationJobObject.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD]
K.SetInformationJobObject.restype = W.BOOL
K.AssignProcessToJobObject.argtypes = [W.HANDLE, W.HANDLE]
K.AssignProcessToJobObject.restype = W.BOOL
K.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p,
                            W.BOOL, W.DWORD, C.c_void_p, W.LPCWSTR,
                            C.POINTER(Startup), C.POINTER(Process)]
K.CreateProcessW.restype = W.BOOL
K.ResumeThread.argtypes = [W.HANDLE]
K.ResumeThread.restype = W.DWORD
K.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
K.WaitForSingleObject.restype = W.DWORD
K.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
K.GetExitCodeProcess.restype = W.BOOL
K.TerminateJobObject.argtypes = [W.HANDLE, W.UINT]
K.TerminateJobObject.restype = W.BOOL
K.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
K.TerminateProcess.restype = W.BOOL


def need(value: object, message: str) -> None:
    if not value:
        raise RuntimeError(f"{message}; win32={C.get_last_error()}")


def sha(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def exports(path: Path) -> tuple[int, list[str]]:
    data = path.read_bytes()
    pe, = struct.unpack_from("<I", data, 0x3C)
    need(data[pe:pe + 4] == b"PE\0\0", "Invalid proxy PE")
    machine, sections = struct.unpack_from("<HH", data, pe + 4)
    optional_size, = struct.unpack_from("<H", data, pe + 20)
    optional = pe + 24
    need(struct.unpack_from("<H", data, optional)[0] == 0x10B, "Proxy must be PE32")
    def offset(rva: int) -> int:
        for index in range(sections):
            s = optional + optional_size + index * 40
            size, address, raw_size, raw = struct.unpack_from("<IIII", data, s + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise RuntimeError("Unmapped PE RVA")
    export_rva, = struct.unpack_from("<I", data, optional + 96)
    directory = offset(export_rva)
    count, = struct.unpack_from("<I", data, directory + 24)
    names_rva, = struct.unpack_from("<I", data, directory + 32)
    names = []
    for index in range(count):
        rva, = struct.unpack_from("<I", data, offset(names_rva) + index * 4)
        start = offset(rva)
        names.append(data[start:data.index(b"\0", start)].decode("ascii"))
    return machine, sorted(names)


E_ACCESSDENIED = 0x80070005
E_POINTER = 0x80004003


def check_forms(text: str, private: Path, source: Path, witness_dir: Path) -> None:
    """Traces and files: D1 NT name forms, D2 trailing separators, D3
    Documents decisions. Metadata only; the witness uses TDUR_WITNESS_* names."""
    lines = text.splitlines()

    def traced(event: str, reason: str, *needles: str) -> bool:
        return any(f"event={event} " in line and f"reason={reason} " in line and
                   all(needle in line for needle in needles) for line in lines)

    for api in ("folder-a", "folder-w", "known-folder"):
        for reason in ("null-token", "current-user-token"):
            need(traced(f"{api}-redirected", reason, "error=0 "),
                 f"Missing {api}-redirected reason={reason}")
        for reason, error in (("default-user-token", E_ACCESSDENIED),
                              ("token-not-queryable", E_ACCESSDENIED),
                              ("null-output", E_POINTER)):
            need(traced(f"{api}-refused", reason, f"error={error} "),
                 f"Missing {api}-refused reason={reason}")
    for line in lines:
        if " event=folder-" in line or " event=known-folder-" in line:
            need(line.endswith(" path="), "A Documents decision traced a path")
    need("S-1-" not in text, "A SID reached the observation log")
    for reason, error, needle in (
            ("dos-device-namespace", 5, "\\??\\TDUR_WITNESS_ABSENT_DEVICE"),
            ("dos-device-namespace", 5, "\\??\\TDUR_WITNESS_COLLISION"),
            ("dos-device-namespace", 5, "\\??\\Global\\TDUR_WITNESS_ABSENT_DEVICE"),
            ("dos-device-namespace", 5, "\\??\\pipe\\TDUR_WITNESS_ABSENT_PIPE"),
            ("relative-name-without-root", 161, "TDUR_WITNESS_RELATIVE"),
            ("absolute-name-with-root", 87, "TDUR_WITNESS_ROOTED"),
            ("dos-volume-device", 5, "path=\\??\\")):
        need(traced("nt-name-form-refused", reason, f"error={error} ", needle),
             f"Missing nt-name-form-refused reason={reason} {needle}")
    cwd = private / "TDUR_WITNESS_CWD"
    for leaf in ("TDUR_WITNESS_ABSENT_DEVICE", "TDUR_WITNESS_RELATIVE",
                 "TDUR_WITNESS_ROOTED", "Global", "pipe"):
        need(not (cwd / leaf).exists(), f"An NT name became a current-directory entry: {leaf}")
    need((cwd / "TDUR_WITNESS_COLLISION").is_file() and
         (cwd / "TDUR_WITNESS_COLLISION").stat().st_size == 0,
         "The current-directory file named like a device changed")
    need((cwd / "TDUR_WITNESS_DRIVE_FILE").is_file(), "A real drive path no longer opens")
    for leaf in ("TDUR_WITNESS_DIR_WIN32", "TDUR_WITNESS_DIR_NT"):
        need((private / leaf).is_dir(), f"Trailing-separator directory missing: {leaf}")
    need(not (private / "TDUR_WITNESS_ABSENT").exists(), "A missing parent was created")
    need(not (private / "TDUR_WITNESS_TARGET" / "TDUR_WITNESS_THROUGH").exists(),
         "A reparse point was traversed")
    need(not (source / "TDUR_WITNESS_SOURCE_DIR").exists(), "The source root accepted a directory")
    need(not any(witness_dir.glob("TDUR_WITNESS_*")),
         "A witness case wrote outside its temporary folders")


def check_document_callers(text: str, witness: Path) -> None:
    """Compare reported call sites with CALL instructions in the owned PE."""
    image = witness.read_bytes()
    pe = struct.unpack_from("<I", image, 0x3c)[0]
    need(image[pe:pe + 4] == b"PE\0\0", "Invalid owned witness image")
    sections, = struct.unpack_from("<H", image, pe + 6)
    optional_size, = struct.unpack_from("<H", image, pe + 20)
    section_table = pe + 24 + optional_size
    executable = []
    for index in range(sections):
        offset = section_table + index * 40
        size, address, raw_size, raw_offset = struct.unpack_from("<IIII", image, offset + 8)
        flags, = struct.unpack_from("<I", image, offset + 36)
        if flags & 0x20000000:
            executable.append((address, min(size, raw_size), raw_offset))
    seen, sites = set(), set()
    for line in text.splitlines():
        fields = dict(part.split("=", 1) for part in line.split() if "=" in part)
        event = fields.get("event", "")
        if event == "observation-summary":
            need("caller_rva" not in fields, "A summary attributed several callers to one call site")
        if not event.startswith(("folder-a-", "folder-w-", "known-folder-")):
            continue
        need("caller_image" in fields and "caller_rva" in fields,
             "Missing Documents call-site metadata")
        if fields["caller_image"] != "main":
            need(fields["caller_image"] in ("other", "unresolved") and
                 fields["caller_rva"] == "none", "External caller address was exposed")
            continue
        rva = int(fields["caller_rva"], 16)
        matches = [(start, size, raw) for start, size, raw in executable
                   if start <= rva < start + size]
        need(len(matches) == 1, "Documents caller is outside owned executable code")
        start, _, raw = matches[0]
        position = raw + rva - start
        relative_call = rva - start >= 5 and image[position - 5] == 0xe8
        iat_call = rva - start >= 6 and image[position - 6:position - 4] == b"\xff\x15"
        register_call = rva - start >= 2 and image[position - 2] == 0xff and \
            image[position - 1] & 0xf8 == 0xd0
        need(relative_call or iat_call or register_call,
             f"Documents return site {rva:#x} does not follow a recognized fixture CALL")
        seen.add(event)
        sites.add(rva)
    need({"folder-a-redirected", "folder-w-redirected", "known-folder-redirected"} <= seen,
         "The three Documents APIs did not expose independent main-image call sites")
    need(len(sites) >= 3, "Documents callers collapsed to a single logger/helper location")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--witness", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    parser.add_argument("--expect-source-mutant", action="store_true")
    parser.add_argument("--cursor-diagnostics", action="store_true",
                        help="Verify cursor hook installation without cursor input")
    args = parser.parse_args()
    witness = args.witness.resolve(strict=True)
    need(witness.name == "tdur_mod_witness.exe", "Only the source-owned witness may run")
    proxy = witness.with_name("dinput8.dll")
    need(exports(proxy) == (0x14C, ["DirectInput8Create"]), "Unexpected proxy architecture/exports")
    manifest = json.loads(witness.with_name("tdur-observation-manifest.json").read_text())
    need(manifest["schema"] == "tdur-observation-v1" and
         manifest["witness_sha256"] == sha(witness) and manifest["proxy_sha256"] == sha(proxy),
         "Witness/proxy differ from the build manifest")
    root = args.work_root.resolve() / uuid.uuid4().hex
    root.mkdir(parents=True, exist_ok=True)
    source = root / "source-documents"
    private = root / "private-documents"
    installation = root / "source-installation"
    source.mkdir(exist_ok=True)
    private.mkdir(exist_ok=True)
    installation.mkdir()
    resource = installation / "fixture-resource.bin"
    resource.write_bytes(b"TDU-Recompilation owned fixture resource\n")
    resource_digest = sha(resource)
    canaries = [source / f"save{i}.sav" for i in range(17)]
    for index, path in enumerate(canaries):
        path.write_bytes(f"TDU-Recompilation owned fixture canary {index}\n".encode())
    before = {path.name: sha(path) for path in canaries}
    forbidden = source / "forbidden-new.sav"
    if forbidden.exists():
        forbidden.unlink()  # Exact source-owned fixture file, never a game save.
    log = private / "observation.log"
    log.write_bytes(b"")
    locks = []
    job = None
    process = Process()
    try:
        for path in canaries:
            handle = K.CreateFileW(str(path), 0x80000000, 1, None, 3, 0x80, None)
            need(handle not in (None, INVALID), "Cannot lock source canary read-only")
            locks.append(handle)
        probe = K.CreateFileW(str(canaries[0]), 0x40000000, 1, None, 3, 0x80, None)
        need(probe == INVALID and C.get_last_error() == 32, "Source share lock did not deny write")
        job_name = "Local\\TDUR-Observation-Fixture-" + uuid.uuid4().hex
        job = K.CreateJobObjectW(None, job_name)
        need(job, "CreateJobObject")
        limits = ExtendedLimits()
        limits.basic.flags = 0x2000 | 8  # KILL_ON_JOB_CLOSE + ACTIVE_PROCESS; no breakaway.
        limits.basic.active = 1
        need(K.SetInformationJobObject(job, 9, C.byref(limits), C.sizeof(limits)), "Set job limits")
        environment = dict(os.environ)
        environment.pop("TDUR_CURSOR_DIAGNOSTICS", None)
        if args.cursor_diagnostics:
            environment["TDUR_CURSOR_DIAGNOSTICS"] = "1"
        environment.update(TDUR_PRIVATE_DOCUMENTS=str(private), TDUR_SOURCE_DOCUMENTS=str(source),
                           TDUR_SOURCE_INSTALLATION=str(installation),
                           TDUR_OBSERVATION_LOG=str(log), TDUR_OBSERVATION_JOB=job_name)
        block = C.create_unicode_buffer("\0".join(f"{k}={v}" for k, v in sorted(environment.items(), key=lambda x: x[0].upper())) + "\0\0")
        startup = Startup()
        startup.cb = C.sizeof(startup)
        command = C.create_unicode_buffer(f'"{witness}"')
        need(K.CreateProcessW(str(witness), command, None, None, False,
                             0x4 | 0x400 | 0x08000000, block, str(witness.parent),
                             C.byref(startup), C.byref(process)), "Create suspended fixture")
        if not K.AssignProcessToJobObject(job, process.process):
            K.TerminateProcess(process.process, 2)
            raise RuntimeError("Cannot confine fixture in its job")
        need(K.ResumeThread(process.thread) != 0xFFFFFFFF, "Resume fixture")
        if K.WaitForSingleObject(process.process, 25000) != 0:
            K.TerminateJobObject(job, 2)
            raise RuntimeError("Fixture exceeded 25-second deadline")
        exit_code = W.DWORD()
        need(K.GetExitCodeProcess(process.process, C.byref(exit_code)), "Get fixture exit")
        after = {path.name: sha(path) for path in canaries}
        need(before == after, "Source canary contents changed")
        need(sha(resource) == resource_digest and
             not (installation / "forbidden-installation-new.bin").exists(),
             "Source installation fixture changed")
        text = log.read_text(encoding="utf-8")
        cursor_lines = [line for line in text.splitlines()
                        if " event=cursor-" in line]
        if args.cursor_diagnostics:
            installed = [line for line in cursor_lines
                         if " event=cursor-diagnostics-installed error=0 " in line]
            need(len(installed) == 1,
                 "Cursor Detours transaction was not installed exactly once")
            need(not any("event=cursor-diagnostics-install-failed " in line or
                         "event=cursor-diagnostics-invalid-configuration " in line
                         for line in cursor_lines),
                 "Enabled cursor diagnostics reported a startup failure")
        else:
            need(not cursor_lines, "Cursor diagnostics ran without an explicit request")
        need(log.stat().st_size < 256 * 1024, "Frequent observer events were not bounded")
        need("event=observation-summary " in text and
             "source_event=folder-a-redirected " in text,
             "The periodic frequent-event counter was not emitted")
        summaries = [line for line in text.splitlines()
                     if "event=observation-summary " in line and
                     "source_event=unconfined-write-refused " in line]
        need(summaries and "reason=object-name-empty" in text,
             "Missing generic empty-native-name refusal samples/summary")
        fields = dict(part.split("=", 1) for part in summaries[-1].split() if "=" in part)
        need(int(fields["count_total"]) >= 100001 and int(fields["empty_paths_total"]) >= 100001,
             "Empty native-name refusals were lost from the counters")
        new_source_file = forbidden.exists()
        if args.expect_source_mutant:
            need(exit_code.value != 0 and new_source_file, "Source-write mutation was not caught")
        else:
            need(exit_code.value == 0, f"Witness failed with exit code {exit_code.value:#x}; inspect {log}")
            need(not new_source_file, "New source file escaped the guard")
            for event in ("hooks-installed-pre-entry", "folder-a-redirected", "folder-w-redirected",
                          "known-folder-redirected", "DirectInput8Create-forwarded", "source-write-refused",
                          "shell-execute-refused", "child-process-refused", "source-installation-write-refused"):
                need(f"event={event} " in text, f"Missing observation event {event}")
            pids = {part.split("=", 1)[1] for line in text.splitlines() for part in line.split() if part.startswith("pid=")}
            need(pids == {str(process.pid)}, "Unexpected process in observation log")
            need(len(list(private.glob("global-*.txt-*"))) == 4,
                 "The four pre-main folder APIs did not write privately")
            check_forms(text, private, source, witness.parent)
            check_document_callers(text, witness)
        report = {"project": "TDU-Recompilation", "game_executed": False,
                  "architecture": "x86", "source_files_locked": len(locks),
                  "source_hashes_unchanged": before == after,
                  "source_new_file_created": new_source_file,
                  "source_installation_unchanged": True,
                  "pre_main_private_markers": len(list(private.glob("global-*.txt-*"))),
                  "child_process_policy": "refused; job active-process limit 1",
                  "observer_log_bytes": log.stat().st_size,
                  "folder_api_flood_calls": 100001,
                  "empty_native_name_calls": 100001,
                  "private_marker_files": len(list(private.glob("*.txt-*"))),
                  "name_form_cases_checked": not args.expect_source_mutant,
                  "child_exit_code": exit_code.value, "mutation_detected": args.expect_source_mutant,
                  "witness_sha256": sha(witness), "proxy_sha256": sha(proxy)}
        report["evidence_directory"] = str(root)
        (root / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report))
        return 0
    finally:
        if job:
            K.CloseHandle(job)
        if process.thread:
            K.CloseHandle(process.thread)
        if process.process:
            K.CloseHandle(process.process)
        for handle in locks:
            K.CloseHandle(handle)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"Observation fixture failed: {error}", file=sys.stderr)
        raise SystemExit(1)
