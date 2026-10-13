"""Support report contents, masking and the explicit Game setup action."""
import json
import os
import struct
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
from unittest.mock import patch, Mock

from tools import support_report, le_session as le
from tools.player_setup import PlayerSetup
from tools.ui_bridge import UiBridge
from tools.test_player_setup import game


class ReportTests(unittest.TestCase):
    def test_window_capture_restores_thread_dpi_context_even_on_failure(self):
        change = Mock(return_value=123)
        with patch.object(support_report.sys, 'platform', 'win32'), patch.object(support_report.ctypes, 'WinDLL', return_value=Mock(SetThreadDpiAwarenessContext=change)), patch.object(support_report, '_capture_window', side_effect=RuntimeError('capture failed')):
            with self.assertRaisesRegex(RuntimeError, 'capture failed'):
                support_report.capture_window(99)
        self.assertEqual(change.call_count, 2)
        self.assertEqual(change.call_args_list[0].args[0].value, support_report.wt.HANDLE(-4).value)
        self.assertEqual(change.call_args_list[1].args[0], 123)

    def test_window_capture_keeps_compatibility_without_thread_dpi_api(self):
        with patch.object(support_report.sys, 'platform', 'win32'), patch.object(support_report.ctypes, 'WinDLL', return_value=Mock(SetThreadDpiAwarenessContext=None)), patch.object(support_report, '_capture_window', return_value=b'png') as capture:
            self.assertEqual(support_report.capture_window(99), b'png')
            capture.assert_called_once_with(99)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.user = self.root / 'user'
        patcher = patch.dict(os.environ, {'EPOCHPACT_USER_DIR': str(self.user), 'USERNAME': 'Ayse', 'COMPUTERNAME': 'DESKTOP-AYSE'})
        patcher.start(); self.addCleanup(patcher.stop)
        self.game = self.root / 'Last Epoch'
        (self.game / 'EpochPact/logs').mkdir(parents=True)
        (self.game / 'EpochPact/logs/core.log').write_text(
            f'loaded {Path.home()}\\Saves for Ayse on DESKTOP-AYSE\ncommand: loadname Hero\n', encoding='utf-8')
        (self.game / 'EpochPact/workshop-recipes.txt').write_text('private recipes')
        (self.game / 'EpochPact/logs/loader.log').write_text('loader started\n')

    def read(self, path):
        with zipfile.ZipFile(path) as z:
            return {n: z.read(n) for n in z.namelist()}

    def test_masks_user_and_computer_names_and_profile_paths(self):
        path = support_report.create(self.game, {'installed': True, 'executable': str(self.game / 'Last Epoch.exe')}, 'Crash when Ayse opened stash')
        files = self.read(path)
        text = b''.join(files.values()).decode('utf-8', 'replace')
        self.assertNotIn('Ayse', text); self.assertNotIn('DESKTOP-AYSE', text); self.assertNotIn(str(Path.home()), text)
        self.assertIn('%USERPROFILE%', files['game-logs/core.log'].decode())
        self.assertIn('Hero', files['game-logs/core.log'].decode(), 'character names stay for support')
        summary = json.loads(files['summary.json'])
        self.assertNotIn('executable', summary['setup'])
        self.assertEqual(files['description.txt'].decode().strip(), 'Crash when <user> opened stash')

    def test_never_includes_saves_recipes_or_game_files(self):
        names = set(self.read(support_report.create(self.game, {}, '')))
        self.assertTrue(names <= {'game-logs/core.log', 'game-logs/loader.log', 'game-logs/installed-build.json',
                                  'app-logs/startup.log', 'app-logs/operations.log', 'summary.json', 'description.txt', 'screenshot.png'})
        self.assertNotIn('description.txt', names, 'an empty description adds no file')

    def test_long_logs_keep_only_their_end(self):
        (self.game / 'EpochPact/logs/core.log').write_text('old\n' * 300000 + 'newest line\n')
        text = self.read(support_report.create(self.game, {}, ''))['game-logs/core.log'].decode()
        self.assertTrue(text.startswith('[first ') and text.rstrip().endswith('newest line'))
        self.assertLessEqual(len(text), support_report.LOG_LIMIT + 100)

    def test_png_encoder_produces_a_valid_image(self):
        png = support_report._png(2, 1, bytes([0, 0, 255, 255, 0, 255, 0, 255]))
        self.assertTrue(png.startswith(b'\x89PNG\r\n\x1a\n'))
        width, height = struct.unpack('>II', png[16:24]); self.assertEqual((width, height), (2, 1))
        idat = png[png.index(b'IDAT') + 4:png.index(b'IEND') - 8]
        self.assertEqual(zlib.decompress(idat), bytes([0, 255, 0, 0, 0, 255, 0]))

    def test_screenshot_is_optional_and_missing_window_is_harmless(self):
        names = set(self.read(support_report.create(self.game, {}, 'x', screenshot=True, window=lambda: None)))
        self.assertNotIn('screenshot.png', names)
        with patch.object(support_report, 'capture_window', return_value=b'\x89PNG fake'):
            names = set(self.read(support_report.create(self.game, {}, 'x', screenshot=True, window=lambda: 5)))
        self.assertIn('screenshot.png', names)

    def test_setup_action_writes_a_report_and_reveals_its_folder(self):
        exe = game(self.root, 'Install')
        setup = PlayerSetup(UiBridge(), config_path=self.root / 'settings.json')
        with patch.object(le, 'GAME', le.GAME), patch.object(le, 'game_pids', return_value=[]), \
                patch.object(le, 'require_supported_build', return_value={}):
            setup.select(str(exe))
            shown = []
            setup.reveal = shown.append
            result = setup.execute('report', {'description': 'Stash all moved nothing', 'screenshot': False})
        self.assertTrue(result['ok']); self.assertTrue(result['report'].startswith('EpochPact-report-'))
        # Saved next to the in-game hotkey reports, in the game folder.
        self.assertEqual(shown, [exe.parent / 'EpochPact' / 'reports'])
        self.assertTrue((exe.parent / 'EpochPact' / 'reports' / result['report']).is_file())
        with self.assertRaises(ValueError):
            setup.execute('report', {'description': 'x' * 5000})


if __name__ == '__main__':
    unittest.main()
