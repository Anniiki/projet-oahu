"""Packaging boundaries, using generated PE headers and no game data."""
from pathlib import Path
from hashlib import sha256
import importlib.util
import json
import shutil
import stat
import struct
import subprocess
import tempfile
import unittest
from unittest import mock
import zipfile

MODULE = Path(__file__).resolve().parents[1] / "package_devtools.py"
SPEC = importlib.util.spec_from_file_location("oahu_package_tests", MODULE)
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)
PUBLISH_SPEC = importlib.util.spec_from_file_location(
    "oahu_publish_tests", MODULE.with_name("publish_release.py"))
PUBLISH = importlib.util.module_from_spec(PUBLISH_SPEC)
PUBLISH_SPEC.loader.exec_module(PUBLISH)


def synthetic_pe(machine=0x8664):
    data = bytearray(256)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 128)
    data[128:132] = b"PE\0\0"
    struct.pack_into("<H", data, 132, machine)
    return bytes(data)


class PackagingTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp(prefix="oahu-release-test-")).resolve()
        self.repo = self.directory / "repo"
        self.repo.mkdir()
        self.build = self.directory / "build"
        self.output = self.directory / "output"
        self.report = self.directory / "ctest.xml"
        self.report.write_text('<testsuite tests="2" failures="0" errors="0"/>',
                               encoding="utf-8")
        for name in PACKAGE.DOCUMENTS:
            (self.repo / name).write_text("Source-owned test document\n", encoding="utf-8")
        self.hooks = self.directory / "no-hooks"
        self.hooks.mkdir()
        self.git("init", "--quiet")
        self.git("add", ".")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "commit", "--quiet", "-m", "Fixture")
        for name, folder in PACKAGE.BINARIES.items():
            path = self.build / folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(synthetic_pe())

    def tearDown(self):
        parent = Path(tempfile.gettempdir()).resolve()
        if self.directory.parent != parent or not self.directory.name.startswith("oahu-release-test-"):
            raise RuntimeError("Refusing to remove a directory outside the named test scope")
        for path in self.directory.rglob("*"):
            if path.is_file() and path.resolve().is_relative_to(self.directory):
                path.chmod(stat.S_IREAD | stat.S_IWRITE)
        shutil.rmtree(self.directory)

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-c", "core.hooksPath=" + str(self.hooks), "-C", str(self.repo), *args],
            text=True, encoding="utf-8", stderr=subprocess.DEVNULL).strip()

    def package(self):
        return PACKAGE.package(self.repo, self.build, self.output, self.report)

    def test_allowlist_source_identity_and_hashes(self):
        (self.build / "foreign-game.bnk").write_bytes(b"unrelated fixture")
        manifest = self.package()
        self.assertEqual(manifest["source_commit"], self.git("rev-parse", "HEAD"))
        self.assertEqual(manifest["validation"]["ctest"]["tests"], 2)
        archive = next(self.output.glob("*.zip"))
        with zipfile.ZipFile(archive) as packed:
            self.assertEqual(set(packed.namelist()), PACKAGE.PACKAGE_FILES)
            self.assertNotIn("foreign-game.bnk", packed.namelist())
            for row in manifest["binaries"]:
                self.assertEqual(sha256(packed.read(row["file"])).hexdigest(), row["sha256"])
        self.assertEqual((self.output / "SHA256SUMS.txt").read_text("ascii").split()[0],
                         sha256(archive.read_bytes()).hexdigest())

    def test_refuses_untracked_source_and_existing_output(self):
        (self.repo / "untracked.txt").write_text("new", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.package()
        (self.repo / "untracked.txt").unlink()
        self.output.mkdir()
        (self.output / "keep.txt").write_text("keep", encoding="utf-8")
        with self.assertRaises(ValueError):
            self.package()
        self.assertEqual((self.output / "keep.txt").read_text("utf-8"), "keep")

    def test_refuses_wrong_architecture_and_profile_paths(self):
        with self.assertRaises(ValueError):
            PACKAGE.verify_binary(synthetic_pe(0x14C), "wrong.exe")
        with self.assertRaises(ValueError):
            PACKAGE.verify_binary(b"MZ", "truncated.exe")
        for suffix in (b"C:\\Users\\Example\\private.pdb",
                       "C:\\Users\\Example\\private.pdb".encode("utf-16le")):
            with self.assertRaises(ValueError):
                PACKAGE.verify_binary(synthetic_pe() + suffix, "profile.exe")

    def test_refuses_empty_or_failed_test_reports(self):
        for text in ('<testsuite tests="0" failures="0"/>',
                     '<testsuite tests="2" failures="1"/>',
                     '<testsuite tests="2" failures="0"><testcase><error/></testcase></testsuite>'):
            self.report.write_text(text, encoding="utf-8")
            with self.assertRaises(ValueError):
                self.package()
        self.assertFalse(self.output.exists())

    def test_publisher_refuses_tampering_before_network(self):
        manifest = self.package()
        archive = next(self.output.glob("*.zip"))
        archive.write_bytes(archive.read_bytes() + b"tampered")
        with mock.patch.object(PUBLISH, "gh") as api:
            with self.assertRaises(ValueError):
                PUBLISH.publish("Example/project", manifest["source_commit"], self.output)
            api.assert_not_called()

    def test_existing_release_keeps_its_assets(self):
        manifest = self.package()
        responses = [json.dumps({"tag_name": manifest["tag"]}),
                     json.dumps({"object": {"type": "commit",
                                             "sha": manifest["source_commit"]}})]
        with mock.patch.object(PUBLISH, "gh", side_effect=responses) as api:
            PUBLISH.publish("Example/project", manifest["source_commit"], self.output)
            self.assertEqual(api.call_count, 2)
            self.assertTrue(all(call.args[0] == "api" for call in api.call_args_list))


if __name__ == "__main__":
    unittest.main()
