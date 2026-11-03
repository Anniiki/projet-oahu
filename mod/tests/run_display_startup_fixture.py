"""Exercise the actual injected display gate on a source-owned D3D9 fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def hashes(root):
    return {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(root.rglob("*")) if p.is_file()}


def main():
    parser = argparse.ArgumentParser()
    for name in ("launcher", "witness", "dll", "work-root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--mode", choices=("borderless", "exclusive"), default="borderless")
    args = parser.parse_args()
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
    command = [str(args.launcher.resolve(strict=True)), "--exe", str(witness),
               "--dll", str(args.dll.resolve(strict=True)),
               "--source-documents", str(source), "--private-documents", str(private),
               "--source-saves", str(saves), "--log", str(log), "--report", str(report),
               "--pid-file", str(pid_file), "--resume-file", str(resume), "--max-seconds", "15"]
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
        assert not (private / "display-startup-proof.json").exists()
        resume.write_text("source-owned fixture registered\n", encoding="ascii")
        stdout, stderr = child.communicate(timeout=20)
        assert child.returncode == 0, stdout + stderr
        result = json.loads(report.read_text(encoding="utf-8"))
        assert result["pid"] == identity["pid"] and not result["timed_out"]
        assert result["exit_code"] == 0, "fixture failed; inspect " + str(root)
        assert result["locked_save_files"] == 17 and result["source_saves_unchanged"]
        assert hashes(source) == before and hashes(installation) == installation_before
        proof = json.loads((private / "display-startup-proof.json").read_text(encoding="utf-8"))
        assert proof["success"] and proof["native_d3d12"] and proof["presents"] == 3
        trace = log.read_text(encoding="utf-8")
        events = ["hooks-installed-pre-entry", "display-public-factory-gate-armed",
                  "display-deferred-initialization", "display-first-dxgi-frame"]
        positions = [trace.index("event=" + event) for event in events]
        assert positions == sorted(positions), "factory setup was not deferred in order"
        assert "display-first-dxgi-frame on12=1" in trace and "focus=0" in trace
        assert "retail-video-defaults-identity-refused" in trace
        assert "retail-background-identity-refused" in trace
        assert "retail-background-branches-applied" not in trace
        assert "retail-video-defaults-applied" not in trace
        first = next(line for line in trace.splitlines()
                     if "event=display-first-dxgi-frame" in line)
        assert "exclusive=0" in first
        assert ("exclusive_pending=1" in first) == (args.mode == "exclusive")
        assert ("mode=2" in first) == (args.mode == "exclusive")
        (root / "result.json").write_text(json.dumps({
            "game_executed": False, "success": True, "proof": proof,
            "requested_mode": args.mode,
            "exclusive_pending": args.mode == "exclusive",
            "source_files_unchanged": True,
            "dll_sha256": hashlib.sha256(args.dll.read_bytes()).hexdigest(),
            "evidence_directory": str(root)}, indent=2) + "\n", encoding="utf-8")
        print("PASS injected deferred D3D9On12 startup; 17 source canaries unchanged; " + str(root))
    finally:
        if child.poll() is None:
            child.kill()  # Only this owned launcher; its Job closes the fixture.
            child.wait(timeout=5)


if __name__ == "__main__":
    main()
