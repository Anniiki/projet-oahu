"""Check fail-closed termination of our own executable-code fixture."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture", required=True, type=Path)
    args = parser.parse_args()
    fixture = args.fixture.resolve(strict=True)
    if fixture.name != "tdur_code_patch_test.exe":
        parser.error("expected the source-owned code-patch fixture")
    result = subprocess.run(
        [str(fixture), "--fatal-protection"], capture_output=True,
        text=True, timeout=10,
    )
    if result.returncode != 29:  # ERROR_WRITE_FAULT from TerminateProcess(self)
        raise RuntimeError(
            f"expected fail-closed exit 29, got {result.returncode}: "
            f"{result.stdout} {result.stderr}"
        )
    print("persistent protection failure terminated only the owned fixture")


if __name__ == "__main__":
    main()
