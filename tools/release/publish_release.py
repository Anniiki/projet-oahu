"""Publish an immutable development snapshot after the workflow's test jobs."""
from pathlib import Path
from hashlib import sha256
import argparse
import importlib.util
import json
import re
import subprocess
import tempfile
import zipfile

_spec = importlib.util.spec_from_file_location(
    "oahu_package_devtools", Path(__file__).with_name("package_devtools.py"))
_package = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_package)
PACKAGE_FILES = _package.PACKAGE_FILES


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout


def publish(repository, source, package_dir):
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Invalid repository name")
    if not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("A full source commit is required")
    package_dir = Path(package_dir).resolve()
    manifest = json.loads((package_dir / "manifest.json").read_text("utf-8"))
    if manifest["source_commit"] != source or manifest["architecture"] != "x64":
        raise ValueError("Package source or architecture mismatch")
    tag = manifest["tag"]
    if not re.fullmatch(r"dev-\d{8}-" + re.escape(source[:7]), tag):
        raise ValueError("Unexpected development tag")
    archive = package_dir / ("Projet-Oahu-" + tag + "-windows-x64.zip")
    checksum = (package_dir / "SHA256SUMS.txt").read_text("ascii").strip()
    if checksum != sha256(archive.read_bytes()).hexdigest() + "  " + archive.name:
        raise ValueError("Archive checksum mismatch")
    with zipfile.ZipFile(archive) as packed:
        if packed.testzip() is not None or set(packed.namelist()) != PACKAGE_FILES:
            raise ValueError("Unexpected archive contents")
        if json.loads(packed.read("manifest.json")) != manifest:
            raise ValueError("Archive and standalone manifests differ")
        if {row["file"] for row in manifest["binaries"]} != set(_package.BINARIES):
            raise ValueError("Incomplete binary manifest")
        if len(manifest["binaries"]) != len(_package.BINARIES):
            raise ValueError("Duplicate binary manifest entries")
        for row in manifest["binaries"]:
            data = packed.read(row["file"])
            _package.verify_binary(data, row["file"])
            if len(data) != row["bytes"] or sha256(data).hexdigest() != row["sha256"]:
                raise ValueError("Packaged binary checksum mismatch")
    validation = manifest["validation"]["ctest"]
    if validation["tests"] <= 0 or validation["failures"] or validation["errors"]:
        raise ValueError("Package has no successful test summary")

    try:
        existing = json.loads(gh("api", "repos/" + repository + "/releases/tags/" + tag))
    except subprocess.CalledProcessError as error:
        if "(HTTP 404)" not in error.stderr:
            raise
        existing = None
    if existing is not None:
        ref = json.loads(gh("api", "repos/" + repository + "/git/ref/tags/" + tag))
        if ref["object"]["type"] != "commit" or ref["object"]["sha"] != source:
            raise ValueError("Existing development tag points to another object")
        print("Release already exists for this commit; assets remain unchanged.")
        return

    notes = (
        "## Development snapshot\n\n"
        "Source commit: " + source + "\n\n"
        "Includes Windows x64 installation, bank and database inspection tools,\n"
        "native D3D12 texture and mesh viewers, MIT license, guides and checksums.\n\n"
        "## Validation\n\n"
        "Windows x64 and Win32 build/test jobs completed successfully. The x64\n"
        "package records " + str(validation["tests"]) + " registered CPU tests, with no\n"
        "failures or errors. GPU-dependent tests are excluded on hosted runners;\n"
        "hardware and WARP validation is recorded separately in project evidence.\n\n"
        "## Scope\n\n"
        "Full gameplay, authenticated multiplayer, two-PC co-op and the final\n"
        "installer remain under reconstruction. Commercial game data, saves,\n"
        "keys and the original executable are excluded. Use your own installation.\n"
    )
    # A named temporary file is removed individually, never a computed tree.
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", suffix=".md",
                                     prefix="oahu-release-notes-", delete=False) as stream:
        stream.write(notes)
        notes_path = Path(stream.name)
    try:
        result = gh("release", "create", tag, str(archive),
                    str(package_dir / "manifest.json"),
                    str(package_dir / "SHA256SUMS.txt"),
                    "--repo", repository, "--target", source, "--prerelease",
                    "--latest=false", "--title", "Development snapshot " + tag,
                    "--notes-file", str(notes_path))
        print(result.strip())
    finally:
        notes_path.unlink()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    publish(args.repository, args.source, args.package_dir)
