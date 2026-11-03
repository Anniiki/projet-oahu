"""Build a source-owned x64 tools archive, without commercial game files."""
from pathlib import Path
from hashlib import sha256
import argparse
import datetime
import json
import re
import shutil
import struct
import subprocess
import xml.etree.ElementTree as ET
import zipfile

BINARIES = {
    "tdur_inspect_game.exe": "Release",
    "tdur_inspect_bank.exe": "Release",
    "tdur_inspect_database.exe": "Release",
    "tdur_view_texture.exe": "tests/render/Release",
    "tdur_view_mesh.exe": "tests/render/Release",
}
DOCUMENTS = {
    "LICENSE": "LICENSE",
    "README.md": "PROJECT-README.md",
    "README.fr.md": "PROJECT-README.fr.md",
}
PACKAGE_FILES = set(BINARIES) | set(DOCUMENTS.values()) | {"README.txt", "manifest.json"}
PAYLOAD_FILES = PACKAGE_FILES - {"manifest.json"}
MANIFEST_VERSION = 2


def validate_test_counts(summary):
    keys = ("tests", "failures", "errors", "skipped")
    if any(type(summary.get(key)) is not int or summary[key] < 0 for key in keys):
        raise ValueError("Test counts must be nonnegative integers")
    if (summary["tests"] <= summary["skipped"] or summary["failures"]
            or summary["errors"]):
        raise ValueError("At least one executed, successful test is required")


def project_version(repo):
    text = (repo / "CMakeLists.txt").read_text("utf-8")
    match = re.search(r"\bproject\s*\(\s*TDURecompilation\s+VERSION\s+"
                      r"(\d+\.\d+\.\d+)\s", text, re.IGNORECASE)
    if not match:
        raise ValueError("The source project version could not be identified")
    return match.group(1)


def cmake_literal(value):
    # Bracket arguments do not expand $ variables, quotes or backslashes.
    value = str(value)
    delimiter = "="
    while "]" + delimiter + "]" in value:
        delimiter += "="
    return "[" + delimiter + "[" + value + "]" + delimiter + "]"


def cpack_archive(payload, archive, version, cpack):
    # Only this curated, already validated staging directory is installed.
    # CPack's generated install scripts also interpret list/glob/variable syntax.
    validate_cpack_path(archive.parent)
    variables = {
        "CPACK_GENERATOR": "ZIP", "CPACK_PACKAGE_NAME": "Projet-Oahu-Tools",
        "CPACK_PACKAGE_VERSION": version,
        "CPACK_PACKAGE_DESCRIPTION_SUMMARY": "Development inspection tools",
        "CPACK_PACKAGE_DESCRIPTION": "Projet Oahu development inspection tools for Windows x64",
        "CPACK_PACKAGE_FILE_NAME": archive.stem,
        "CPACK_PACKAGE_DIRECTORY": archive.parent.as_posix(),
        "CPACK_INSTALLED_DIRECTORIES": payload.as_posix() + ";/",
        "CPACK_INCLUDE_TOPLEVEL_DIRECTORY": "OFF",
        "CPACK_THREADS": "1", "CPACK_ARCHIVE_THREADS": "1",
    }
    config = archive.parent / "CPackConfig.cmake"
    config.write_text("".join("set(" + key + " " + cmake_literal(value) + ")\n"
                              for key, value in variables.items()), encoding="utf-8")
    log_path = archive.parent / "cpack.log"
    try:
        with log_path.open("wb") as log:
            subprocess.run([str(cpack), "--config", str(config), "-G", "ZIP"],
                           cwd=archive.parent, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=300)
    except subprocess.CalledProcessError as error:
        error.add_note(log_path.read_text(encoding="utf-8", errors="replace")[-4000:])
        raise


def validate_cpack_path(path):
    if any(character in str(path) for character in ";$[]\r\n"):
        raise ValueError("CPack output paths cannot contain ; $ [ ] or newlines")


