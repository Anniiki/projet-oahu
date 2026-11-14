"""Run the source-owned Shell-free deletion witness; never a game.

Prepares synthetic save trees under a private Documents folder created for
this run inside the build tree, runs tdur_shell_delete_witness.exe with the
observation DLL beside it, then checks the witness results, the observation
log lines and the files left on disk. Two more runs, each with its own
folders, check that the DLL refuses to load when the source Documents or the
source installation lies at or under the save root. New policy, see
docs/evidence/shell_delete_without_shell.md.
"""
from __future__ import annotations

import argparse
import ctypes as C
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import uuid

try:
    import _winapi
except ImportError:  # pragma: no cover - Windows only.
    _winapi = None

K = C.WinDLL("kernel32", use_last_error=True)
K.GetShortPathNameW.argtypes = [W.LPCWSTR, W.LPWSTR, W.DWORD]
K.GetShortPathNameW.restype = W.DWORD
K.GetFileAttributesW.argtypes = [W.LPCWSTR]
K.GetFileAttributesW.restype = W.DWORD
K.SetFileAttributesW.argtypes = [W.LPCWSTR, W.DWORD]
K.SetFileAttributesW.restype = W.BOOL
K.GetErrorMode.argtypes = []
K.GetErrorMode.restype = W.UINT
K.SetErrorMode.argtypes = [W.UINT]
K.SetErrorMode.restype = W.UINT

READONLY = 0x1
INVALID_ATTRIBUTES = 0xFFFFFFFF
GAME_FLAGS = "0x00000414"
# SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX:
# a refused DLL ends the witness with a status, never with a dialog.
QUIET_ERRORS = 0x8003
DLL_INIT_FAILED = 0xC0000142


def need(value: object, message: str) -> None:
    if not value:
        raise RuntimeError(message)


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


def attributes(path: Path) -> int:
    return K.GetFileAttributesW(str(path))


def write(path: Path, text: str, read_only: bool = False) -> None:
    path.write_bytes(text.encode("ascii"))
    if read_only:
        need(K.SetFileAttributesW(str(path), READONLY), f"Cannot mark {path} read-only")


def junction(link: Path, target: Path) -> None:
    if _winapi is not None and hasattr(_winapi, "CreateJunction"):
        _winapi.CreateJunction(str(target), str(link))
    else:
        subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(target)],
                       check=True, capture_output=True)
    need(attributes(link) != INVALID_ATTRIBUTES and attributes(link) & 0x400,
         f"Junction not created: {link}")


def observer_fields(line: str) -> dict[str, str] | None:
    """Fields of one observer line. The path is last and may hold spaces; the
    reason is its first token, the key=value tokens after it are fields."""
    if " event=" not in line or " path=" not in line:
        return None
    head, path = line.split(" path=", 1)
    fields = {"path": path}
    reason = ""
    if " reason=" in head:
        head, reason = head.split(" reason=", 1)
    for token in head.split() + reason.split()[1:]:
        key, separator, value = token.partition("=")
        if separator:
            fields.setdefault(key, value)
    fields["reason"] = reason.split()[0] if reason.split() else ""
    return fields


def tree(base: Path, leaf: str) -> tuple[Path, int, int]:
    """Profile folder with four files in three folders."""
    top = base / leaf
    (top / "sub" / "deeper").mkdir(parents=True)
    write(top / "a.sav", "a")
    write(top / "b.sav", "b")
    write(top / "sub" / "c.sav", "c")
    write(top / "sub" / "deeper" / "d.sav", "d")
    return top, 4, 3


def single(base: Path, leaf: str) -> tuple[Path, int, int]:
    top = base / leaf
    top.mkdir()
    write(top / "x.sav", leaf)
    return top, 1, 1


