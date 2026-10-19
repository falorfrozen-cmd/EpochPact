"""The player loader carries the core: recognition, verification and updates from an older install.

Fixture folders only; the real game, its saves and the real build output are never changed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

from tools import le_session as le, stamp_build
from tools.player_setup import PlayerSetup
from tools.ui_bridge import UiBridge


def sha(data):
    return hashlib.sha256(data).hexdigest()


class LoaderMarkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def write(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_both_generations_of_our_loader_are_recognised_and_foreign_ones_are_not(self):
        older = self.write('older.dll', b'MZ ' + le.MARK)           # looks for a separate core
        linked = self.write('linked.dll', b'MZ ' + le.LINKED_MARK)   # carries the core
        foreign = self.write('foreign.dll', b'MZ some other mod loader')
        self.assertTrue(le.is_ours(older)); self.assertFalse(le.is_linked_loader(older))
        self.assertTrue(le.is_ours(linked)); self.assertTrue(le.is_linked_loader(linked))
        self.assertFalse(le.is_ours(foreign)); self.assertFalse(le.is_linked_loader(foreign))
        self.assertFalse(le.is_ours(self.root / 'missing.dll'))

    def test_real_builds_carry_the_expected_mark(self):
        linked = le.BUILD / 'player/version.dll'
        split = le.BUILD / 'version.dll'
        self.assertTrue(le.is_linked_loader(linked))
        # Older apps recognise their loader by this text alone; without it they would refuse to
        # replace or remove the linked loader as "another mod loader".
        self.assertIn(le.MARK, linked.read_bytes())
        self.assertIn(b'connectionping', linked.read_bytes())   # the core's handlers are inside it
        self.assertFalse(le.is_ours_core(linked), 'the linked loader must never pass for a separate core file')
        self.assertTrue(le.is_ours(split)); self.assertFalse(le.is_linked_loader(split))

    def build_dir(self):
        build = self.root / 'build'
        (build / 'player').mkdir(parents=True)
        shutil.copy2(le.BUILD / 'player/version.dll', build / 'player/version.dll')
        shutil.copy2(le.BUILD / 'player/build-info.json', build / 'player/build-info.json')
        shutil.copy2(le.BUILD / 'version.dll', build / 'version.dll')
        return build

    def test_player_loader_is_verified_against_its_recorded_hash(self):
        build = self.build_dir()
        with patch.object(le, 'BUILD', build):
            path, digest = le.loader_artifact('player')
            self.assertEqual(path, build / 'player/version.dll'); self.assertEqual(digest, sha(path.read_bytes()))
            path.write_bytes(path.read_bytes() + b'changed after the build')
            with self.assertRaisesRegex(ValueError, 'Loader SHA256 mismatch'):
                le.loader_artifact('player')

    def test_player_loader_that_does_not_carry_the_core_is_refused(self):
        build = self.build_dir()
        plain = (build / 'version.dll').read_bytes()   # the separate-core loader, not the linked one
        (build / 'player/version.dll').write_bytes(plain)
        info = json.loads((build / 'player/build-info.json').read_text())
        info['loaderSha256'] = sha(plain)
        (build / 'player/build-info.json').write_text(json.dumps(info))
        with patch.object(le, 'BUILD', build), self.assertRaisesRegex(ValueError, 'does not carry the core'):
            le.loader_artifact('player')

    def test_research_and_test_builds_keep_the_separate_core_loader(self):
        build = self.build_dir()
        with patch.object(le, 'BUILD', build):
            for flavor in ('research', 'test'):
                path, digest = le.loader_artifact(flavor)
                self.assertEqual(path, build / 'version.dll'); self.assertFalse(le.is_linked_loader(path))
            with self.assertRaises(ValueError):
                le.loader_artifact('unknown')

    def test_stamping_a_player_build_records_the_loader_and_requires_it(self):
        build = self.root / 'stamp'
        build.mkdir()
        (build / 'EpochPact.Core.dll').write_bytes(b'core bytes')
        with self.assertRaisesRegex(ValueError, 'merged loader'):
            stamp_build.stamp('player', build)
        (build / 'player').mkdir(exist_ok=True)
        (build / 'player/version.dll').write_bytes(b'linked loader bytes')
        stamp_build.stamp('player', build)
        info = json.loads((build / 'player/build-info.json').read_text())
        self.assertEqual(info['sha256'], sha(b'core bytes')); self.assertEqual(info['loaderSha256'], sha(b'linked loader bytes'))
        stamp_build.stamp('research', build)   # other flavors never need or record a loader
        self.assertNotIn('loaderSha256', json.loads((build / 'research/build-info.json').read_text()))


class OlderInstallUpdateTests(unittest.TestCase):
    """An install from before the linked loader: separate loader and core, both recognised as ours."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        directory = self.root / 'Older install ü'
        directory.mkdir()
        self.exe = directory / le.EXE
        self.exe.write_bytes(b'MZgame')
        (directory / 'GameAssembly.dll').touch()
        (directory / 'Last Epoch_Data').mkdir()
        self.setup = PlayerSetup(UiBridge(), config_path=self.root / 'preferences/settings.json')
        for p in (patch.object(le, 'GAME', le.GAME), patch.object(le, 'require_supported_build', return_value={'gameVersion': 'fixture'}),
                  patch.object(le, 'game_pids', return_value=[])):
            p.start(); self.addCleanup(p.stop)
        self.setup.select(str(self.exe))
        game = self.exe.parent
        (game / 'EpochPact').mkdir()
        self.old_loader = b'MZ older loader ' + le.MARK
        self.old_core = b'MZ older core EpochPact_Start\x00'
        self.old_record = json.dumps({'flavor': 'player', 'sha256': sha(self.old_core), 'loaderMode': 'standalone'}).encode()
        (game / 'version.dll').write_bytes(self.old_loader)
        (game / 'EpochPact/EpochPact.Core.dll').write_bytes(self.old_core)
        (game / 'EpochPact/installed-build.json').write_bytes(self.old_record)
        (game / 'EpochPact/logs').mkdir()
        (game / 'EpochPact/logs/core.log').write_text('kept')

    def test_older_install_asks_for_an_update_and_is_not_ready(self):
        state = self.setup.status()
        self.assertFalse(state['installed']); self.assertTrue(state['updateRequired'])
        with self.assertRaisesRegex(RuntimeError, 'another EpochPact package'):
            self.setup.require_ready()

    def test_update_replaces_both_files_with_the_linked_loader_and_backs_up_the_old_ones(self):
        result = self.setup.install()
        self.assertTrue(result['ok'], result)
        game = self.exe.parent
        self.assertEqual((game / 'version.dll').read_bytes(), le.loader_artifact()[0].read_bytes())
        self.assertFalse((game / 'EpochPact/EpochPact.Core.dll').exists())
        state = self.setup.status()
        self.assertTrue(state['installed']); self.assertFalse(state['updateRequired'])
        backups = list((game / 'EpochPact/install-backups').iterdir())
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'version.dll').read_bytes(), self.old_loader)
        self.assertEqual((backups[0] / 'EpochPact.Core.dll').read_bytes(), self.old_core)
        self.assertEqual((game / 'EpochPact/logs/core.log').read_text(), 'kept')
        self.assertEqual(list((game / 'EpochPact').glob('.install-*')), [])

    def test_a_foreign_file_named_like_the_core_is_never_removed(self):
        core = self.exe.parent / 'EpochPact/EpochPact.Core.dll'
        core.write_bytes(b'someone else wrote this file')
        with self.assertRaisesRegex(RuntimeError, 'unrecognized core file'):
            self.setup.install()
        self.assertEqual(core.read_bytes(), b'someone else wrote this file')
        self.assertEqual((self.exe.parent / 'version.dll').read_bytes(), self.old_loader)

    def test_uninstall_removes_the_older_pair_and_keeps_logs(self):
        self.assertEqual(le.cmd_uninstall(argparse.Namespace()), 0)
        self.assertFalse((self.exe.parent / 'version.dll').exists())
        self.assertFalse((self.exe.parent / 'EpochPact/EpochPact.Core.dll').exists())
        self.assertEqual((self.exe.parent / 'EpochPact/logs/core.log').read_text(), 'kept')


if __name__ == '__main__':
    unittest.main()
