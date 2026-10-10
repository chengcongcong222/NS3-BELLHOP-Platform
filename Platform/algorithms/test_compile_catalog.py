import tempfile
import unittest
from pathlib import Path
from compile_catalog import compile_packages, load_package

ROOT = Path(__file__).parent


class CatalogTests(unittest.TestCase):
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
