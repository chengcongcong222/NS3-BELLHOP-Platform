import tempfile
import unittest
import shutil
from pathlib import Path
from compile_catalog import compile_packages, load_package

ROOT = Path(__file__).parent


class CatalogTests(unittest.TestCase):
    def test_v2_metadata_and_complete_content_identity(self):
        original = ROOT / "examples/channel_aware"
        package = load_package(original)
        self.assertEqual(package["knowledge"], "global-simulation")
        self.assertEqual(package["output"], "serial-flow-schedule-v1")
        generated = compile_packages([ROOT / "adaptive_tdma", original])
        self.assertIn("FindFlowAlgorithm", generated)
        self.assertIn("&example_channel_aware::Plan", generated)
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / "copy"
            shutil.copytree(original, directory)
            self.assertEqual(package["package_sha256"], load_package(directory)["package_sha256"])
            xml = directory / "algorithm.xml"
            source = xml.read_text()
            xml.write_text(source.replace('id="example.channel-aware" version="1.0.0"', 'version="1.0.0" id="example.channel-aware"'))
            self.assertEqual(package["package_sha256"], load_package(directory)["package_sha256"])
            (directory / "table.dat").write_bytes(b"resource")
            self.assertNotEqual(package["package_sha256"], load_package(directory)["package_sha256"])
            xml.write_text(source.replace('knowledge="global-simulation"', 'knowledge="future"'))
            with self.assertRaises(ValueError): load_package(directory)
            xml.write_text(source.replace('name="routing-view-v1"', 'name="node-state-v1"'))
            with self.assertRaises(ValueError): load_package(directory)
            xml.write_text(source.replace('interface="planning-mac-v2"', 'interface="planning-mac-v1"'))
            with self.assertRaises(ValueError): load_package(directory)

    def test_multi_source_identity_and_path_rejection(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp) / "package"
            shutil.copytree(ROOT / "examples/ordered_mac", directory)
            previous = load_package(directory)["package_sha256"]
            (directory / "src").mkdir()
            source = directory / "src/helper.cpp"
            source.write_text("int package_helper() { return 1; }\n")
            package = load_package(directory)
            self.assertNotEqual(previous, package["package_sha256"])
            self.assertEqual(package["sources"], [str(source)])
            source.write_text("int package_helper() { return 2; }\n")
            self.assertNotEqual(package["package_sha256"], load_package(directory)["package_sha256"])
            source.unlink()
            source.symlink_to(Path(temp) / "external.cpp")
            (Path(temp) / "external.cpp").write_text("int external;\n")
            with self.assertRaises(ValueError):
                load_package(directory)

    def test_builtin_and_external_package(self):
        first = compile_packages([ROOT / "adaptive_tdma", ROOT / "examples/ordered_mac"])
        self.assertEqual(first, compile_packages([ROOT / "examples/ordered_mac", ROOT / "adaptive_tdma"]))
        self.assertIn("&example_mac::Build", first)
        with self.assertRaises(ValueError):
            compile_packages([ROOT / "adaptive_tdma", ROOT / "adaptive_tdma"])

    def test_strict_xml(self):
        source = (ROOT / "adaptive_tdma/algorithm.xml").read_text()
        invalid = [source.replace('default="0"', 'default="-1"'), source.replace('type="duration_ns"', 'type="eval"'),
                   source.replace('<description>', '<script>'), source.replace('advanced="true"', 'advanced="maybe"'),
                   '<!DOCTYPE algorithm [<!ENTITY x SYSTEM "file:///etc/passwd">]>' + source,
                   source.replace('min="0"', 'min="-1"'), source.replace('min="0"', 'min="01"'),
                   source.replace('interface="planning-mac-v1"', 'interface="future"'),
                   source.replace('</algorithm>', '<unknown/></algorithm>')]
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / "algorithm.hpp").write_text("// trusted test source")
            for text in invalid:
                with self.subTest(text=text):
                    (directory / "algorithm.xml").write_text(text)
                    with self.assertRaises((ValueError, SyntaxError)):
                        load_package(directory)
            (directory / "algorithm.xml").write_text(source)
            first = load_package(directory)["package_sha256"]
            (directory / "algorithm.hpp").write_text("// changed implementation")
            self.assertNotEqual(first, load_package(directory)["package_sha256"])


if __name__ == "__main__":
    unittest.main()