def verify_archive(archive, manifest):
    """Validate the same boundary before delivery and before network access."""
    if (manifest.get("schema_version") != MANIFEST_VERSION
            or manifest.get("product") != "development-tools"
            or not re.fullmatch(r"\d+\.\d+\.\d+", manifest.get("client_version", ""))
            or manifest.get("protocol") != {"version": None, "status": "not-applicable"}
            or manifest.get("architecture") != "x64"
            or manifest.get("configuration") != "Release"
            or not re.fullmatch(r"[0-9a-f]{64}", manifest["validation"].get("ctest_report_sha256", ""))
            or manifest.get("commercial_assets_included") is not False):
        raise ValueError("Unsupported package identity")
    validate_test_counts(manifest["validation"]["ctest"])
    rows = manifest["files"]
    if (len(rows) != len(PAYLOAD_FILES)
            or {row["file"] for row in rows} != PAYLOAD_FILES):
        raise ValueError("Incomplete or duplicate file manifest")
    binaries = [row for row in rows if row["file"] in BINARIES]
    if manifest["binaries"] != binaries:
        raise ValueError("Binary and complete manifests differ")
    with zipfile.ZipFile(archive) as packed:
        names = packed.namelist()
        if (len(names) != len(PACKAGE_FILES) or set(names) != PACKAGE_FILES
                or packed.testzip() is not None):
            raise ValueError("Unexpected or duplicate archive entries")
        if json.loads(packed.read("manifest.json")) != manifest:
            raise ValueError("Archive and standalone manifests differ")
        for row in rows:
            data = packed.read(row["file"])
            if (type(row["bytes"]) is not int or len(data) != row["bytes"]
                    or sha256(data).hexdigest() != row["sha256"]):
                raise ValueError("Packaged file checksum mismatch: " + row["file"])
            if row["file"] in BINARIES:
                verify_binary(data, row["file"])


def verify_binary(data, name):
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Not a PE executable: " + name)
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 6 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("Invalid PE header: " + name)
    if struct.unpack_from("<H", data, pe + 4)[0] != 0x8664:
        raise ValueError("Only x64 tools can be packaged: " + name)
    # Release binaries should not disclose a developer's Windows profile path.
    for decoded in (data.decode("latin1"), data.decode("utf-16le", errors="ignore")):
        if re.search(r"[A-Za-z]:\\Users\\", decoded, re.IGNORECASE):
            raise ValueError("Embedded Windows profile path: " + name)


def test_summary(path):
    root = ET.fromstring(path if isinstance(path, bytes) else Path(path).read_bytes())
    # Sum leaves only: aggregate testsuite nodes must not count children twice.
    suites = [s for s in root.iter("testsuite") if s.find("testsuite") is None]
    for suite in suites:
        counts = {key: int(suite.get(key, 0))
                  for key in ("tests", "failures", "errors", "skipped")}
        if (any(value < 0 for value in counts.values())
                or sum(counts[k] for k in ("failures", "errors", "skipped")) > counts["tests"]):
            raise ValueError("Inconsistent test counts")
    summary = {name: sum(int(s.get(name, 0)) for s in suites)
               for name in ("tests", "failures", "errors", "skipped")}
    validate_test_counts(summary)
    if (next(root.iter("failure"), None) is not None
            or next(root.iter("error"), None) is not None):
        raise ValueError("A successful, nonempty CTest report is required")
    return summary


