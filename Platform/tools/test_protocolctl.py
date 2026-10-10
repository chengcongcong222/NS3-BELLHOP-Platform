"""Real compiler checks plus publication/activation failure-isolation gates."""
import argparse
import contextlib
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

import protocolctl as tool
from algorithm_installation import InstallationLease


class ProtocolctlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.package = self.root / "package"
        shutil.copytree(tool.PLATFORM / "algorithms/user/integration_probe", self.package)
        self.settings = {"schema": "platform.protocolctl.v1", "cxx": shutil.which("c++"),
            "build_root": str(self.root / "build"), "install_root": str(self.root / "installed"),
            "ns3_prefix": "/unused-in-unit-test", "jobs": 1}
        self.config = self.root / "config.json"
        tool.atomic_json(self.config, self.settings)

    def installation(self):
        root = Path(self.settings["install_root"])
        root.mkdir()
        candidate = root / "candidate-worker"
        candidate.write_bytes(b"test fixture - never launched")
        identity = tool.digest(candidate)
        directory = root / "releases" / identity
        directory.mkdir(parents=True)
        candidate.rename(directory / "platform_sim_worker")
        tool.atomic_json(directory / "manifest.json", {"schema": "platform.algorithm-installation.v1",
            "worker_sha256": identity, "dependencies": [], "packages": []})
        tool.atomic_json(root / "active.json", {"release": identity})
        tool.atomic_json(root / "pending.json", {"release": identity})
        return root, identity

    def test_compile_and_run_multifile_sample(self):
        (self.package / "src").mkdir()
        (self.package / "include").mkdir()
        (self.package / "include/helper.hpp").write_text("int helper();\n")
        (self.package / "src/helper.cpp").write_text('#include "helper.hpp"\nint helper() { return 42; }\n')
        (self.package / "tests/helper.cpp").write_text('#include "helper.hpp"\nint main() { return helper()==42 ? 0 : 1; }\n')
        result = tool.check(self.package, self.settings, self.root / "check")
        self.assertEqual(len(result["sources"]), 1)

    def test_generated_v2_package_compiles_and_has_behavior_test(self):
        generated = self.root / "generated_v2"
        args = argparse.Namespace(name="generated_v2", interface="planning-mac-v2")
        with patch.object(tool, "package_path", return_value=generated), contextlib.redirect_stdout(io.StringIO()):
            tool.new_package(args)
        result = tool.check(generated, self.settings, self.root / "check-v2")
        self.assertEqual(result["interface"], "planning-mac-v2")
        self.assertEqual(result["id"], "user.generated_v2")

    def test_failed_compile_keeps_active_installation(self):
        root, identity = self.installation()
        before = (root / "active.json").read_bytes()
        (self.package / "algorithm.hpp").write_text("#error intentional broken package\n")
        args = argparse.Namespace(config=self.config, name="broken", activate=True)
        with patch.object(tool, "package_path", return_value=self.package):
            with self.assertRaisesRegex(ValueError, "构建失败"):
                tool.install_package(args)
        self.assertEqual(before, (root / "active.json").read_bytes())
        self.assertEqual([identity], [p.name for p in (root / "releases").iterdir()])

    def test_conflicting_version_and_active_service_are_not_overwritten(self):
        root, identity = self.installation()
        path = root / "releases" / identity / "manifest.json"
        manifest = json.loads(path.read_text())
        metadata = tool.inspect_package(self.package)
        # Other retained version, not active, still reserves the package identity.
        other = root / "releases" / ("f" * 64)
        other.mkdir()
        tool.atomic_json(other / "manifest.json", {"packages": [{"id": metadata["id"],
            "version": metadata["version"], "package_sha256": "0" * 64}]})
        args = argparse.Namespace(config=self.config, name="conflict", activate=True, release=identity)
        with patch.object(tool, "package_path", return_value=self.package):
            with self.assertRaisesRegex(ValueError, "升级版本"):
                tool.install_package(args)
        before = (root / "active.json").read_bytes()
        with InstallationLease(root), contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(tool.activate_release(args), 3)
            self.assertIn("ActivationBlocked", output.getvalue())
        self.assertEqual(before, (root / "active.json").read_bytes())

    def fake_build(self, command, **kwargs):
        """Publication unit fixture; actual cumulative worker build is D02."""
        command = list(map(str, command))
        if command[:2] == ["cmake", "--build"]:
            build = Path(command[2])
            worker = build / "worker/platform_sim_worker"
            worker.parent.mkdir(parents=True)
            packages = [tool.inspect_package(p) for p in sorted((build.parent / "packages").iterdir())]
            worker.write_text(json.dumps({"algorithms": packages}, sort_keys=True))
        if "--algorithm-catalog" in command:
            return Path(command[0]).read_text()
        return "fixture-toolchain"

    def install_fixture(self, package):
        args = argparse.Namespace(config=self.config, name="fixture", activate=False)
        with patch.object(tool, "package_path", return_value=package), \
             patch.object(tool, "check", side_effect=lambda p, *a: tool.inspect_package(p)), \
             patch.object(tool, "run", side_effect=self.fake_build), \
             patch.object(tool, "dependency_manifest", return_value=[]), \
             contextlib.redirect_stdout(io.StringIO()):
            tool.install_package(args)
        root = Path(self.settings["install_root"])
        identity = json.loads((root / "pending.json").read_text())["release"]
        return identity, tool.read_release(root, identity)[1]

    def test_sequential_installs_accumulate_with_and_without_active(self):
        for with_active in (False, True):
            with self.subTest(with_active=with_active):
                root = self.root / ("active-case" if with_active else "empty-case")
                self.settings["install_root"] = str(root)
                tool.atomic_json(self.config, self.settings)
                if with_active:
                    self.installation()
                first, _ = self.install_fixture(self.package)
                second_package = self.root / ("package-b-" + str(with_active))
                shutil.copytree(self.package, second_package)
                xml = second_package / "algorithm.xml"
                old_id = tool.inspect_package(self.package)["id"]
                xml.write_text(xml.read_text().replace(old_id, "user.closeout_b"))
                second, manifest = self.install_fixture(second_package)
                self.assertNotEqual(first, second)
                self.assertEqual({old_id, "user.closeout_b"}, {p["id"] for p in manifest["packages"]})
                # An unrelated historical activation must preserve the candidate.
                args = argparse.Namespace(config=self.config, release=first)
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(tool.activate_release(args), 0)
                self.assertEqual(second, json.loads((root / "pending.json").read_text())["release"])
                args.release = second
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(tool.activate_release(args), 0)
                self.assertFalse((root / "pending.json").exists())
                self.assertEqual(second, json.loads((root / "active.json").read_text())["release"])

    def test_bad_pending_never_falls_back(self):
        root, _ = self.installation()
        tool.atomic_json(root / "pending.json", {"release": "a" * 64})
        before = {p: (root / p).read_bytes() for p in ("active.json", "pending.json")}
        with self.assertRaises((ValueError, OSError)):
            self.install_fixture(self.package)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})

    def test_publication_failure_preserves_pointers(self):
        root, _ = self.installation()
        before = {p: (root / p).read_bytes() for p in ("active.json", "pending.json")}
        with patch.object(tool.os, "rename", side_effect=OSError("publication failed")):
            with self.assertRaisesRegex(OSError, "publication failed"):
                self.install_fixture(self.package)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})

    def test_damaged_pending_package_is_not_replaced_by_developer_source(self):
        root, _ = self.installation()
        identity, manifest = self.install_fixture(self.package)
        installed = root / "releases" / identity / "packages" / manifest["packages"][0]["directory"]
        (installed / "algorithm.hpp").write_text("// damaged retained source\n")
        before = {p: (root / p).read_bytes() for p in ("active.json", "pending.json")}
        with self.assertRaisesRegex(ValueError, "源码已变化"):
            self.install_fixture(self.package)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})

    def test_failed_activation_preserves_pending(self):
        root, _ = self.installation()
        pending, _ = self.install_fixture(self.package)
        before = {p: (root / p).read_bytes() for p in ("active.json", "pending.json")}
        args = argparse.Namespace(config=self.config, release=pending)
        with InstallationLease(root), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(tool.activate_release(args), 3)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})
        args.release = "b" * 64
        with self.assertRaises((ValueError, OSError)):
            tool.activate_release(args)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})

    def test_second_compile_failure_preserves_pending_and_active(self):
        root, _ = self.installation()
        first, _ = self.install_fixture(self.package)
        before = {p: (root / p).read_bytes() for p in ("active.json", "pending.json")}
        args = argparse.Namespace(config=self.config, name="broken", activate=False)
        with patch.object(tool, "package_path", return_value=self.package), \
             patch.object(tool, "check", side_effect=ValueError("构建失败")):
            with self.assertRaisesRegex(ValueError, "构建失败"):
                tool.install_package(args)
        self.assertEqual(before, {p: (root / p).read_bytes() for p in before})
        self.assertEqual(first, json.loads((root / "pending.json").read_text())["release"])

    def test_list_distinguishes_candidate_history_and_equal_pointer(self):
        root, active = self.installation()
        pending, _ = self.install_fixture(self.package)
        catalog = json.dumps({"algorithms": [{"id": "fixture", "version": "1", "interface": "v2", "package_sha256": "sha"}]})
        args = argparse.Namespace(config=self.config)
        with patch.object(tool, "run", return_value=catalog), contextlib.redirect_stdout(io.StringIO()) as output:
            tool.list_packages(args)
        self.assertIn("Active fixture", output.getvalue())
        self.assertIn("InstalledPendingActivation fixture", output.getvalue())
        tool.atomic_json(root / "active.json", {"release": pending})
        with patch.object(tool, "run", return_value=catalog), contextlib.redirect_stdout(io.StringIO()) as output:
            tool.list_packages(args)
        self.assertNotIn("InstalledPendingActivation", output.getvalue())
        self.assertIn("Retained fixture", output.getvalue())

    def test_symlink_and_custom_build_scripts_rejected(self):
        (self.package / "CMakeLists.txt").write_text("execute_process(COMMAND false)\n")
        with self.assertRaisesRegex(ValueError, "构建脚本"):
            tool.inspect_package(self.package)
        (self.package / "CMakeLists.txt").unlink()
        (self.package / "external.hpp").symlink_to(self.root / "outside")
        with self.assertRaisesRegex(ValueError, "符号链接"):
            tool.inspect_package(self.package)


if __name__ == "__main__":
    unittest.main()
