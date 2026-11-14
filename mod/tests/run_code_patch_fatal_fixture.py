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
    for argument in ("--fatal-protection", "--fatal-protection-detour"):
        result = subprocess.run(
            [str(fixture), argument], capture_output=True,
            text=True, timeout=10,
        )
        # ERROR_WRITE_FAULT from TerminateProcess(self)
        if result.returncode != 29:
            raise RuntimeError(
                f"{argument}: expected fail-closed exit 29, got "
                f"{result.returncode}: {result.stdout} {result.stderr}"
            )
        print(f"{argument}: persistent protection failure terminated only "
              "the owned fixture")


if __name__ == "__main__":
    main()
