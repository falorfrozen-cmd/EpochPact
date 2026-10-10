"""First-run selection, installer refusals, frozen paths and HTTP queue boundaries."""
import argparse
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

from tools import app_paths, le_session as le, progression_backend as progression
from tools.player_setup import DesktopDialogs, PlayerSetup, picker_directory, validate_executable
from tools.ui_bridge import UiBridge
from epochpact_ui import create_app, interface_server


def game(root, name='Custom install ü'):
    directory = root / name
    directory.mkdir()
    (directory / le.EXE).write_bytes(b'MZgame')
    (directory / 'GameAssembly.dll').touch()
    (directory / 'Last Epoch_Data').mkdir()
    return directory / le.EXE


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.exe = game(self.root)
        self.bridge = UiBridge()
        self.setup = PlayerSetup(self.bridge, config_path=self.root / 'preferences/settings.json')
        self.patches = [patch.object(le, 'GAME', le.GAME), patch.object(progression, 'BACKUPS', progression.BACKUPS),
                        patch.object(le, 'require_supported_build', return_value={'gameVersion':'fixture'}),
                        patch.object(le, 'game_pids', return_value=[])]
        for p in self.patches:
            p.start(); self.addCleanup(p.stop)

    def test_first_run_does_not_choose_steam_or_write_anything(self):
        self.assertFalse(self.setup.status()['selected'])
        self.assertFalse(self.setup.config_path.exists())
        with self.assertRaises(RuntimeError): self.setup.require_ready()
        self.assertFalse((self.exe.parent / 'EpochPact').exists())

    def test_custom_path_is_persisted_and_reloaded_with_dynamic_recovery_boundary(self):
        self.bridge.actor = {'old-character': {}}
        self.bridge.history = [{'backup':'old-game'}]
        self.setup.select(str(self.exe))
        self.assertEqual(le.GAME, self.exe.parent)
        self.assertEqual(progression.BACKUPS, self.exe.parent / 'EpochPact/backups/progression')
        self.assertEqual(self.bridge.actor, {})
        self.assertEqual(self.bridge.history, [])
        reloaded = PlayerSetup(UiBridge(), config_path=self.setup.config_path)
        self.assertEqual(reloaded.executable, self.exe)
        moved = self.exe.with_name('moved.exe'); self.exe.rename(moved)
        missing = PlayerSetup(UiBridge(), config_path=self.setup.config_path)
        self.assertIsNone(missing.executable)
        self.assertIn('unavailable', missing.status()['problem'])

    def test_wrong_executable_and_incomplete_game_refused_without_persisting(self):
        other = self.exe.with_name('launcher.exe'); other.write_bytes(b'MZ')
        for candidate in (str(other), 'Last Epoch.exe', str(self.root), ''):
            with self.assertRaises((OSError, ValueError)): self.setup.select(candidate)
        (self.exe.parent / 'GameAssembly.dll').unlink()
        with self.assertRaises(ValueError): validate_executable(str(self.exe))
        self.assertFalse(self.setup.config_path.exists())

    def test_game_running_blocks_location_change_and_install(self):
        self.setup.select(str(self.exe))
        other = game(self.root, 'Other install')
        with patch.object(le, 'game_pids', return_value=[123]):
            with self.assertRaises(RuntimeError): self.setup.select(str(other))
            with self.assertRaises(RuntimeError): self.setup.install()
        self.assertEqual(self.setup.executable, self.exe)
        self.assertFalse((self.exe.parent / 'version.dll').exists())

    def test_cancelled_picker_returns_none_and_starts_in_steam(self):
        steam = self.root / 'Steam'; steam.mkdir()
        dialogs = DesktopDialogs()
        from unittest.mock import Mock
        dialogs._window = Mock()
        dialogs._window.create_file_dialog.return_value = None
        with patch.object(le, '_steam_root', return_value=steam):
            self.assertEqual(picker_directory(), steam)
            self.assertIsNone(dialogs.choose_game_executable())
        self.assertEqual(dialogs._window.create_file_dialog.call_args.kwargs['directory'], str(steam))
        self.assertFalse(dialogs._window.create_file_dialog.call_args.kwargs['allow_multiple'])
        self.assertEqual([x for x in vars(dialogs) if not x.startswith('_')], [])

    def test_foreign_loader_is_preserved_and_launch_refuses_uninstalled_mod(self):
        self.setup.select(str(self.exe))
        dll = self.exe.parent / 'version.dll'; dll.write_bytes(b'other loader')
        with self.assertRaisesRegex(RuntimeError, 'not EpochPact'): self.setup.install()
        self.assertEqual(dll.read_bytes(), b'other loader')
        with patch.object(le, 'cmd_launch') as launch:
            with self.assertRaises(RuntimeError): self.setup.launch()
            launch.assert_not_called()

    def test_actual_install_in_custom_folder_verifies_player_dll(self):
        self.setup.select(str(self.exe))
        self.assertTrue(self.setup.install()['launcher']['installed'])
        self.assertEqual((self.exe.parent / 'EpochPact/EpochPact.Core.dll').read_bytes(), le.build_artifact()[0].read_bytes())
        record = json.loads((self.exe.parent / 'EpochPact/installed-build.json').read_text())
        self.assertEqual(record['flavor'], 'player')
        self.assertFalse((self.exe.parent / 'EpochPact/ipc').exists())
        self.assertFalse((self.exe.parent / 'EpochPact/dump').exists())

    def test_failed_staging_keeps_every_installed_file_unchanged(self):
        self.setup.select(str(self.exe)); self.setup.install()
        paths = [self.exe.parent / 'version.dll', self.exe.parent / 'EpochPact/EpochPact.Core.dll',
                 self.exe.parent / 'EpochPact/installed-build.json']
        before = {path: path.read_bytes() for path in paths}
        real_copy = le.shutil.copy2
        def denied_loader(source, destination, *args, **kwargs):
            if Path(destination).name == 'new-2':
                raise PermissionError('simulated denied stage')
            return real_copy(source, destination, *args, **kwargs)
        with patch.object(le.shutil, 'copy2', side_effect=denied_loader):
            self.assertTrue(self.setup.install()['requiresElevation'])
        self.assertEqual(before, {path: path.read_bytes() for path in paths})
        self.assertEqual(list((self.exe.parent / 'EpochPact').glob('.install-*')), [])

    def test_failed_publication_restores_previous_files_or_removes_new_files(self):
        self.setup.select(str(self.exe))
        for updating in (False, True):
            with self.subTest(updating=updating):
                if updating:
                    self.setup.install()
                    (self.exe.parent / 'EpochPact/EpochPact.Core.dll').write_bytes(b'old player core EpochPact_Start\x00')
                paths = [self.exe.parent / 'version.dll', self.exe.parent / 'EpochPact/EpochPact.Core.dll',
                         self.exe.parent / 'EpochPact/installed-build.json']
                before = {path: path.read_bytes() if path.exists() else None for path in paths}
                real_replace = le.os.replace
                def denied_loader(source, destination):
                    if Path(source).name == 'new-2':
                        raise PermissionError('simulated locked loader')
                    return real_replace(source, destination)
                with patch.object(le.os, 'replace', side_effect=denied_loader):
                    self.assertTrue(self.setup.install()['requiresElevation'])
                self.assertEqual(before, {path: path.read_bytes() if path.exists() else None for path in paths})
                self.assertEqual(list((self.exe.parent / 'EpochPact').glob('.install-*')), [])

    def test_failed_recovery_keeps_previous_files_for_manual_recovery(self):
        self.setup.select(str(self.exe)); self.setup.install()
        core = self.exe.parent / 'EpochPact/EpochPact.Core.dll'
        core.write_bytes(b'old player core EpochPact_Start\x00')
        real_replace = le.os.replace
        def deny_publish_and_restore(source, destination):
            if Path(source).name in ('new-2', 'old-0'):
                raise PermissionError('simulated recovery failure')
            return real_replace(source, destination)
        with patch.object(le.os, 'replace', side_effect=deny_publish_and_restore):
            with self.assertRaisesRegex(RuntimeError, 'recovery could not finish'):
                self.setup.install()
        stages = list((self.exe.parent / 'EpochPact').glob('.install-*'))
        self.assertEqual(len(stages), 1)
        self.assertEqual((stages[0] / 'old-0').read_bytes(), b'old player core EpochPact_Start\x00')

    def test_elevated_helper_uses_exact_selected_path_and_verifies_result(self):
        self.setup.select(str(self.exe))
        def helper(arguments):
            self.assertEqual(arguments[arguments.index('--game-exe') + 1], str(self.exe))
            result_path = Path(arguments[arguments.index('--install-result') + 1])
            self.setup.install()
            result_path.write_text('{"ok":true}')
        with patch('tools.player_setup.user_root', return_value=self.root / 'user'), \
             patch('tools.player_setup.run_elevated', side_effect=helper) as invoked:
            self.assertTrue(self.setup.elevated_install()['launcher']['installed'])
        self.assertEqual(invoked.call_count, 1)
        self.assertEqual(list((self.root / 'user').iterdir()), [])

    def test_cancelled_administrator_request_never_launches_or_retries(self):
        self.setup.select(str(self.exe))
        with patch('tools.player_setup.user_root', return_value=self.root / 'user'), \
             patch('tools.player_setup.run_elevated', side_effect=RuntimeError('Administrator installation was cancelled')) as invoked, \
             patch.object(le, 'cmd_launch') as launch:
            with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                self.setup.elevated_install()
        self.assertEqual(invoked.call_count, 1); launch.assert_not_called()
        self.assertFalse((self.exe.parent / 'version.dll').exists())

    def test_permission_denial_provides_administrator_action_without_launching(self):
        self.setup.select(str(self.exe))
        with patch.object(le, 'cmd_install', side_effect=PermissionError()), patch.object(le, 'cmd_launch') as launch:
            result = self.setup.install()
        self.assertFalse(result['ok']); self.assertTrue(result['requiresElevation'])
        launch.assert_not_called()

    def test_compatibility_access_denial_reaches_admin_button_without_installing(self):
        self.setup.select(str(self.exe))
        with patch.object(le, 'require_supported_build', side_effect=PermissionError('denied GameAssembly.dll')):
            result = self.setup.install()
        self.assertFalse(result['ok']); self.assertTrue(result['requiresElevation'])
        self.assertIn('GameAssembly.dll', result['error'])
        self.assertFalse((self.exe.parent / 'version.dll').exists())
        self.assertFalse((self.exe.parent / 'EpochPact').exists())

    def test_denied_download_does_not_offer_elevation_or_change_game(self):
        self.setup.select(str(self.exe))
        with patch.object(le, 'build_artifact', side_effect=PermissionError('denied bundled core')):
            result = self.setup.install()
        self.assertFalse(result['ok']); self.assertFalse(result['requiresElevation'])
        self.assertIn('download', result['error'])
        self.assertFalse((self.exe.parent / 'EpochPact').exists())

    def test_unreadable_process_list_is_not_treated_as_game_closed(self):
        self.setup.select(str(self.exe))
        with patch.object(le, 'game_pids', side_effect=PermissionError('process list denied')), \
             patch.object(le, 'cmd_install') as install:
            self.assertTrue(self.setup.status()['runningUnknown'])
            with self.assertRaisesRegex(RuntimeError, 'Installation has been blocked'):
                self.setup.install()
            install.assert_not_called()

    def test_launch_calls_only_existing_offline_backend(self):
        self.setup.select(str(self.exe)); self.setup.install()
        with patch.object(le, 'cmd_launch', return_value=0) as launch:
            self.setup.launch()
        self.assertTrue(launch.call_args.args[0].offline)

    def test_tampered_core_and_disabled_marker_block_launch(self):
        self.setup.select(str(self.exe)); self.setup.install()
        marker = self.exe.parent / 'EpochPact/disabled'; marker.touch()
        with self.assertRaisesRegex(RuntimeError, 'disabled'): self.setup.require_ready()
        marker.unlink()
        (self.exe.parent / 'EpochPact/EpochPact.Core.dll').write_bytes(b'tampered')
        with self.assertRaisesRegex(RuntimeError, 'update'): self.setup.require_ready()

    def test_regular_game_control_gate_does_not_spawn_a_process_scan(self):
        self.setup.select(str(self.exe)); self.setup.install()
        with patch.object(le, 'game_pids') as scan:
            self.setup.require_ready()
        scan.assert_not_called()

    def test_atomic_config_failure_preserves_previous_game(self):
        self.setup.select(str(self.exe))
        old = self.setup.config_path.read_bytes()
        other = game(self.root, 'Other game')
        with patch('tools.player_setup.os.replace', side_effect=PermissionError()):
            with self.assertRaises(PermissionError): self.setup.select(str(other))
        self.assertEqual(self.setup.config_path.read_bytes(), old)
        self.assertEqual(le.GAME, self.exe.parent)

    def test_ui_startup_and_connection_do_not_dispatch_ipc_before_selection(self):
        app = create_app(self.bridge, setup=self.setup)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        client = app.test_client()
        with patch.object(le, 'send') as send, patch.object(le, 'cmd_launch') as launch:
            token = client.get('/api/bootstrap').json['token']
            response = client.post('/api/jobs', json={'type':'connection'}, headers={'X-Epoch-Token':token})
            result = app.extensions['epoch_jobs'][response.json['job']].result(timeout=2)
        self.assertFalse(result['launcher']['selected'])
        send.assert_not_called(); launch.assert_not_called()

    def test_backend_without_setup_cannot_advertise_client_selection(self):
        app = create_app(self.bridge)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        boot = app.test_client().get('/api/bootstrap').json
        self.assertEqual(boot['setup_version'], 0)
        self.assertIsNone(boot['launcher'])

    def test_occupied_legacy_port_starts_own_backend_with_windowed_stderr(self):
        from urllib.request import urlopen
        from werkzeug.serving import make_server
        legacy = create_app(self.bridge)
        app = create_app(UiBridge(), setup=self.setup)
        # Reproduce the previous development server's SO_REUSEADDR behavior.
        old = make_server('127.0.0.1', 0, legacy, threaded=True)
        old_thread = threading.Thread(target=old.serve_forever, daemon=True); old_thread.start()
        server = None; thread = None
        try:
            with patch('sys.stderr', None):
                server = interface_server(app, old.server_port)
            self.assertNotEqual(server.server_port, old.server_port)
            thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
            with urlopen(f'http://127.0.0.1:{server.server_port}/api/bootstrap', timeout=2) as reply:
                boot = json.load(reply)
            self.assertEqual(boot['setup_version'], 1)
            self.assertFalse(boot['launcher']['selected'])
            with urlopen(f'http://127.0.0.1:{old.server_port}/api/bootstrap', timeout=2) as reply:
                self.assertEqual(json.load(reply)['setup_version'], 0)
            self.assertFalse(self.setup.config_path.exists())
        finally:
            if server:
                if thread: server.shutdown(); thread.join(timeout=2)
                server.server_close()
            old.shutdown(); old_thread.join(timeout=2); old.server_close()
            app.extensions['epoch_worker'].shutdown()
            legacy.extensions['epoch_worker'].shutdown()

    def test_queued_game_action_cannot_switch_to_another_installation(self):
        app = create_app(self.bridge, setup=self.setup)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        client = app.test_client(); token=client.get('/api/bootstrap').json['token']
        headers={'X-Epoch-Token':token}
        gate=threading.Event()
        blocker=app.extensions['epoch_worker'].submit(gate.wait)
        try:
            selected=client.post('/api/jobs', json={'type':'launcher','action':'select','args':{'executable':str(self.exe)}}, headers=headers)
            action=client.post('/api/jobs', json={'type':'control','id':'xp','args':{'value':2}}, headers=headers)
            with patch.object(self.bridge, 'execute') as execute:
                gate.set(); blocker.result(timeout=2)
                self.assertTrue(app.extensions['epoch_jobs'][selected.json['job']].result(timeout=2)['ok'])
                result=app.extensions['epoch_jobs'][action.json['job']].result(timeout=2)
                self.assertFalse(result['ok']); self.assertIn('location changed', result['error'])
                execute.assert_not_called()
        finally:
            gate.set()

    def test_frozen_resources_and_user_data_never_share_extraction_folder(self):
        with patch.object(app_paths.sys, 'frozen', True, create=True), \
             patch.object(app_paths.sys, '_MEIPASS', str(self.root / 'temporary'), create=True), \
             patch.dict(app_paths.os.environ, {'EPOCHPACT_USER_DIR':str(self.root / 'persistent')}):
            self.assertEqual(app_paths.resource_root(), self.root / 'temporary')
            self.assertEqual(app_paths.runtime_root(), self.root / 'persistent')


if __name__ == '__main__':
    unittest.main()
