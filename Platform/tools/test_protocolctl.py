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
            tool.activate_release(args)
            self.assertIn("InstalledPendingActivation", output.getvalue())
        self.assertEqual(before, (root / "active.json").read_bytes())

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
