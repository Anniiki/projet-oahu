"""Validate a v2 development package without contacting a remote service."""
import argparse
from hashlib import sha256
import importlib.util
import json
from pathlib import Path
import re


def check(source, directory):
    if not re.fullmatch(r"[0-9a-f]{40}", source):
        raise ValueError("A full source commit is required")
    spec = importlib.util.spec_from_file_location(
        "oahu_package_check", Path(__file__).with_name("package_devtools.py"))
    package = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(package)
    directory = Path(directory).resolve()
    manifest = json.loads((directory / "manifest.json").read_text("utf-8"))
    if manifest.get("source_commit") != source:
        raise ValueError("Package source mismatch")
    packaging = manifest.get("packaging", {})
    if (packaging.get("backend") != "CPack" or packaging.get("generator") != "ZIP"
            or not packaging.get("version", "").startswith("cpack version ")):
        raise ValueError("The package must identify its CPack ZIP producer")
    tag = manifest.get("tag", "")
    if not re.fullmatch(r"dev-\d{8}-" + re.escape(source[:7]), tag):
        raise ValueError("Unexpected development tag")
    archive = directory / ("Projet-Oahu-" + tag + "-windows-x64.zip")
    digest = sha256(archive.read_bytes()).hexdigest()
    if (directory / "SHA256SUMS.txt").read_text("ascii").strip() != digest + "  " + archive.name:
        raise ValueError("Archive checksum mismatch")
    package.verify_archive(archive, manifest)
    return {"source_commit": source, "tag": tag, "schema_version": 2,
            "sha256": digest, "files": len(manifest["files"]),
            "ctest": manifest["validation"]["ctest"]}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True)
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(check(args.source, args.package_dir)))