def package(repo, build_dir, output_dir, report, cpack=None):
    repo, build_dir, output_dir = map(lambda p: Path(p).resolve(),
                                      (repo, build_dir, output_dir))
    git = lambda *args: subprocess.check_output(
        ["git", "-C", str(repo), *args], text=True, encoding="utf-8").strip()
    source = git("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", source) or git("status", "--porcelain"):
        raise ValueError("A clean, identified source commit is required")
    report_data = Path(report).read_bytes()
    summary = test_summary(report_data)
    version = project_version(repo)
    date = git("show", "-s", "--format=%cI", "HEAD")[:10].replace("-", "")
    tag = "dev-" + date + "-" + source[:7]
    if output_dir == repo or repo in output_dir.parents:
        raise ValueError("Package output must be outside the source tree")
    if output_dir.exists():
        raise ValueError("Package output must be a new directory")
    validate_cpack_path(output_dir)
    cpack = cpack or shutil.which("cpack")
    if not cpack:
        raise ValueError("CPack is required; add it to PATH or pass --cpack")
    cpack_version = subprocess.check_output(
        [str(cpack), "--version"], text=True, encoding="utf-8", timeout=15).splitlines()[0]

    validated = {}
    for name, folder in BINARIES.items():
        data = (build_dir / folder / name).read_bytes()
        verify_binary(data, name)
        validated[name] = data
    for source_name in DOCUMENTS:
        if not (repo / source_name).is_file():
            raise ValueError("Required project document is missing: " + source_name)

    output_dir.mkdir(parents=True)
    payload = output_dir / "payload"
    payload.mkdir()
    for name, data in validated.items():
        (payload / name).write_bytes(data)
    for source_name, dest in DOCUMENTS.items():
        shutil.copyfile(repo / source_name, payload / dest)
    (payload / "README.txt").write_text(
        "Projet Oahu development inspection tools - Windows x64\n\n"
        "The complete playable client and final installer remain under reconstruction.\n"
        "The five tools and every accompanying file are listed in manifest.json.\n"
        "Provide your own local TDU PC data. No commercial game files are included.\n\n"
        "Requirements: Windows 10/11 x64, Microsoft Visual C++ x64 runtime,\n"
        "and D3D12 for the viewers (--warp selects software rendering).\n\n"
        "Open a terminal here. Commands:\n"
        'tdur_inspect_game.exe --game-dir "D:\\Games\\Test Drive Unlimited"\n'
        'tdur_inspect_database.exe --game-dir "D:\\Games\\Test Drive Unlimited"\n'
        "tdur_inspect_bank.exe --bank PATH --verify-extractions\n"
        "tdur_view_texture.exe --help\n"
        "tdur_view_mesh.exe --help\n\n"
        "These tools read game data without installing or modifying the game.\n"
        "Source and evidence: https://github.com/Anniiki/projet-oahu\n",
        encoding="utf-8")
    rows = []
    for item in sorted(payload.iterdir()):
        data = item.read_bytes()
        rows.append({"file": item.name, "bytes": len(data), "sha256": sha256(data).hexdigest()})
    manifest = {
        "schema_version": MANIFEST_VERSION, "product": "development-tools",
        "client_version": version,
        "protocol": {"version": None, "status": "not-applicable"},
        "tag": tag, "source_commit": source, "architecture": "x64",
        "configuration": "Release", "cpp_mode": "/std:c++latest",
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "Development inspection tools and native D3D12 viewers",
        "validation": {"ctest": summary, "gpu_tests_on_hosted_runner": False,
                       "ctest_report_sha256": sha256(report_data).hexdigest()},
        "packaging": {"backend": "CPack", "generator": "ZIP", "version": cpack_version},
        "provenance": {"build_source_binding": "not-attested",
                       "note": "Packaging checks inputs; it does not rebuild or attest their source."},
        "commercial_assets_included": False, "files": rows,
        "binaries": [row for row in rows if row["file"] in BINARIES],
    }
    (payload / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if {p.name for p in payload.iterdir()} != PACKAGE_FILES:
        raise ValueError("Package inventory differs from its allowlist")
    archive = output_dir / ("Projet-Oahu-" + tag + "-windows-x64.zip")
    cpack_archive(payload, archive, version, cpack)
    verify_archive(archive, manifest)
    digest = sha256(archive.read_bytes()).hexdigest()
    (output_dir / "SHA256SUMS.txt").write_text(
        digest + "  " + archive.name + "\n", encoding="ascii")
    shutil.copyfile(payload / "manifest.json", output_dir / "manifest.json")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--ctest-report", type=Path, required=True)
    parser.add_argument("--cpack", type=Path, help="Explicit CPack executable (otherwise PATH)")
    args = parser.parse_args()
    result = package(args.repo, args.build_dir, args.output_dir, args.ctest_report, args.cpack)
    print(json.dumps({"tag": result["tag"], "source_commit": result["source_commit"]}))
