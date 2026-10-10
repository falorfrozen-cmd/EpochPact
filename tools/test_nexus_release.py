"""Compatibility and installer recovery checks on isolated, non-runnable fixtures."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
from tools import game_compatibility as compatibility, le_session as le


class NexusReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='EpochPact-nexus-fixture-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'SteamLibrary ü' / 'steamapps/common/Last Epoch'
        self.game.mkdir(parents=True)
        (self.game / le.EXE).write_bytes(b'MZ non-runnable fixture')
        (self.game / 'Last Epoch_Data').mkdir()
        self.assembly = self.game / 'GameAssembly.dll'
        self.assembly.write_bytes(b'known fixture assembly')
        self.manifest = self.root / 'builds.json'
        self.manifest.write_text(json.dumps({'builds':[{'gameVersion':'fixture',
            'bytes': self.assembly.stat().st_size, 'sha256':hashlib.sha256(self.assembly.read_bytes()).hexdigest()}]}))
        self.patches = [patch.object(le, 'GAME', self.game), patch.object(le, 'game_pids', return_value=[])]
        for item in self.patches:
            item.start(); self.addCleanup(item.stop)

    def test_version_digest_caches_but_invalidates_after_same_size_edit(self):
        self.assertEqual(compatibility.require_supported_build(self.game, manifest=self.manifest)['gameVersion'], 'fixture')
        self.assembly.write_bytes(b'wrong fixture assembly')
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            compatibility.require_supported_build(self.game, manifest=self.manifest)

    def test_unknown_build_install_and_launch_are_refused_before_any_write(self):
        original = self.assembly.read_bytes()
        with patch.object(le, 'backup_saves') as saves, patch.object(le.os, 'startfile') as launch:
            self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')), 2)
            with self.assertRaisesRegex(ValueError, 'Unsupported'):
                le.cmd_launch(argparse.Namespace(offline=True))
        saves.assert_not_called(); launch.assert_not_called()
        self.assertFalse((self.game / 'EpochPact').exists())
        self.assertEqual(self.assembly.read_bytes(), original)

    def test_manual_install_update_rollback_uninstall_preserve_saves_and_foreign_mod(self):
        loader = le.BUILD / 'version.dll'
        core = le.build_artifact()[0]
        # Sentinel paths are in the isolated fixture, never the owner's saves.
        saves = self.root / 'Player saves'; saves.mkdir()
        (saves / 'character.sav').write_bytes(b'preserve player save exactly')
        other = self.game / 'OtherMod'; other.mkdir()
        (other / 'plugin.dll').write_bytes(b'preserve unrelated mod exactly')
        before = {p: p.read_bytes() for p in [saves / 'character.sav', other / 'plugin.dll', self.assembly, self.game / le.EXE]}
        mod = self.game / 'EpochPact'; mod.mkdir()
        shutil.copy2(loader, self.game / 'version.dll')
        shutil.copy2(core, mod / 'EpochPact.Core.dll')
        old_core = core.read_bytes() + b'previous fixture release'
        (mod / 'EpochPact.Core.dll').write_bytes(old_core)
        with patch.object(le, 'require_supported_build', return_value={'gameVersion':'fixture'}):
            self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')), 0)
        backups = list((mod / 'install-backups').iterdir())
        self.assertEqual(len(backups), 1)
        self.assertEqual((backups[0] / 'EpochPact.Core.dll').read_bytes(), old_core)
        shutil.copy2(backups[0] / 'EpochPact.Core.dll', mod / 'EpochPact.Core.dll')
        shutil.copy2(backups[0] / 'version.dll', self.game / 'version.dll')
        self.assertEqual((mod / 'EpochPact.Core.dll').read_bytes(), old_core)
        self.assertEqual(le.cmd_uninstall(argparse.Namespace()), 0)
        self.assertFalse((self.game / 'version.dll').exists())
        self.assertFalse((mod / 'EpochPact.Core.dll').exists())
        self.assertTrue((backups[0] / 'EpochPact.Core.dll').exists())
        self.assertEqual(before, {p:p.read_bytes() for p in before})

    def test_ui_gate_refuses_unsupported_build_even_after_manual_copy(self):
        from tools.player_setup import PlayerSetup
        from tools.ui_bridge import UiBridge
        setup = PlayerSetup(UiBridge(), config_path=self.root / 'settings.json')
        setup.select(str(self.game / le.EXE))
        mod = self.game / 'EpochPact'; mod.mkdir()
        shutil.copy2(le.BUILD / 'version.dll', self.game / 'version.dll')
        shutil.copy2(le.build_artifact()[0], mod / 'EpochPact.Core.dll')
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            setup.require_ready()

    def test_unrecognized_core_is_preserved_even_if_loader_is_missing(self):
        mod = self.game / 'EpochPact'; mod.mkdir()
        core = mod / 'EpochPact.Core.dll'; core.write_bytes(b'not our core: preserve')
        with patch.object(le, 'require_supported_build', return_value={'gameVersion':'fixture'}):
            self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')), 2)
        self.assertEqual(le.cmd_uninstall(argparse.Namespace()), 2)
        self.assertEqual(core.read_bytes(), b'not our core: preserve')
        self.assertFalse((self.game / 'version.dll').exists())


if __name__ == '__main__':
    unittest.main()
