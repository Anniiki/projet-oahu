"""A late LoadLibrary must fail before installing any observation hook."""

import argparse
import os
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    for name in ("witness", "dll", "work-root"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    witness = args.witness.resolve(strict=True)
    if witness.name != "tdur_launch_witness.exe":
        parser.error("expected the owned launcher fixture")
    root = args.work_root / str(time.time_ns())
    source = root / "source"
    private = root / "private"
    source.mkdir(parents=True)
    private.mkdir()
    log = private / "observer.log"
    env = dict(os.environ, TDUR_SOURCE_DOCUMENTS=str(source.resolve()),
               TDUR_PRIVATE_DOCUMENTS=str(private.resolve()),
               TDUR_OBSERVATION_LOG=str(log.resolve()),
               TDUR_D3D12_DISPLAY="0")
    result = subprocess.run(
        [str(witness), "--late-load", str(args.dll.resolve(strict=True))],
        env=env, capture_output=True, text=True, timeout=10,
    )
    if result.returncode != 0 or log.exists():
        raise RuntimeError(
            f"late DLL load was not refused before hooks: {result.returncode}; "
            f"{result.stdout} {result.stderr}"
        )
    print("late owned DLL load refused before any hook or observer log")


if __name__ == "__main__":
    main()
