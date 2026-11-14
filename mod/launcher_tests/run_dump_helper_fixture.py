"""The dump helper alone, on witnesses started by this fixture.

Modes:
  self-check   TH3: dbghelp from the system folder, IGNORE_CVREC, no symbol.
  refuses      TH11: invented handles, wrong identity, import table.
  dump         a hang dump of an idle witness, read back without dbghelp,
               then TH14: the witness PDB opens exclusively (no lock left).
  suspension   review D2: a test variant of the helper stays inside the
               dump; the witness sentinel must stop, then the helper is
               stopped and the measurement says whether the sentinel resumes.
Every process touched is a child of this fixture.
"""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

KERNEL = ctypes.WinDLL("kernel32", use_last_error=True)
KERNEL.CreateFileW.argtypes = (wintypes.LPCWSTR, wintypes.DWORD,
                               wintypes.DWORD, ctypes.c_void_p,
                               wintypes.DWORD, wintypes.DWORD,
                               wintypes.HANDLE)
KERNEL.CreateFileW.restype = wintypes.HANDLE
KERNEL.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
KERNEL.OpenProcess.restype = wintypes.HANDLE
KERNEL.GetProcessTimes.argtypes = (wintypes.HANDLE,) + (
    ctypes.POINTER(wintypes.FILETIME),) * 4
KERNEL.GetProcessTimes.restype = wintypes.BOOL
KERNEL.CloseHandle.argtypes = (wintypes.HANDLE,)
KERNEL.CloseHandle.restype = wintypes.BOOL
INVALID = wintypes.HANDLE(-1).value

GENERIC_READ, GENERIC_WRITE = 0x80000000, 0x40000000
CREATE_NEW = 1
OPEN_EXISTING = 3
QUERY_INFORMATION, VM_READ, SYNCHRONIZE = 0x400, 0x10, 0x100000


class SecurityAttributes(ctypes.Structure):
    _fields_ = [("nLength", wintypes.DWORD),
                ("lpSecurityDescriptor", ctypes.c_void_p),
                ("bInheritHandle", wintypes.BOOL)]


def check(value, message):
    if not value:
        raise AssertionError(message)


def inheritable_file(path):
    attributes = SecurityAttributes(ctypes.sizeof(SecurityAttributes), None,
                                    True)
    handle = KERNEL.CreateFileW(str(path), GENERIC_READ | GENERIC_WRITE, 1,
                                ctypes.byref(attributes), CREATE_NEW, 0x80,
                                None)
    if handle in (None, INVALID):
        raise ctypes.WinError(ctypes.get_last_error())
    return handle


