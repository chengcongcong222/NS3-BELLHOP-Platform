from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/restore_typical_workspace.py'
spec = importlib.util.spec_from_file_location('typical_restore', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class RestoreTests(unittest.TestCase):
    def fixture(self, root):
        seed = root / 'seed'
        path = seed / 'data/workspace/drafts/test.json'
        path.parent.mkdir(parents=True)
        path.write_text('{"path":"${WORKSPACE}/builds/example"}')
        (seed / 'manifest.json').write_text(json.dumps({
            'schema': 1, 'sha256': {'workspace/drafts/test.json':
                hashlib.sha256(path.read_bytes()).hexdigest()}}))
        return seed, path

    def test_restore_and_escaped_relocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed, _ = self.fixture(root)
            target = root / 'target "with quotes"'
            module.restore(seed, target)
            value = json.loads((target / 'workspace/drafts/test.json').read_text())
            self.assertEqual(value['path'], str(target / 'workspace/builds/example'))

    def test_refuse_existing_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed, _ = self.fixture(root)
            target = root / 'target'
            target.mkdir()
            (target / 'user.txt').write_text('preserve')
            with self.assertRaises(ValueError): module.restore(seed, target)
            self.assertEqual((target / 'user.txt').read_text(), 'preserve')

    def test_corrupt_seed_does_not_create_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed, path = self.fixture(root)
            path.write_text('corrupt')
            with self.assertRaises(ValueError): module.restore(seed, root / 'target')
            self.assertFalse((root / 'target').exists())

    def test_traversal_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed, _ = self.fixture(root)
            (seed / 'manifest.json').write_text(json.dumps({'sha256': {'../outside': 'bad'}}))
            with self.assertRaises(ValueError): module.restore(seed, root / 'target')

    def test_shipped_seed_is_intact(self):
        with tempfile.TemporaryDirectory() as directory:
            seed = SCRIPT.parents[1] / 'release/typical-system'
            target = Path(directory) / 'restored'
            module.restore(seed, target)
            self.assertEqual(len(list((target / 'environments').iterdir())), 2)
            self.assertTrue((target / 'workspace/drafts/experiment/experiment-7c33c1817ed9.json').is_file())


if __name__ == '__main__':
    unittest.main()
