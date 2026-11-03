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
    root = ET.parse(path).getroot()
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    summary = {name: sum(int(s.get(name, 0)) for s in suites)
               for name in ("tests", "failures", "errors", "skipped")}
    if (summary["tests"] == 0 or summary["failures"] or summary["errors"]
            or next(root.iter("failure"), None) is not None
            or next(root.iter("error"), None) is not None):
        raise ValueError("A successful, nonempty CTest report is required")
    return summary


def package(repo, build_dir, output_dir, report):
    repo, build_dir, output_dir = map(lambda p: Path(p).resolve(),
                                      (repo, build_dir, output_dir))
    git = lambda *args: subprocess.check_output(
        ["git", "-C", str(repo), *args], text=True, encoding="utf-8").strip()
    source = git("rev-parse", "HEAD")
    if not re.fullmatch(r"[0-9a-f]{40}", source) or git("status", "--porcelain"):
        raise ValueError("A clean, identified source commit is required")
    summary = test_summary(report)
    date = git("show", "-s", "--format=%cI", "HEAD")[:10].replace("-", "")
    tag = "dev-" + date + "-" + source[:7]
    if output_dir == repo or repo in output_dir.parents:
        raise ValueError("Package output must be outside the source tree")
    if output_dir.exists():
        raise ValueError("Package output must be a new directory")

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
    rows = []
    for name, data in validated.items():
        (payload / name).write_bytes(data)
        rows.append({"file": name, "bytes": len(data), "sha256": sha256(data).hexdigest()})
    for source_name, dest in DOCUMENTS.items():
        shutil.copyfile(repo / source_name, payload / dest)
    (payload / "README.txt").write_text(
        "Projet Oahu development inspection tools - Windows x64\n\n"
        "Full gameplay, multiplayer, co-op and the final installer remain under reconstruction.\n"
        "This archive contains only the five source-owned tools listed in manifest.json.\n"
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
    manifest = {
        "tag": tag, "source_commit": source, "architecture": "x64",
        "configuration": "Release", "cpp_mode": "/std:c++latest",
        "created_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "Development inspection tools and native D3D12 viewers",
        "validation": {"ctest": summary, "gpu_tests_on_hosted_runner": False},
        "commercial_assets_included": False, "binaries": rows,
    }
    (payload / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    if {p.name for p in payload.iterdir()} != PACKAGE_FILES:
        raise ValueError("Package inventory differs from its allowlist")
    archive = output_dir / ("Projet-Oahu-" + tag + "-windows-x64.zip")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as packed:
        for item in sorted(payload.iterdir()):
            packed.write(item, arcname=item.name)
    with zipfile.ZipFile(archive) as packed:
        if packed.testzip() is not None or set(packed.namelist()) != PACKAGE_FILES:
            raise ValueError("Archive integrity check failed")
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
    args = parser.parse_args()
    result = package(args.repo, args.build_dir, args.output_dir, args.ctest_report)
    print(json.dumps({"tag": result["tag"], "source_commit": result["source_commit"]}))
