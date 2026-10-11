"""Installer coexistence fixtures; never touch the actual game or saves."""
import argparse,json,shutil,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
from tools import le_session as le,melon_compat as melon

class MelonTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.game=self.root/'Steam library ü/Last Epoch';self.game.mkdir(parents=True)
        (self.game/le.EXE).write_bytes(b'MZ fixture');(self.game/'GameAssembly.dll').write_bytes(b'fixture')
        (self.game/'MelonLoader/net6').mkdir(parents=True);(self.game/'MelonLoader/net6/MelonLoader.dll').write_bytes(b'sdk fixture')
        (self.game/'version.dll').write_bytes(b'MZ existing MelonLoader proxy fixture')
        (self.game/'Mods').mkdir();(self.game/'Mods/OtherMod.dll').write_bytes(b'foreign mod fixture')
        self.before={p.relative_to(self.game).as_posix():p.read_bytes() for p in self.game.rglob('*') if p.is_file()}
        core,digest=le.build_artifact('player');self.build=self.root/'build';(self.build/'player').mkdir(parents=True)
        shutil.copy2(core,self.build/'player/EpochPact.Core.dll');shutil.copy2(core.parent/'build-info.json',self.build/'player/build-info.json')
        shutil.copy2(le.BUILD/'version.dll',self.build/'version.dll');(self.build/'melon').mkdir()
        (self.build/'melon'/melon.BRIDGE_NAME).write_bytes(b'MZ EpochPact.MelonBridge EpochPactBridge fixture')
        for p in [patch.object(le,'GAME',self.game),patch.object(le,'BUILD',self.build),patch.object(le,'game_pids',return_value=[]),
                  patch.object(le,'require_supported_build',return_value={}),patch.object(melon,'_version_strings',return_value={'ProductName':'MelonLoader.Bootstrap','FileVersion':'0.7.3.0'})]:
            p.start();self.addCleanup(p.stop)
    def unchanged(self):
        for name,data in self.before.items():self.assertEqual((self.game/name).read_bytes(),data,name)
    def test_install_update_uninstall_keeps_melon_and_other_mods(self):
        self.assertTrue(melon.detected(self.game))
        for _ in range(2):
            self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')),0);self.unchanged()
            self.assertEqual(json.loads((self.game/'EpochPact/installed-build.json').read_text())['loaderMode'],'melon')
        self.assertTrue(melon.owned_bridge(self.game/'Mods'/melon.BRIDGE_NAME))
        self.assertEqual(le.cmd_uninstall(argparse.Namespace()),0);self.unchanged()
        self.assertFalse((self.game/'Mods'/melon.BRIDGE_NAME).exists())
    def test_foreign_bridge_refused_without_overwrite(self):
        bridge=self.game/'Mods'/melon.BRIDGE_NAME;bridge.write_bytes(b'another author')
        self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')),2);self.unchanged();self.assertEqual(bridge.read_bytes(),b'another author')
    def test_denied_bridge_staging_rolls_back_every_existing_file(self):
        self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')),0)
        before={p:p.read_bytes() for p in [self.game/'Mods'/melon.BRIDGE_NAME,self.game/'EpochPact/EpochPact.Core.dll',self.game/'EpochPact/installed-build.json']}
        original=le._copy_install_file
        def copy(source,destination,*args,**kw):
            if Path(destination).name=='new-2':raise PermissionError(5,'fixture denied',str(destination))
            return original(source,destination,*args,**kw)
        with patch.object(le,'_copy_install_file',side_effect=copy),self.assertRaises(PermissionError):le.cmd_install(argparse.Namespace(flavor='player'))
        for p,data in before.items():self.assertEqual(p.read_bytes(),data)
        self.unchanged()
    def test_old_or_unrecognized_loader_is_not_guessed(self):
        for version in ('0.6.6','99.0.0'):
            with patch.object(melon,'_version_strings',return_value={'ProductName':'MelonLoader.Bootstrap','FileVersion':version}):
                self.assertFalse(melon.detected(self.game));self.assertEqual(le.cmd_install(argparse.Namespace(flavor='player')),2)
        self.unchanged()
