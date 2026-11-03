"""Regression CLI, using only a new source-owned KNAB bank and new outputs.

Requires the final mesh viewer plus mesh_material_cli_fixture. The fixture
writer performs CPU format checks; this script runs actual headless rendering.
--work-root requires a new directory. --work-parent atomically creates a unique
leaf below the supplied base, preserving all previous runs and their evidence.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from typing import Any


MATERIAL_A = "4100000000000000"
MATERIAL_B = "4100000000000080"
BLACK_KEY = "424c41434b000000"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def sha256(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_new(path: Path, content: bytes) -> None:
    with path.open("xb") as destination:
        destination.write(content)


def write_json_new(path: Path, value: Any) -> None:
    write_new(path, (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8"))


def bitmap_is_rgb_black(path: Path, size: int) -> None:
    data = path.read_bytes()
    require(data[:2] == b"BM" and len(data) >= 54, "viewer must export a BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    bits = struct.unpack_from("<H", data, 28)[0]
    require((width, height, bits) == (size, -size, 32), "BMP dimensions and top-down RGBA format")
    require(len(data) == offset + size * size * 4, "BMP has a complete tightly packed image")
    pixels = data[offset:]
    require(all(pixels[i] == pixels[i + 1] == pixels[i + 2] == 0
                for i in range(0, len(pixels), 4)), "requested image must remain RGB black")


def parse_success(result: subprocess.CompletedProcess[str], name: str) -> dict[str, Any]:
    require(result.returncode == 0, f"{name} must succeed, got {result.returncode}: {result.stderr}")
    value = json.loads(result.stdout)
    require(isinstance(value, dict), f"{name} returns a JSON object")
    return value


def check_viewer_report(report: dict[str, Any], *, filtered: int, size: int) -> None:
    require(report.get("scope") == "3dd-selected-material-texture-inspection", "explicit textured inspection scope")
    require(report.get("game_executed") is False and report.get("api") == "D3D12", "only our D3D12 resource viewer executed")
    require(report.get("material_textures_applied") is True, "selected texture stage applied")
    require(report.get("original_shaders_reconstructed") is False and report.get("inferred_material") is False, "no original-shader or inferred-material claim")
    require(report.get("inspection_lighting") is False, "texture inspection stays unlit")
    require(report.get("draw_count") == 1 and report.get("drawn_triangles") == 2, "only A's two triangles rendered")
    require(report.get("target_size") == size, "requested target size used")
    require(report.get("coverage_pixels") == 0, "valid black texture may have zero RGB coverage")
    coverage = report.get("geometry_coverage_pixels")
    require(isinstance(coverage, int) and coverage >= 64, "white geometric pass proves actual coverage independently of texture RGB")
    fingerprint = report.get("rendered_rgba_fnv1a64")
    require(isinstance(fingerprint, str) and len(fingerprint) == 16 and
            all(c in "0123456789abcdefABCDEF" for c in fingerprint), "requested image has a complete FNV64 fingerprint")
    require(report.get("filtered_primitive_draws") == filtered, "only the explicitly excluded primitive is filtered")
    require(report.get("selected_texture_stage") == 0 and report.get("texture_member_indices") == [3], "explicit stage and resource collection preserved")
    draws = report.get("material_draws")
    require(isinstance(draws, list) and len(draws) == 1, "one material binding reported")
    binding = draws[0]
    require(isinstance(binding, dict), "material binding is a JSON object")
    require(binding.get("node") == 1 and binding.get("entry") == 0 and binding.get("primitive") == 0, "A's exact node/OBJ/PRIM selected")
    require(binding.get("material_index") == 0 and binding.get("texture_index") == 0, "typed cache token zero is a valid renderer index")
    require(binding.get("material_key_hex") == MATERIAL_A and binding.get("texture_key_hex") == BLACK_KEY, "full raw8 keys reported without NUL truncation")
    require(binding.get("sampler_indices") == [0, 0, 0, 0], "linear/wrap inspection sampler is explicit")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--viewer", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    work = parser.add_mutually_exclusive_group(required=True)
    work.add_argument("--work-root", type=Path, help="exact new evidence directory")
    work.add_argument("--work-parent", type=Path, help="base for a new unique evidence directory on every invocation")
    parser.add_argument("--warp", action="store_true", help="use the viewer's explicit software adapter")
    parser.add_argument("--debug", action="store_true", help="request the D3D12 debug layer")
    args = parser.parse_args()
    viewer = args.viewer.resolve(strict=True)
    fixture_writer = args.fixture.resolve(strict=True)
    require(viewer.is_file() and fixture_writer.is_file(), "both input executables must be files")
    if args.work_root is not None:
        root = args.work_root.resolve()
        # Refuse an existing path before running a child or writing a log there.
        if root.exists():
            parser.error("--work-root must be a new directory")
        root.mkdir(parents=True, exist_ok=False)
    else:
        parent = args.work_parent.resolve()
        parent.mkdir(parents=True, exist_ok=True)
        root = Path(tempfile.mkdtemp(prefix="mesh-material-cli-", dir=parent))
    fixture = root / "black-materials.bnk"
    inputs = {"viewer_sha256": sha256(viewer), "fixture_writer_sha256": sha256(fixture_writer)}
    cases: list[dict[str, Any]] = []
    summary: dict[str, Any] = {
        "scope": "source-owned-mesh-material-cli-regressions",
        "work_root": str(root),
        "game_executed": False,
        "inputs": inputs,
        "warp": args.warp,
        "debug": args.debug,
        "cases": cases,
        "success": False,
    }

    def run_case(name: str, command: list[str]) -> subprocess.CompletedProcess[str]:
        directory = root / name
        directory.mkdir(exist_ok=False)
        write_json_new(directory / "command.json", command)
        result = subprocess.run(command, capture_output=True, text=True,
                                encoding="utf-8", errors="strict", timeout=60,
                                check=False)
        write_new(directory / "stdout.txt", result.stdout.encode("utf-8"))
        write_new(directory / "stderr.txt", result.stderr.encode("utf-8"))
        cases.append({"name": name, "returncode": result.returncode})
        return result

    size = 128

    def viewer_command(nodes: list[int], *, output: Path, stage: int | None = 0,
                       texture_member: int = 3, material_member: int = 2,
                       material_filter: str | None = None) -> list[str]:
        command = [str(viewer), "--bank", str(fixture), "--member", "1",
                   "--scene-member", "0", "--material-member", str(material_member),
                   "--texture-member", str(texture_member), "--headless",
                   "--size", str(size), "--seconds", "1", "--output-bmp", str(output)]
        if stage is not None:
            command += ["--texture-stage", str(stage)]
        for node in nodes:
            command += ["--node", str(node)]
        if material_filter is not None:
            command += ["--only-material", material_filter]
        if args.warp:
            command += ["--warp"]
        if args.debug:
            command += ["--debug"]
        return command

    try:
        created = parse_success(run_case("fixture-created", [str(fixture_writer), "--output", str(fixture)]), "fixture writer")
        require(created.get("scope") == "source-owned-black-material-cli-fixture" and
                created.get("game_executed") is False and created.get("members") == 4 and
                created.get("file_created") is True, "validated source-owned four-member bank created")
        require(created.get("node_a") == 1 and created.get("node_b") == 2 and
                created.get("material_a_hex") == MATERIAL_A and
                created.get("material_b_hex") == MATERIAL_B, "fixture keeps two post-NUL-distinct materials")
        original_fixture_hash = sha256(fixture)
        summary["fixture_sha256_before"] = original_fixture_hash

        def run_viewer(name: str, command: list[str]) -> subprocess.CompletedProcess[str]:
            result = run_case(name, command)
            require(sha256(fixture) == original_fixture_hash, f"{name} must preserve fixture bytes")
            return result

        refused_writer = run_case("fixture-refuses-overwrite", [str(fixture_writer), "--output", str(fixture)])
        require(refused_writer.returncode != 0 and sha256(fixture) == original_fixture_hash, "CREATE_NEW refuses fixture overwrite without changing bytes")

        first_bmp = root / "a-alone" / "image.bmp"
        first_command = viewer_command([1], output=first_bmp)
        first = parse_success(run_viewer("a-alone", first_command), "A alone")
        check_viewer_report(first, filtered=0, size=size)
        bitmap_is_rgb_black(first_bmp, size)
        first_bmp_hash = sha256(first_bmp)

        filtered_bmp = root / "a-b-filtered" / "image.bmp"
        filtered = parse_success(run_viewer("a-b-filtered", viewer_command(
            [1, 2], output=filtered_bmp, material_filter=MATERIAL_A)), "A plus distant B, filtered to A")
        check_viewer_report(filtered, filtered=1, size=size)
        bitmap_is_rgb_black(filtered_bmp, size)
        require(first["geometry_coverage_pixels"] == filtered["geometry_coverage_pixels"], "filtered distant B must not alter A's inspection-camera coverage")
        require(first["rendered_rgba_fnv1a64"] == filtered["rendered_rgba_fnv1a64"] and
                first_bmp_hash == sha256(filtered_bmp), "retained draws must produce identical requested pixels and BMP bytes")

        refused_output = run_viewer("viewer-refuses-overwrite", first_command)
        require(refused_output.returncode != 0 and sha256(first_bmp) == first_bmp_hash, "existing BMP must be refused and preserved")

        negatives = [
            ("inactive-stage", {"stage": 1}),
            ("missing-stage", {"stage": None}),
            ("wrong-texture-type", {"texture_member": 2}),
            ("unknown-material-filter", {"material_filter": "ffffffffffffffff"}),
        ]
        for name, override in negatives:
            image_path = root / name / "image.bmp"
            result = run_viewer(name, viewer_command([1], output=image_path, **override))
            require(result.returncode != 0 and not image_path.exists(), f"{name} must fail without synthesizing a texture, stage, material or BMP")

        require(sha256(viewer) == inputs["viewer_sha256"] and
                sha256(fixture_writer) == inputs["fixture_writer_sha256"], "input executables must stay unchanged throughout the batch")
        summary.update({
            "success": True,
            "fixture_sha256_after": sha256(fixture),
            "fixture_unchanged": True,
            "geometry_coverage_pixels": first["geometry_coverage_pixels"],
            "coverage_pixels": first["coverage_pixels"],
            "rendered_rgba_fnv1a64": first["rendered_rgba_fnv1a64"],
            "bmp_sha256": first_bmp_hash,
            "viewer_reports": {"a_alone": first, "a_b_filtered": filtered},
        })
    except (AssertionError, OSError, ValueError, subprocess.SubprocessError) as error:
        summary["error"] = str(error)
        if fixture.exists():
            summary["fixture_sha256_after"] = sha256(fixture)
        print(f"mesh material CLI regression failed: {error}", file=sys.stderr)
    write_json_new(root / "report.json", summary)
    print(json.dumps({"success": summary["success"], "cases": len(cases),
                      "report": str(root / "report.json")}, ensure_ascii=False))
    return 0 if summary["success"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
