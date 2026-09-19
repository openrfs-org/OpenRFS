#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import json
import tempfile
from unittest.mock import patch
from pathlib import Path
import unittest
from filesystem_evidence import source, validate_sweep


class SweepTests(unittest.TestCase):
    def test_source_manifest_hash_matches_uploaded_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / 'source.json'
            with patch('filesystem_evidence.subprocess.run'), \
                 patch('filesystem_evidence.command', side_effect=['commit', 'tree', '100644 blob abc\tfile']), \
                 patch('filesystem_evidence.scenarios', return_value=['first']), \
                 patch.dict('filesystem_evidence.os.environ', {}, clear=True):
                source(path)
            recorded = json.loads(path.read_text())
            self.assertEqual(recorded['git_tree_manifest_sha256'],
                hashlib.sha256(path.with_suffix('.tree.txt').read_bytes()).hexdigest())

    def test_missing_duplicate_and_incomplete_sweeps_refuse(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('first', 'last'):
                (root / name).mkdir()
                (root / name / 'serial.log').write_text('guest evidence\n')
            text = ('QEMU scenario first passed\nQEMU scenario last passed\n'
                    'all deterministic QEMU scenarios passed\n')
            validate_sweep(['first', 'last'], text, root)
            for broken in (text.replace('QEMU scenario first passed\n', ''),
                           text + 'QEMU scenario first passed\n',
                           text.replace('all deterministic QEMU scenarios passed\n', '')):
                with self.assertRaises(RuntimeError):
                    validate_sweep(['first', 'last'], broken, root)
            (root / 'first/serial.log').unlink()
            with self.assertRaises(RuntimeError):
                validate_sweep(['first', 'last'], text, root)
            (root / 'first/serial.log').touch()
            with self.assertRaises(RuntimeError):
                validate_sweep(['first', 'last'], text, root)


if __name__ == '__main__':
    unittest.main()
