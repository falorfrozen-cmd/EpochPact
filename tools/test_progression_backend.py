"""Recovery validation tests run in temporary folders, without a game process."""
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import progression_backend as backend


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.saves = self.base / "active"
        self.saves.mkdir()
        self.backups = self.base / "backups"
        self.backup = self.backups / "test"
        (self.backup / "saves").mkdir(parents=True)
        self.character = {"id": "1", "characterName": "Test", "level": 8, "currentExp": 921,
                          "savedItems": [{"containerID": 2, "data": [1, 2, 3]}]}
        self.manifest = {"format": 1, "operation": "questscomplete", "id": "1", "player": "Test",
                         "stashId": "STASH_CYCLE_8_2_1", "saveDirectory": str(self.saves)}
        self.write("manifest.json", self.manifest)
        self.write("live-character.json", self.character)
        self.write("live-stash.json", {"id": self.manifest["stashId"], "gold": 7})
        self.write("live-global.json", {"id": "global", "unlockedChallengeModes": False})
        for filename in ("1CHARACTERSLOT_BETA_1", "1CHARACTERSLOT_BETA_1.bak",
                         "1CHARACTERSLOT_BETA_1_temp", "1CHARACTERSLOT_BETA_0",
                         self.manifest["stashId"], "STASH_CYCLE_8_0", "STASH_CYCLE_8_2_1_TAB_0",
                         "Epoch_Local_Global_Data_Beta"):
            (self.saves / filename).write_text("new play state", encoding="utf-8")
            (self.backup / "saves" / filename).write_text("old disk state", encoding="utf-8")
        self.recovery = self.base / "recovery"
        self.recovery.mkdir()
        self.addCleanup(patch.stopall)
        patch.object(backend.le_session, "game_pids", return_value=[]).start()
        self.backup_mock = patch.object(backend.le_session, "backup_saves", return_value=self.recovery).start()

    def write(self, name, data):
        (self.backup / name).write_text(json.dumps(data), encoding="utf-8")

    def validate(self, path=None):
        return backend.validate_backup(path or self.backup, backup_root=self.backups, saves=self.saves)

    def restore(self):
        return backend.restore_files(self.backup, backup_root=self.backups, saves=self.saves)

    def test_restores_live_inventory_xp_wallet_and_save_fallbacks(self):
        self.restore()
        for suffix in ("", ".bak", "_temp"):
            self.assertEqual(backend._json(self.saves / ("1CHARACTERSLOT_BETA_1" + suffix)), self.character)
        self.assertEqual(backend._json(self.saves / self.manifest["stashId"])["gold"], 7)
        self.assertFalse(backend._json(self.saves / "Epoch_Local_Global_Data_Beta")["unlockedChallengeModes"])

    def test_one_shot_forge_snapshot_restores_live_item_data_through_existing_recovery(self):
        self.manifest['operation']='forge-one-item'
        self.write('manifest.json',self.manifest)
        self.assertEqual(self.validate()['manifest']['operation'],'forge-one-item')
        self.restore()
        self.assertEqual(backend._json(self.saves/'1CHARACTERSLOT_BETA_1')['savedItems'],self.character['savedItems'])
        self.assertEqual((self.saves/'1CHARACTERSLOT_BETA_0').read_text(),'new play state')

    def test_preserves_other_character_stash_and_tabs(self):
        self.restore()
        for name in ("1CHARACTERSLOT_BETA_0", "STASH_CYCLE_8_0", "STASH_CYCLE_8_2_1_TAB_0"):
            self.assertEqual((self.saves / name).read_text(), "new play state")

    def test_refuses_restore_while_game_runs(self):
        with patch.object(backend.le_session, "game_pids", return_value=[123]):
            with self.assertRaises(RuntimeError): self.restore()
        self.backup_mock.assert_not_called()

    def test_corrupt_snapshot_does_not_touch_any_active_save(self):
        (self.backup / "live-stash.json").write_text("broken")
        before = {f.name: f.read_bytes() for f in self.saves.iterdir()}
        with self.assertRaises(ValueError): self.restore()
        self.assertEqual(before, {f.name: f.read_bytes() for f in self.saves.iterdir()})
        self.backup_mock.assert_not_called()

    def test_refuses_wrong_character(self):
        self.character["id"] = "0"
        self.write("live-character.json", self.character)
        with self.assertRaises(ValueError): self.validate()

    def test_refuses_wrong_wallet(self):
        self.write("live-stash.json", {"id": "STASH_CYCLE_8_0"})
        with self.assertRaises(ValueError): self.validate()

    def test_refuses_identifier_path_traversal(self):
        for key in ("id", "stashId"):
            manifest = dict(self.manifest, **{key: "../other"})
            self.write("manifest.json", manifest)
            with self.assertRaises(ValueError): self.validate()

    def test_refuses_wrong_save_directory(self):
        self.write("manifest.json", dict(self.manifest, saveDirectory=str(self.base)))
        with self.assertRaises(ValueError): self.validate()

    def test_refuses_snapshot_outside_its_root(self):
        with self.assertRaises(ValueError): self.validate(self.backups)

    def test_missing_original_character_file_refused(self):
        (self.backup / "saves" / "1CHARACTERSLOT_BETA_1").unlink()
        with self.assertRaises(ValueError): self.validate()

    def test_missing_recovery_backup_leaves_files_unchanged(self):
        self.backup_mock.return_value = None
        with self.assertRaises(RuntimeError): self.restore()
        self.assertEqual((self.saves / "1CHARACTERSLOT_BETA_1").read_text(), "new play state")

    def test_extra_directory_in_snapshot_refused(self):
        (self.backup / "saves" / "unexpected").mkdir()
        with self.assertRaises(ValueError): self.validate()

    def test_undo_refuses_different_loaded_character_before_close(self):
        with patch.object(backend, "validate_backup", return_value={"manifest": self.manifest}), \
             patch.object(backend, "read", return_value={"player": {"id": "0", "name": "Falor"}}), \
             patch.object(backend.le_session, "cmd_close") as close:
            with self.assertRaises(RuntimeError): backend.undo(self.backup)
            close.assert_not_called()


if __name__ == "__main__":
    unittest.main()
