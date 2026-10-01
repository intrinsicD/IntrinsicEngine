#!/usr/bin/env python3
"""Exercise the shutdown runner's driver selection before any GPU process starts."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
RUNNER = ROOT / 'tests/regression/graphics/Run.VulkanShutdownLsanContract.cmake'


class DriverSelectionTests(unittest.TestCase):
    def test_selection_and_precedence(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            manifest = directory / 'driver with spaces.json'
            manifest.write_text(json.dumps({'ICD': {'library_path': 'test-driver.so'}}))
            malformed = directory / 'invalid.json'
            malformed.write_text('{}')
            probe = directory / 'probe'
            probe.write_text('#!/bin/sh\necho NEGATIVE_CONTROL_REACHED\nexit 1\n')
            probe.chmod(0o755)
            cases = [
                ({}, False),
                ({'VK_DRIVER_FILES': str(directory / 'missing.json')}, False),
                ({'VK_DRIVER_FILES': str(directory)}, False),
                ({'VK_DRIVER_FILES': 'relative.json'}, False),
                ({'VK_DRIVER_FILES': f'{manifest}:{manifest}'}, False),
                ({'VK_DRIVER_FILES': str(malformed)}, False),
                ({'VK_DRIVER_FILES': str(manifest)}, True),
                ({'VK_ICD_FILENAMES': str(manifest)}, True),
                ({'VK_DRIVER_FILES': '', 'VK_ICD_FILENAMES': str(manifest)}, True),
                ({'VK_DRIVER_FILES': str(malformed), 'VK_ICD_FILENAMES': str(manifest)}, False),
                ({'VK_DRIVER_FILES': str(manifest), 'VK_ICD_FILENAMES': 'ignored'}, True),
            ]
            for selection, accepted in cases:
                with self.subTest(selection=selection):
                    env = {k: v for k, v in os.environ.items()
                           if k not in {'VK_DRIVER_FILES', 'VK_ICD_FILENAMES'}}
                    env.update(selection)
                    result = subprocess.run([
                        'cmake', f'-DSANDBOX_EXE={probe}', f'-DLEAK_CONTROL_EXE={probe}',
                        f'-DREPORT_PATH={directory / "report.json"}',
                        f'-DSUPPRESSIONS_PATH={ROOT / "lsan.supp"}', '-DSYMBOLIZER_PATH=unused',
                        '-P', str(RUNNER),
                    ], env=env, capture_output=True, text=True, timeout=10)
                    output = result.stdout + result.stderr
                    self.assertNotEqual(result.returncode, 0)  # no fake passing leak gate
                    self.assertEqual('NEGATIVE_CONTROL_REACHED' in output, accepted, output)
                    if not accepted:
                        self.assertIn('BUG-221:', output)


if __name__ == '__main__':
    unittest.main()
