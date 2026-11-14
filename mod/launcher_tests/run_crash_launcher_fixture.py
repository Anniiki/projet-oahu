"""Crash and hang dumps through the real launcher and observer DLL.

One scenario per call, on tdur_crash_witness (never named like the game).
Every process started here is a child of this fixture or of the launcher it
started; the decoy of TH13 is stopped by this fixture at the end.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

SCENARIOS = ("crash-exec-private", "crash-write-reserved",
             "crash-stack-overflow", "crash-handled", "uef-image-call",
             "hang-ui-stop", "hang-ui-ask-yes", "hang-ui-ask-no",
             "hang-cross-thread", "hang-loader", "busy-pumping", "stutter",
             "foreign", "helper-gone", "options")


def check(value, message):
    if not value:
        raise AssertionError(message)


class Session:
    def __init__(self, args, name):
        self.args = args
        self.root = args.work_root / ("%s-%d" % (name, time.time_ns()))
        self.source = self.root / "source"
        self.saves = self.source / "saves"
        self.saves.mkdir(parents=True)
        (self.saves / "canary.bin").write_bytes(b"abc")
        self.session = self.root / "session"
        self.documents = self.session / "documents"
        self.documents.mkdir(parents=True)
        self.report_file = self.session / "startup-report.json"
        self.launcher = args.launcher

    def command(self, seconds, extra=(), report=None):
        return [str(self.launcher), "--exe", str(self.args.witness.resolve()),
                "--dll", str(self.args.dll.resolve()),
                "--source-documents", str(self.source.resolve()),
                "--private-documents", str(self.documents.resolve()),
                "--source-saves", str(self.saves.resolve()),
                "--log", str((self.documents / "observer.log").resolve()),
                "--report", str((report or self.report_file).resolve()),
                "--pid-file", str((self.session / "pid.json").resolve()),
                "--resume-file", str((self.session / "resume").resolve()),
                "--max-seconds", str(seconds)] + list(extra)

    def run(self, mode, seconds, extra=(), env_extra=None, after_pid=None,
            timeout=None):
        env = dict(os.environ, TDUR_CRASH_WITNESS_MODE=mode,
                   TDUR_CRASH_WITNESS_DUMP=str(
                       self.session / "tdur-crash-1.dmp"),
                   TDUR_CRASH_LOADER_HOLD_DLL=str(
                       self.args.loader_dll.resolve()))
        env.update(env_extra or {})
        process = subprocess.Popen(self.command(seconds, extra), env=env,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            pid_file = self.session / "pid.json"
            until = time.monotonic() + 20
            while not pid_file.exists():
                if process.poll() is not None:
                    raise AssertionError("launcher stopped before the PID: " +
                                         str(process.communicate()))
                check(time.monotonic() < until, "no PID publication")
                time.sleep(0.025)
            if after_pid is not None:
                after_pid()
            (self.session / "resume").write_text("fixture\n", "ascii")
            self.stdout, self.stderr = process.communicate(
                timeout=timeout or seconds + 60)
        finally:
            if process.poll() is None:
                process.kill()  # Closes the job: the owned witness goes too.
                process.wait(timeout=10)
        self.code = process.returncode
        check(self.code == 0, "launcher code %d: %s %s" % (
            self.code, self.stdout, self.stderr))
        self.report = json.loads(self.report_file.read_text("utf-8"))
        facts = self.documents / "crash-witness.json"
        self.facts = json.loads(facts.read_text("utf-8")) \
            if facts.exists() else {}
        check(self.report["source_saves_unchanged"], "source save changed")
        check((self.saves / "canary.bin").read_bytes() == b"abc",
              "canary changed")
        check(self.report["window_text_collected"] is False and
              self.report["window_messages_sent"] is False and
              self.report["visible_windows"] == [], "window contract")
        check(self.report["crash_channel"]["created"] and
              self.report["crash_channel"]["mod_armed"], "channel not armed")
        log = (self.documents / "observer.log").read_text("utf-8")
        check("crash-handler-armed" in log, "no crash-handler-armed record")
        check("game-ended reason=" + self.report["end_reason"] in self.stdout,
              "no visible end line for " + self.report["end_reason"])
        self.dumps = sorted(p.name for p in self.session.glob("tdur-*.dmp"))
        return self.report

    def parse(self, name):
        data = (self.session / name).read_bytes()
        return data, self.args.reader_module.parse(data)


def direct_exit_code(args, mode):
    """Reference run: the witness alone, no launcher and no DLL."""
    folder = args.work_root / ("direct-%s-%d" % (mode, time.time_ns()))
    folder.mkdir(parents=True)
    env = dict(os.environ, TDUR_CRASH_WITNESS_MODE=mode,
               TDUR_CRASH_WITNESS_DIR=str(folder))
    env.pop("TDUR_PRIVATE_DOCUMENTS", None)
    return subprocess.run([str(args.witness)], env=env,
                          timeout=60).returncode & 0xFFFFFFFF


def check_crash_dump(args, run, code, access, thread_key):
    report = run.report
    crash = report["crash"]
    dump = crash["dump"]
    check(run.dumps == ["tdur-crash-1.dmp"], "dumps: %s" % run.dumps)
    data, parsed = run.parse("tdur-crash-1.dmp")
    check(dump["helper_exit"] == 0 and dump["bytes"] == len(data) and
          dump["sha256"] == hashlib.sha256(data).hexdigest(),
          "dump identity differs from the report")
    check(dump["helper_ms"] < 5000, "helper took %d ms" % dump["helper_ms"])
    check(parsed["architecture"] == 0, "not x86")
    exception = parsed["exception"]
    check(exception is not None and
          exception["thread_id"] == run.facts[thread_key] and
          int(exception["code"], 16) == code, "exception stream: %s" %
          exception)
    if access is not None:
        check(int(exception["parameters"][0], 16) == access,
              "access kind %s" % exception["parameters"])
    check(len(parsed["threads"]) >= 2, "fewer than two threads")
    modules = [Path(m["name"]).name.lower() for m in parsed["modules"]]
    for name in (args.witness.name.lower(), "dinput8.dll", "ntdll.dll"):
        check(name in modules, name + " missing from the modules")
    summary = dump["summary"]
    check(summary["exception_stream"] and summary["symbols_loaded"] is False,
          "summary stream or symbols")
    check(summary["wow64_transition"]["target_region"] != "unreadable",
          "wow64 transition not read")
    filter_file = run.documents / "crash-filter.json"
    check(filter_file.exists(), "witness filter not called after the dump")
    check(json.loads(filter_file.read_text("utf-8"))["dump_ready"],
          "real filter ran before the dump existed")
    check(report["end_reason"] == "crash-dumped", report["end_reason"])
    check(report["exit_code"] == direct_exit_code(args, run.mode),
          "exit code differs from the direct run (exception masked)")
    return data, parsed


def crash_exec_private(args):
    run = Session(args, "exec")
    run.mode = "crash-exec-private"
    run.run(run.mode, 30)
    data, parsed = check_crash_dump(args, run, 0xC0000005, 8, "worker_tid")
    fault = int(run.facts["fault_address"], 16)
    check(int(parsed["exception"]["address"], 16) == fault, "fault address")
    chunk = args.reader_module.memory_bytes(data, parsed, fault, 16)
    check(chunk == b"TDUR-DUMP-SENTRY", "sentinel bytes missing: %r" % chunk)
    region = args.reader_module.region_for(parsed, fault)
    check(region is not None and region["type"] == "private",
          "fault region: %s" % region)
    print("PASS crash in private memory: dump, sentinel, order, code")


def wer_entries():
    names = set()
    root = Path(os.environ.get("ProgramData", r"C:\ProgramData"))
    for folder in ("ReportArchive", "ReportQueue"):
        base = root / "Microsoft" / "Windows" / "WER" / folder
        try:
            names |= {p.name for p in base.iterdir()
                      if "tdur_crash_witn" in p.name.lower()}
        except OSError:
            pass
    return names


def crash_write_reserved(args):
    before = wer_entries()
    run = Session(args, "reserved")
    run.mode = "crash-write-reserved"
    run.run(run.mode, 30)
    data, parsed = check_crash_dump(args, run, 0xC0000005, 1, "main_tid")
    target = int(run.facts["target_address"], 16)
    region = args.reader_module.region_for(parsed, target)
    check(region is not None and region["state"] == "reserve",
          "target region: %s" % region)
    check(wer_entries() == before, "a WER report was created")
    print("PASS crash on a reserved page: main thread, no WER report")


def crash_stack_overflow(args):
    """Measured limit: the dump of the overflow is written, but the chained
    filter then lacks stack and faults again (0xC0000005). The process
    still ends abnormally and the report names the later entry."""
    run = Session(args, "overflow")
    run.mode = "crash-stack-overflow"
    report = run.run(run.mode, 30)
    check(run.dumps == ["tdur-crash-1.dmp"], "dumps: %s" % run.dumps)
    _, parsed = run.parse("tdur-crash-1.dmp")
    exception = parsed["exception"]
    check(exception["thread_id"] == run.facts["worker_tid"] and
          int(exception["code"], 16) == 0xC00000FD, "exception %s" % exception)
    crash = report["crash"]
    check(crash["exception_code"] == "0xc00000fd" and
          crash["dump"]["helper_exit"] == 0, "crash record %s" % crash)
    check(report["end_reason"] == "crash-dumped", report["end_reason"])
    check(report["exit_code"] >= 0xC0000000, "exit %x" % report["exit_code"])
    direct = direct_exit_code(args, run.mode)
    check(direct == 0xC00000FD, "direct run exit %x" % direct)
    if report["exit_code"] != direct:
        check(crash["later_entries"] >= 1 and
              report["crash_channel"]["last_entry_code"] is not None,
              "changed exit code without a recorded later entry")
    print("PASS stack overflow dumped; exit %x (direct %x), later "
          "entries %d" % (report["exit_code"], direct,
                           crash["later_entries"]))


def crash_handled(args):
    for mode in ("crash-handled", "uef-image-call"):
        run = Session(args, mode)
        report = run.run(mode, 30)
        check(report["exit_code"] == 37, "exit %d" % report["exit_code"])
        check(run.dumps == [] and report["crash"] is None,
              "dump for a handled exception")
        check(report["end_reason"] == "exit", report["end_reason"])
        calls = report["crash_channel"]["image_calls"]
        check(calls == (1 if mode == "uef-image-call" else 0),
              "image calls %d" % calls)
    print("PASS handled fault and image call: no dump, exit")


def hang_ui_stop(args):
    run = Session(args, "stop")
    report = run.run("hang-ui", 40, ["--hang-action", "stop",
                                     "--hang-test-thresholds", "1",
                                     "--hang-dump-after", "6",
                                     "--hang-ask-after", "7",
                                     "--hang-stop-after", "9"])
    check(report["exit_code"] == 0xE1480001, "exit %x" % report["exit_code"])
    check(report["end_reason"] == "hang-stopped", report["end_reason"])
    check("game-stopped reason=hang" in run.stdout, "no stop line")
    check(run.dumps == ["tdur-hang-1.dmp", "tdur-hang-2.dmp"],
          "dumps: %s" % run.dumps)
    for name in run.dumps:
        _, parsed = run.parse(name)
        tids = {thread["thread_id"] for thread in parsed["threads"]}
        check(run.facts["main_tid"] in tids, "main thread missing")
    watch = report["watchdog"]
    check(watch["test_thresholds"] and watch["policy"] == "stop",
          "watchdog settings not reported")
    episode = watch["episodes"][-1]
    check(episode["stopped"] and episode["hung_ms"] >= 9000 and
          episode["dumps"] == [1, 2], "episode: %s" % episode)
    check(report["stop"]["hung_ms"] >= 9000 and
          report["stop"]["exit_code_requested"] == "0xe1480001", "stop")
    print("PASS hang stopped after two readable dumps")


def hang_ui_ask(args, answer):
    """Review F1: the question of a play session through the real launcher.

    The launcher given here is a test variant (crash_tests.cmake) whose question
    returns a fixed answer instead of opening a box. A play session has no
    deadline, so the witness recovers by itself after 15 s.
    """
    run = Session(args, "ask-" + answer)
    report = run.run("hang-ui-recover", 40, [
        "--session-mode", "play", "--hang-action", "ask",
        "--notify", "dialog", "--hang-test-thresholds", "1",
        "--hang-dump-after", "6", "--hang-ask-after", "7"])
    lines = run.stdout.splitlines()
    asked = [index for index, line in enumerate(lines)
             if line.startswith("hang-question-open ")]
    dumped = [index for index, line in enumerate(lines)
              if line.startswith("hang-dump file=")]
    check(len(asked) == 1, "questions asked: %d" % len(asked))
    check(len(dumped) == 2 and dumped[1] < asked[0],
          "the question did not follow the second dump: %s" % run.stdout)
    check("hang-question-answer answer=" + answer in run.stdout,
          "no answer line")
    check(run.dumps == ["tdur-hang-1.dmp", "tdur-hang-2.dmp"],
          "dumps: %s" % run.dumps)
    watch = report["watchdog"]
    check(watch["test_thresholds"] and watch["policy"] == "ask",
          "watchdog settings not reported")
    check(watch["answers_after_episode"] == 0, "answer outside its episode")
    dumping = [e for e in watch["episodes"] if e["dumps"]]
    check(len(dumping) == 1, "episodes: %s" % watch["episodes"])
    episode = dumping[0]
    check(episode["answer"] == answer and not episode["answer_late"] and
          episode["dumps"] == [1, 2], "episode: %s" % episode)
    if answer == "yes":
        check(report["exit_code"] == 0xE1480001,
              "exit %x" % report["exit_code"])
        check(report["end_reason"] == "hang-stopped", report["end_reason"])
        check(episode["stopped"] and "game-stopped reason=hang" in run.stdout,
              "episode not stopped")
        stop = report["stop"]
        check(stop["answer"] == "yes" and stop["policy"] == "ask" and
              stop["exit_code_requested"] == "0xe1480001", "stop: %s" % stop)
        print("PASS question answered yes: asked once after two dumps, "
              "game stopped")
    else:
        check(report["exit_code"] == 37, "exit %d" % report["exit_code"])
        check(report["end_reason"] == "exit", report["end_reason"])
        check(report["stop"] is None and not episode["stopped"] and
              "game-stopped" not in run.stdout, "a no answer stopped the game")
        print("PASS question answered no: asked once, game kept until it "
              "recovered")


def hang_cross_thread(args):
    run = Session(args, "cross")
    report = run.run("hang-cross-thread", 20, [
        "--hang-test-thresholds", "1", "--hang-dump-after", "6"])
    check(run.dumps == ["tdur-hang-1.dmp"], "dumps: %s" % run.dumps)
    check(report["end_reason"] == "deadline" and report["timed_out"],
          report["end_reason"])
    check(report["stop"] is None, "dump-only stopped the target")
    print("PASS cross-thread hang: one dump, no stop before deadline")


def hang_loader(args):
    run = Session(args, "loader")
    report = run.run("hang-loader", 20, [
        "--hang-test-thresholds", "1", "--hang-dump-after", "6"])
    check(run.dumps == ["tdur-hang-1.dmp"], "dumps: %s" % run.dumps)
    summary = report["hang_dumps"][0]["summary"]
    check(summary["loader_lock_owner_tid"] == run.facts["worker_tid"],
          "loader lock owner %s" % summary["loader_lock_owner_tid"])
    print("PASS loader lock hang: holder named in the dump summary")


def no_false_positive(args, mode, seconds):
    run = Session(args, mode)
    report = run.run(mode, seconds)
    check(report["exit_code"] == 37, "exit %d" % report["exit_code"])
    check(run.dumps == [] and report["hang_dumps"] == [], "false positive")
    check(report["end_reason"] == "exit", report["end_reason"])
    episodes = report["watchdog"]["episodes"]
    check(all(e["hung_ms"] < 15000 and not e["dumps"] for e in episodes),
          "episodes: %s" % episodes)
    print("PASS no false positive (%s): %d short episode(s)" % (
        mode, len(episodes)))


def foreign(args):
    decoy_dir = args.work_root / ("decoy-%d" % time.time_ns())
    decoy_dir.mkdir(parents=True)
    decoy = decoy_dir / "tdur_crash_decoy.exe"
    shutil.copy2(args.witness, decoy)
    env = dict(os.environ, TDUR_CRASH_WITNESS_MODE="hang-ui",
               TDUR_CRASH_WITNESS_DIR=str(decoy_dir))
    env.pop("TDUR_PRIVATE_DOCUMENTS", None)
    decoy_process = subprocess.Popen([str(decoy)], env=env)
    try:
        run = Session(args, "foreign")
        report = run.run("idle", 20, ["--hang-test-thresholds", "1",
                                      "--hang-dump-after", "6"])
        check(decoy_process.poll() is None, "the decoy was stopped")
        check(run.dumps == [] and report["hang_dumps"] == [],
              "a dump was written while only the decoy hung")
        check(report["owned_pids"] == [report["pid"]] and
              str(decoy_process.pid) not in json.dumps(report),
              "the report names a foreign process")
        check(report["end_reason"] == "deadline", report["end_reason"])
    finally:
        decoy_process.kill()  # This fixture's own decoy.
        decoy_process.wait(timeout=10)
    print("PASS foreign hung process untouched")


def helper_gone(args):
    """Review D1: a failing diagnostic never stops the game."""
    run = Session(args, "gone")
    folder = run.root / "bin"
    folder.mkdir()
    run.launcher = folder / args.launcher.name
    shutil.copy2(args.launcher, run.launcher)
    helper = folder / "tdur_dump_helper.exe"
    shutil.copy2(args.launcher.parent / "tdur_dump_helper.exe", helper)

    def remove_helper():
        helper.rename(folder / "tdur_dump_helper.removed")

    report = run.run("hang-ui", 15, ["--hang-test-thresholds", "1",
                                     "--hang-dump-after", "6"],
                     after_pid=remove_helper)
    check(report["end_reason"] == "deadline" and report["timed_out"],
          "the game did not live until the deadline: %s" %
          report["end_reason"])
    check(report["hang_dumps"] and
          report["hang_dumps"][0]["error_step"] == "helper-start" and
          not report["hang_dumps"][0]["started"], "helper error not reported")
    check(any(e["step"] == "helper-start"
              for e in report["diagnostic_errors"]), "diagnostic errors")
    check("hang-dump file=tdur-hang-1.dmp exit=-1" in run.stdout,
          "failed dump not announced")
    print("PASS helper missing after self-check: game kept, error shown")


def options(args):
    """Reviews D6 and D7: refusals before any process is created."""
    cases = [
        (["--hang-action", "stop", "--session-mode", "play"], "player"),
        (["--hang-action", "ask"], "notify dialog"),
        (["--hang-action", "stop", "--hang-stop-after", "9"], "range"),
        (["--hang-ask-after", "30", "--notify", "dialog"], "range"),
        (["--hang-test-thresholds", "2"], "flag"),
        (["--hang-dump-after", "60", "--hang-test-thresholds", "1",
          "--hang-stop-after", "30"], "follow"),
        (["--notify", "dialog", "--notify", "dialog"], "repeated"),
    ]
    for extra, needle in cases:
        run = Session(args, "options")
        result = subprocess.run(run.command(5, extra), capture_output=True,
                                text=True, timeout=30)
        check(result.returncode == 2 and needle in result.stderr,
              "%s accepted or wrong error: %s" % (extra, result.stderr))
        check(not (run.session / "pid.json").exists(), "process created")
    # D7: a report outside the private session folder is refused.
    run = Session(args, "options")
    elsewhere = run.root / "elsewhere"
    elsewhere.mkdir()
    result = subprocess.run(run.command(5, (), elsewhere / "report.json"),
                            capture_output=True, text=True, timeout=30)
    check(result.returncode == 2 and "session folder" in result.stderr,
          "dump folder outside the session accepted: " + result.stderr)
    print("PASS launcher refusals: floors, policies, dump folder")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", choices=SCENARIOS)
    for name in ("launcher", "witness", "dll", "reader", "loader-dll",
                 "work-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    sys.path.insert(0, str(args.reader.parent))
    import minidump_summary
    args.reader_module = minidump_summary
    {"crash-exec-private": crash_exec_private,
     "crash-write-reserved": crash_write_reserved,
     "crash-stack-overflow": crash_stack_overflow,
     "crash-handled": crash_handled,
     "hang-ui-stop": hang_ui_stop,
     "hang-ui-ask-yes": lambda a: hang_ui_ask(a, "yes"),
     "hang-ui-ask-no": lambda a: hang_ui_ask(a, "no"),
     "hang-cross-thread": hang_cross_thread,
     "hang-loader": hang_loader,
     "busy-pumping": lambda a: no_false_positive(a, "busy-pumping", 45),
     "stutter": lambda a: no_false_positive(a, "stutter", 45),
     "foreign": foreign, "helper-gone": helper_gone,
     "options": options}[args.scenario](args)


if __name__ == "__main__":
    main()
