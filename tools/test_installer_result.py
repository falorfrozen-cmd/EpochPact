"""Elevated result-path validation; no administrator request or game access."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools import app_paths
import epochpact_desktop
from unittest.mock import Mock
from tools import player_setup


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

    def helper_args(self):
        return ['EpochPact.exe', '--install-plugin', '--game-exe', str(self.root / 'Last Epoch.exe'),
                '--install-result', str(self.output)]

    def test_helper_refuses_another_accounts_result_root_before_touching_game(self):
        with patch('sys.argv', self.helper_args()), \
             patch('epochpact_desktop.validate_installer_result', side_effect=ValueError('Invalid installer result path.')), \
             patch('tools.player_setup.validate_executable') as game, \
             patch('tools.le_session.cmd_install') as install:
            self.assertEqual(epochpact_desktop.installer(), app_paths.INSTALL_RESULT_INVALID)
        game.assert_not_called(); install.assert_not_called(); self.assertFalse(self.output.exists())

    def test_helper_receipt_creation_failure_returns_dedicated_exit_code(self):
        with patch('sys.argv', self.helper_args()), \
             patch('tools.player_setup.validate_executable', return_value=self.root / 'Last Epoch.exe'), \
             patch('tools.le_session.GAME'), patch('tools.le_session.cmd_install', return_value=0), \
             patch.object(Path, 'open', side_effect=PermissionError(13, 'denied', str(self.output))):
            self.assertEqual(epochpact_desktop.installer(), app_paths.INSTALL_RESULT_WRITE_FAILED)
        self.assertFalse(self.output.exists())

    def test_elevation_reads_actual_exit_code_and_closes_only_its_process_handle(self):
        shell = Mock()
        def launched(pointer):
            pointer._obj.hProcess = 123
            self.assertEqual(pointer._obj.lpVerb, 'runas')
            self.assertTrue(pointer._obj.lpDirectory)
            return True
        shell.ShellExecuteExW.side_effect = launched
        def exit_code(handle, pointer):
            self.assertEqual(handle, 123); pointer._obj.value = 20; return True
        with patch('tools.player_setup.ctypes.WinDLL', return_value=shell), \
             patch.object(player_setup.le.kernel32, 'WaitForSingleObject', return_value=0), \
             patch.object(player_setup.le.kernel32, 'GetExitCodeProcess', side_effect=exit_code), \
             patch.object(player_setup.le.kernel32, 'CloseHandle') as close:
            self.assertEqual(player_setup.run_elevated(['--install-plugin']), 20)
        close.assert_called_once_with(123)

    def test_pending_elevation_cannot_be_mistaken_for_a_finished_installer(self):
        shell = Mock()
        shell.ShellExecuteExW.side_effect = lambda pointer: setattr(pointer._obj, 'hProcess', 123) or True
        with patch('tools.player_setup.ctypes.WinDLL', return_value=shell), \
             patch.object(player_setup.le.kernel32, 'WaitForSingleObject', return_value=258), \
             patch.object(player_setup.le.kernel32, 'GetExitCodeProcess') as code, \
             patch.object(player_setup.le.kernel32, 'CloseHandle') as close:
            with self.assertRaisesRegex(RuntimeError, 'still running'):
                player_setup.run_elevated(['--install-plugin'])
        code.assert_not_called(); close.assert_called_once_with(123)


if __name__ == '__main__': unittest.main()
