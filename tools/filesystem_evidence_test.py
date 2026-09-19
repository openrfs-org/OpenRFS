#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import tempfile
from pathlib import Path
import unittest
from filesystem_evidence import validate_sweep


class SweepTests(unittest.TestCase):
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