def refused_load(witness: Path, base: Path, name: str, documents_at_root: bool) -> None:
    """Runs the witness with the source Documents equal to the save root, or
    with the source installation under it: the engine works inside the
    guard's internal scope, so the DLL must refuse to load. Standard input is
    empty, so that a DLL loaded by mistake never reaches the forwarded cases
    (the real Shell)."""
    private = base / name / "p"
    root = private / "Test Drive Unlimited" / "savegame"
    root.mkdir(parents=True)
    source = base / name / "s"
    source.mkdir()
    outside = base / name / "o"
    outside.mkdir()
    keep = root / "Keep"
    keep.mkdir()
    write(keep / "keep.sav", "keep")
    log = private / "observation.log"
    log.write_bytes(b"")
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith("TDUR_")}
    environment.update(TDUR_PRIVATE_DOCUMENTS=str(private),
                       TDUR_OBSERVATION_LOG=str(log),
                       TDUR_SHELL_DELETE_OUTSIDE=str(outside),
                       TDUR_SHELL_DELETE_SHORT="")
    if documents_at_root:
        environment["TDUR_SOURCE_DOCUMENTS"] = str(root)
    else:
        installation = root / "Install"
        installation.mkdir()
        environment.update(TDUR_SOURCE_DOCUMENTS=str(source),
                           TDUR_SOURCE_INSTALLATION=str(installation))
    previous = K.SetErrorMode(K.GetErrorMode() | QUIET_ERRORS)
    try:
        result = subprocess.run([str(witness)], env=environment, cwd=str(witness.parent),
                                stdin=subprocess.DEVNULL, capture_output=True, text=True,
                                timeout=60, creationflags=0x08000000)
    finally:
        K.SetErrorMode(previous)
    code = result.returncode & 0xFFFFFFFF
    print(f"load-refusal {name}: exit=0x{code:08X}")
    lines = [fields for fields in map(observer_fields,
                                      log.read_text(encoding="utf-8").splitlines()) if fields]
    need(code == DLL_INIT_FAILED,
         f"{name}: the DLL did not refuse to load (exit 0x{code:08X}, output {result.stdout!r})")
    need("case=" not in result.stdout, f"{name}: the witness ran its cases")
    need(any(line.get("event") == "shell-delete-hooks-install-failed" and
             line.get("reason") == "save-root-refused" and line.get("error") != "0"
             for line in lines), f"{name}: no save-root-refused installation line")
    need(not any(line.get("event") == "shell-delete-hooks-installed" for line in lines),
         f"{name}: the Shell deletion hooks were installed")
    need((keep / "keep.sav").read_text(encoding="ascii") == "keep",
         f"{name}: a save folder changed")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--witness", type=Path, required=True)
    parser.add_argument("--work-root", type=Path, required=True)
    args = parser.parse_args()
    witness = args.witness.resolve(strict=True)
    need(witness.name == "tdur_shell_delete_witness.exe",
         "Only the source-owned witness may run")
    proxy = witness.with_name("dinput8.dll")
    need(exports(proxy) == (0x14C, ["DirectInput8Create"]),
         "Unexpected proxy architecture or exports")
    args.work_root.mkdir(parents=True, exist_ok=True)
    run = args.work_root.resolve() / uuid.uuid4().hex[:8]
    # Load refusals first, each in its own folders; the outcome of each is
    # printed before the main run, which a failure here does not stop.
    refusals = {}
    for name, documents_at_root in (("documents-at-root", True),
                                    ("installation-under-root", False)):
        try:
            refused_load(witness, run / "refusals", name, documents_at_root)
            refusals[name] = "refused"
        except (RuntimeError, subprocess.TimeoutExpired, OSError) as error:
            refusals[name] = str(error)
            print(f"load-refusal {name}: FAILED {error}")
    private = run / "p"
    source = run / "s"
    outside = run / "o"
    root = private / "Test Drive Unlimited" / "savegame"
    root.mkdir(parents=True)
    source.mkdir()
    outside.mkdir()
    log = private / "observation.log"
    log.write_bytes(b"")

    # Expected handled cases: case -> (path, files, dirs, api, apartment, result).
    expected: dict[str, tuple[Path, int, int, str, str, int]] = {}
    top, files, dirs = tree(root, "ProfileA")
    expected["a-profile"] = (top, files, dirs, "A", "none", 0)
    top, files, dirs = tree(root, "ProfileW")
    expected["w-profile"] = (top, files, dirs, "W", "none", 0)
    top, files, dirs = single(root, "ProfileRel")
    expected["relative"] = (top, files, dirs, "A", "none", 0)
    (root / "ProfileA2").mkdir()
    top, files, dirs = single(root, "ProfileDot")
    expected["dotdot"] = (top, files, dirs, "W", "none", 0)
    long_profile, files, dirs = single(root, "LongProfileNameForShort")
    buffer = C.create_unicode_buffer(1024)
    size = K.GetShortPathNameW(str(long_profile), buffer, 1024)
    short = buffer.value if 0 < size < 1024 else ""
    short_available = bool(short) and Path(short).name.lower() != long_profile.name.lower()
    if short_available:
        expected["short"] = (long_profile, files, dirs, "A", "none", 0)
    profile_j = root / "ProfileJ"
    profile_j.mkdir()
    write(profile_j / "j.sav", "j")
    target = outside / "JTarget"
    target.mkdir()
    witness_file = target / "witness.txt"
    write(witness_file, "outside witness", read_only=True)
    witness_attributes = attributes(witness_file)
    junction(profile_j / "link", target)
    expected["junction"] = (profile_j, 1, 2, "A", "none", 0)
    profile_ro = root / "ProfileRO"
    (profile_ro / "rosub").mkdir(parents=True)
    write(profile_ro / "r.sav", "r", read_only=True)
    write(profile_ro / "rosub" / "q.sav", "q", read_only=True)
    need(K.SetFileAttributesW(str(profile_ro / "rosub"), READONLY), "Read-only folder")
    expected["readonly"] = (profile_ro, 2, 2, "W", "none", 0)
    expected["missing"] = (root / "NoSuchProfile", 0, 0, "A", "none", 2)
    top, files, dirs = single(root, "ProfileSTA")
    expected["sta"] = (top, files, dirs, "W", "sta", 0)
    top, files, dirs = single(root, "ProfileMTA")
    expected["mta"] = (top, files, dirs, "A", "mta", 0)
    keep = root / "Keep"
    keep.mkdir()
    write(keep / "keep.sav", "keep")

    # Forwarded cases: case -> (cause, api, func, flags, paths in the line).
    forwarded = {
        "outside": ("outside-root", "A", "3", GAME_FLAGS, [outside / "NoSuchDir"]),
        "mixed": ("outside-root", "W", "3", GAME_FLAGS,
                  [root / "NoSuchA", outside / "NoSuchB"]),
        "wildcard": ("wildcard", "A", "3", GAME_FLAGS, [root / "TDURNOMATCH*.none"]),
        "allowundo": ("flags", "W", "3", "0x00000454", [root / "NoSuchUndo"]),
        "copy": ("not-delete", "A", "2", GAME_FLAGS, [root / "NoSuchCopy"]),
        "stream": ("stream", "W", "3", GAME_FLAGS, [root / "NoSuchStream.sav:flux"]),
        "root": ("outside-root", "W", "3", GAME_FLAGS, [root]),
    }

    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith("TDUR_")}
    environment.update(TDUR_PRIVATE_DOCUMENTS=str(private),
                       TDUR_SOURCE_DOCUMENTS=str(source),
                       TDUR_OBSERVATION_LOG=str(log),
                       TDUR_SHELL_DELETE_OUTSIDE=str(outside),
                       TDUR_SHELL_DELETE_SHORT=short if short_available else "")
    process = subprocess.Popen([str(witness)], env=environment, cwd=str(witness.parent),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True,
                               creationflags=0x08000000)
    try:
        # Phase 1: handled cases. Every call is bounded by the witness itself
        # (ten seconds), which then prints "handled-done" and waits for "go".
        output = ""
        for line in process.stdout:
            output += line
            if line.startswith("handled-done") or line.startswith("witness-end"):
                break
        print(output, end="")
        results = {}

        def parse(text: str) -> None:
            for row in text.splitlines():
                fields = dict(token.split("=", 1) for token in row.split() if "=" in token)
                if "case" in fields:
                    results[fields["case"]] = fields

        parse(output)
        need("handled-done" in output, f"The witness stopped early: {output!r}")
        if not short_available:
            need(results.get("short", {}).get("skipped") == "no-short-names",
                 "The short-name case was neither run nor reported as skipped")
            print("short 8.3 names unavailable on this volume; case skipped")
        for case, (path, files, dirs, api, apartment, result) in expected.items():
            outcome = results.get(case, {})
            need(outcome.get("returned") == "1" and outcome.get("result") == str(result) and
                 outcome.get("aborted") == "0",
                 f"{case}: witness result {outcome}, expected {result} and aborted 0")
            need(not path.exists() and not os.path.lexists(path), f"{case}: {path} remains")
        need((root / "ProfileA2").is_dir(), "The '..' path deleted its first component")
        need(witness_file.read_text(encoding="ascii") == "outside witness" and
             attributes(witness_file) == witness_attributes and target.is_dir(),
             "The junction target or its attributes changed")
        need((keep / "keep.sav").read_text(encoding="ascii") == "keep",
             "A save folder outside every list changed")
        need(root.is_dir(), "The save root was deleted")
        # Phase 2: forwarded cases, which reach the real Shell.
        try:
            process.stdin.write("go\n")
            process.stdin.flush()
        except OSError:
            pass
        rest, errors = process.communicate(timeout=90)
        print(rest, end="")
        output += rest
        parse(rest)
        need("witness-end hung=" in rest, f"The witness did not finish: {errors!r}")
        text = log.read_text(encoding="utf-8")
        lines = [fields for fields in map(observer_fields, text.splitlines()) if fields]
        need(any(line.get("event") == "hooks-installed-pre-entry" for line in lines),
             "The observation DLL did not load")
        need(any(line.get("event") == "shell-delete-hooks-installed" and line.get("error") == "0"
                 for line in lines), "The Shell deletion hooks were not installed")
        handled_lines = [line for line in lines if line.get("event") == "shell-delete-handled"]
        forwarded_lines = [line for line in lines if line.get("event") == "shell-delete-forwarded"]
        returned_lines = [line for line in lines
                          if line.get("event") == "shell-delete-forwarded-returned"]
        for case, (path, files, dirs, api, apartment, result) in expected.items():
            found = [line for line in handled_lines if line["path"].lower() == str(path).lower()]
            need(len(found) == 1, f"{case}: {len(found)} handled lines for {path}")
            line = found[0]
            want = {"error": str(result), "reason": "handled", "api": api,
                    "files": str(files), "dirs": str(dirs), "flags": GAME_FLAGS,
                    "apartment": apartment, "paths": "1", "caller_image": "main"}
            for key, value in want.items():
                need(line.get(key) == value,
                     f"{case}: {key}={line.get(key)}, expected {value}")
            need(line.get("ms", "").isdigit(), f"{case}: duration missing")
        need(len(handled_lines) == len(expected),
             f"{len(handled_lines)} handled lines, expected {len(expected)}")
        for case, (cause, api, function, flags, paths) in forwarded.items():
            outcome = results.get(case, {})
            want_path = "|".join(str(path) for path in paths).lower()
            found = [line for line in forwarded_lines if line.get("reason") == cause and
                     line.get("api") == api and line.get("func") == function and
                     line.get("flags") == flags and line["path"].lower() == want_path]
            need(len(found) == 1, f"{case}: {len(found)} forwarded lines ({cause})")
            if outcome.get("returned") == "1":
                back = [line for line in returned_lines if line.get("reason") == cause and
                        line["path"].lower() == want_path]
                need(len(back) == 1 and back[0].get("error") == outcome.get("result") and
                     back[0].get("ms", "").isdigit(),
                     f"{case}: returned line missing or different from {outcome}")
            else:
                print(f"{case}: the original function did not return within 10 s")
        need(len(forwarded_lines) == len(forwarded),
             f"{len(forwarded_lines)} forwarded lines, expected {len(forwarded)}")
        need(all(outcome == "refused" for outcome in refusals.values()),
             f"load refusals: {refusals}")
        report = {"project": "TDU-Recompilation", "game_executed": False,
                  "handled_cases": sorted(expected), "forwarded_cases": sorted(forwarded),
                  "load_refusals": sorted(refusals),
                  "short_names": short_available,
                  "forwarded_returned": sorted(case for case in forwarded
                                               if results.get(case, {}).get("returned") == "1"),
                  "evidence_directory": str(run)}
        (run / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report))
        return 0
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)
        # A junction left by a failed case is removed as a link, never followed.
        link = profile_j / "link"
        if os.path.lexists(link):
            os.rmdir(link)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"Shell deletion fixture failed: {error}", file=sys.stderr)
        raise SystemExit(1)