def child_handle(pid):
    """Query and read only, like the launcher's duplicated handle."""
    handle = KERNEL.OpenProcess(QUERY_INFORMATION | VM_READ | SYNCHRONIZE,
                                True, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    times = [wintypes.FILETIME() for _ in range(4)]
    check(KERNEL.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)),
          "creation time unreadable")
    return handle, (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime


def start_witness(witness, mode, directory, loader_dll=None):
    env = dict(os.environ, TDUR_CRASH_WITNESS_MODE=mode,
               TDUR_CRASH_WITNESS_DIR=str(directory))
    if loader_dll is not None:
        env["TDUR_CRASH_LOADER_HOLD_DLL"] = str(loader_dll)
    env.pop("TDUR_PRIVATE_DOCUMENTS", None)
    process = subprocess.Popen([str(witness)], env=env)
    until = time.monotonic() + 15
    while not (directory / "crash-witness.json").exists():
        check(process.poll() is None, "witness stopped early")
        check(time.monotonic() < until, "witness never wrote its facts")
        time.sleep(0.05)
    return process


def heartbeat(directory):
    data = (directory / "crash-heartbeat.bin").read_bytes()
    return struct.unpack("<I", data[:4])[0] if len(data) >= 4 else 0


def run_helper(helper, process, creation, pid, dump, summary, kind="hang",
               extra=(), wait=True, timeout=30):
    handles = [process, inheritable_file(dump), inheritable_file(summary)]
    command = [str(helper), "--kind", kind, "--process", hex(handles[0]),
               "--dump", hex(handles[1]), "--summary", hex(handles[2]),
               "--pid", str(pid), "--creation-filetime", str(creation)]
    command += list(extra)
    info = subprocess.STARTUPINFO()
    info.lpAttributeList = {"handle_list": handles}
    started = subprocess.Popen(command, startupinfo=info, close_fds=True,
                               cwd=str(dump.parent),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    for handle in handles[1:]:
        KERNEL.CloseHandle(handle)
    if not wait:
        return started
    started.communicate(timeout=timeout)
    return started.returncode


def self_check(args):
    result = subprocess.run([str(args.helper), "--self-check"],
                            capture_output=True, text=True, timeout=10)
    check(result.returncode == 0, "self-check code %d" % result.returncode)
    data = json.loads(result.stdout)
    folder = Path(os.environ["SystemRoot"])
    allowed = {str(folder / "SysWOW64").lower(),
               str(folder / "System32").lower()}
    check(data["self_check"] and str(Path(data["dbghelp"]).parent).lower()
          in allowed, "dbghelp not from the system folder: %s" % data)
    check(data["version"] and data["symopt_ignore_cvrec"] and
          data["symbols_loaded"] is False, "self-check fields: %s" % data)
    print("PASS helper self-check: " + json.dumps(data))


def imports(path):
    """Imported DLL and function names of a PE32 file."""
    data = Path(path).read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    sections = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24
    check(struct.unpack_from("<H", data, table)[0] == 0x10B, "not PE32")
    rva = struct.unpack_from("<I", data, table + 96 + 8)[0]
    heads = [struct.unpack_from("<IIIII", data, pe + 24 + optional + 40 * i
                                + 8) for i in range(sections)]

    def offset(value):
        for size, address, raw_size, raw, _ in heads:
            if address <= value < address + max(size, raw_size):
                return value - address + raw
        raise AssertionError("rva outside sections")

    def text(value):
        start = offset(value)
        return data[start:data.index(b"\0", start)].decode("ascii")

    result = {}
    cursor = offset(rva)
    while True:
        lookup, _, _, name, first = struct.unpack_from("<IIIII", data, cursor)
        if name == 0:
            break
        functions = []
        entry = offset(lookup or first)
        while True:
            (value,) = struct.unpack_from("<I", data, entry)
            if value == 0:
                break
            if not value & 0x80000000:
                functions.append(text(value + 2))
            entry += 4
        result[text(name).lower()] = functions
        cursor += 20
    return result


def refuses(args):
    work = args.work_root / ("refuses-%d" % time.time_ns())
    work.mkdir(parents=True)
    # (a) Invented handle values: refused before anything is written.
    before = set(work.iterdir())
    code = subprocess.run(
        [str(args.helper), "--kind", "hang", "--process", "0x1234",
         "--dump", "0x5678", "--summary", "0x9abc", "--pid", "4",
         "--creation-filetime", "1"], cwd=str(work), timeout=10).returncode
    check(code in (10, 11), "invented handles gave code %d" % code)
    check(set(work.iterdir()) == before, "helper created a file")
    # Arguments: unknown, repeated, crash without thread, hang with thread.
    for bad in (["--self-check", "x"], ["--kind", "hang", "--kind", "hang"],
                ["--kind", "crash", "--process", "1", "--dump", "2",
                 "--summary", "3", "--pid", "4", "--creation-filetime", "5"],
                ["--kind", "hang", "--process", "1", "--dump", "2",
                 "--summary", "3", "--pid", "4", "--creation-filetime", "5",
                 "--thread", "6", "--exception-pointers", "0x7"],
                ["--unknown", "1"]):
        code = subprocess.run([str(args.helper)] + bad, timeout=10).returncode
        check(code == 10, "bad arguments %s gave %d" % (bad, code))
    # (b) Valid handles, wrong creation time: code 11, files left empty.
    witness = start_witness(args.witness, "idle", work)
    try:
        process, creation = child_handle(witness.pid)
        code = run_helper(args.helper, process, creation + 1, witness.pid,
                          work / "wrong.dmp", work / "wrong.json")
        check(code == 11, "wrong identity gave code %d" % code)
        check((work / "wrong.dmp").stat().st_size == 0 and
              (work / "wrong.json").stat().st_size == 0,
              "helper wrote despite a wrong identity")
        code = run_helper(args.helper, process, creation, witness.pid + 4,
                          work / "pid.dmp", work / "pid.json")
        check(code == 11, "wrong PID gave code %d" % code)
        KERNEL.CloseHandle(process)
    finally:
        witness.kill()
        witness.wait(timeout=10)
    # (c) Import table: no dbghelp, no process opening, no file creation by
    # path, no registry, no process creation. TerminateProcess is imported
    # by the C runtime startup for its own process (security failure path);
    # the helper's handle on the target has no terminate right.
    table = imports(args.helper)
    check("dbghelp.dll" not in table, "dbghelp statically imported")
    check(not any(name.startswith("advapi32") for name in table),
          "registry library imported")
    names = {name for functions in table.values() for name in functions}
    for forbidden in ("OpenProcess", "CreateFileW", "CreateFileA",
                      "CreateProcessW", "CreateProcessA", "OpenThread",
                      "WriteProcessMemory", "DuplicateHandle"):
        check(forbidden not in names, forbidden + " imported")
    raw = Path(args.helper).read_bytes()
    for text in (b"SymInitialize", b"SymLoadModule", b"RegSetValue"):
        check(text not in raw, text.decode() + " present in the helper")
    print("PASS helper refusals: invented handles, bad arguments, wrong "
          "identity, imports " + json.dumps(sorted(table)))


def dump(args):
    sys.path.insert(0, str(args.reader.parent))
    import minidump_summary
    work = args.work_root / ("dump-%d" % time.time_ns())
    work.mkdir(parents=True)
    mode = "hang-loader" if args.loader_dll else "idle"
    witness = start_witness(args.witness, mode, work, args.loader_dll)
    try:
        process, creation = child_handle(witness.pid)
        started = time.monotonic()
        code = run_helper(args.helper, process, creation, witness.pid,
                          work / "tdur-hang-1.dmp", work / "tdur-hang-1.json")
        elapsed = time.monotonic() - started
        KERNEL.CloseHandle(process)
        check(code == 0, "helper code %d" % code)
        check(witness.poll() is None, "witness died during the dump")
    finally:
        witness.kill()
        witness.wait(timeout=10)
    facts = json.loads((work / "crash-witness.json").read_text("utf-8"))
    summary = json.loads((work / "tdur-hang-1.json").read_text("utf-8"))
    data = (work / "tdur-hang-1.dmp").read_bytes()
    parsed = minidump_summary.parse(data)
    check(summary["written"] and summary["bytes"] == len(data),
          "summary size differs from the dump")
    check(summary["symbols_loaded"] is False and
          summary["symopt_ignore_cvrec"], "symbol options")
    check(parsed["architecture"] == 0, "not an x86 dump")
    tids = {thread["thread_id"] for thread in parsed["threads"]}
    check(facts["main_tid"] in tids and len(tids) >= 2,
          "threads missing: %s" % sorted(tids))
    modules = [Path(m["name"]).name.lower() for m in parsed["modules"]]
    check(Path(args.witness).name.lower() in modules and
          "ntdll.dll" in modules, "modules missing: %s" % modules)
    check(parsed["memory_info"], "no memory region map")
    whys = {item["why"] for item in summary["added_ranges"]}
    check({"nt-wait-multiple-stub", "loader-lock"} <= whys,
          "added ranges: %s" % sorted(whys))
    check(summary["wow64_transition"]["target_region"] in
          ("image", "private", "mapped"), "wow64 transition region")
    if args.loader_dll:
        # TH9: PEB32 + 0xA0 and OwningThread + 0x0C name the holder.
        check(summary["loader_lock_owner_tid"] == facts["worker_tid"],
              "loader lock owner %s, holder %s" % (
                  summary["loader_lock_owner_tid"], facts["worker_tid"]))
    # TH14: no PDB left locked by dbghelp.
    pdb = Path(args.witness).with_suffix(".pdb")
    if pdb.exists():
        handle = KERNEL.CreateFileW(str(pdb), GENERIC_READ, 0, None,
                                    OPEN_EXISTING, 0x80, None)
        check(handle not in (None, INVALID), "witness PDB still locked")
        KERNEL.CloseHandle(handle)
    print("PASS helper dump: %d bytes in %.2f s, %d threads, %d modules, "
          "loader lock owner %s" % (len(data), elapsed, len(tids),
                                    len(modules),
                                    summary["loader_lock_owner_tid"]))


def suspension(args):
    work = args.work_root / ("suspension-%d" % time.time_ns())
    work.mkdir(parents=True)
    witness = start_witness(args.witness, "idle", work)
    try:
        process, creation = child_handle(witness.pid)
        first = heartbeat(work)
        time.sleep(0.5)
        check(heartbeat(work) > first, "sentinel not beating before")
        helper = run_helper(args.helper, process, creation, witness.pid,
                            work / "stall.dmp", work / "stall.json",
                            wait=False)
        time.sleep(3)
        during = [heartbeat(work)]
        time.sleep(1)
        during.append(heartbeat(work))
        helper.kill()  # This fixture's own child, mid-dump.
        helper.wait(timeout=10)
        time.sleep(1)
        after = [heartbeat(work)]
        time.sleep(1)
        after.append(heartbeat(work))
        KERNEL.CloseHandle(process)
    finally:
        witness.kill()
        witness.wait(timeout=10)
    result = {"suspended_during_dump": during[0] == during[1],
              "resumed_after_helper_stopped": after[1] > after[0],
              "during": during, "after": after}
    print("MEASURE " + json.dumps(result))
    check(result["suspended_during_dump"],
          "witness kept running inside the dump: measurement invalid")
    expected = args.expect_resumed == "yes"
    check(result["resumed_after_helper_stopped"] == expected,
          "suspension after a stopped helper differs from the recorded "
          "measurement")
    print("PASS helper suspension measurement")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("self-check", "refuses", "dump",
                                         "suspension"))
    parser.add_argument("--helper", type=Path, required=True)
    parser.add_argument("--witness", type=Path)
    parser.add_argument("--reader", type=Path)
    parser.add_argument("--work-root", type=Path)
    parser.add_argument("--expect-resumed", choices=("yes", "no"))
    parser.add_argument("--loader-dll", type=Path)
    args = parser.parse_args()
    {"self-check": self_check, "refuses": refuses, "dump": dump,
     "suspension": suspension}[args.mode](args)


if __name__ == "__main__":
    main()
