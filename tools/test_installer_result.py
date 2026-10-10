"""Elevated result-path validation; no administrator request or game access."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools import app_paths


class ResultPathTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / ('setup-install-' + 'a' * 32 + '.json')
        p = patch.object(app_paths, 'installer_result_root', return_value=self.root)
        p.start(); self.addCleanup(p.stop)

    def test_fresh_result_in_exact_os_root_is_accepted(self):
        self.assertEqual(app_paths.validate_installer_result(str(self.output)), self.output)

    def test_existing_result_cannot_be_overwritten(self):
        self.output.write_text('keep this')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            app_paths.validate_installer_result(str(self.output))
        self.assertEqual(self.output.read_text(), 'keep this')

    def test_environment_override_does_not_authorize_another_output_folder(self):
        other = self.root / 'override'
        with patch.dict(os.environ, {'EPOCHPACT_USER_DIR': str(other), 'LOCALAPPDATA': str(other)}):
            with self.assertRaisesRegex(ValueError, 'Invalid'):
                app_paths.validate_installer_result(str(other / self.output.name))

    def test_junction_in_output_ancestors_is_rejected(self):
        with patch.object(Path, 'is_junction', lambda p: p == self.root):
            with self.assertRaisesRegex(ValueError, 'Linked'):
                app_paths.validate_installer_result(str(self.output))

    def test_traversal_and_arbitrary_filename_are_rejected(self):
        for path in (self.root / '..' / self.output.name, self.root / 'settings.json', Path(self.output.name)):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, 'Invalid'):
                app_paths.validate_installer_result(str(path))


if __name__ == '__main__': unittest.main()
