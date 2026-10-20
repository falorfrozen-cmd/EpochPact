"""Fail-closed test-session guards; no real game, save or preferences writes."""
import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch
from tools import ingame_live_session as session


class SessionSafetyTests(unittest.TestCase):
    def test_restored_crashed_session_cannot_be_reported_as_a_successful_rerun(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "session.json").write_text(json.dumps({"phase": "restored", "closeCode": 1}))
            with self.assertRaisesRegex(RuntimeError, "normal game shutdown was not confirmed"):
                session.finish(folder)

    def test_crash_is_failed_after_recovery_not_before_original_bytes_are_restored(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            game = folder / "game"
            game.mkdir()
            (folder / "runtime-before").mkdir()
            original = b"original-runtime"
            (folder / "runtime-before/version.dll").write_bytes(original)
            (game / "version.dll").write_bytes(b"test-runtime")
            state = {"phase": "running", "game": str(game.resolve()), "records": [{"relative": "version.dll", "sha256": session.hashlib.sha256(original).hexdigest()}],
                     "realPreferencesHash": None, "pid": 123, "process": {"sessionKey": "123:999"}}
            (folder / "session.json").write_text(json.dumps(state))
            with patch.object(session.le, "game_pids", side_effect=[[123], []]), \
                 patch.object(session.le, "process_identity", return_value=state["process"]), \
                 patch.object(session.le, "cmd_close", return_value=1), \
                 patch.object(session.le, "GAME", game), \
                 patch.object(session.le, "_make_install_writable"), \
                 patch.dict(session.os.environ, {"LOCALAPPDATA": str(folder / "no-real-preferences")}):
                with self.assertRaisesRegex(RuntimeError, "This live test failed"):
                    session.finish(folder)
            self.assertEqual((game / "version.dll").read_bytes(), original)
            restored = json.loads((folder / "session.json").read_text())
            self.assertEqual(restored["phase"], "restored")
            self.assertFalse(restored["normalShutdownConfirmed"])

    def test_start_refuses_an_existing_game_before_touching_any_file(self):
        with patch.object(session.le, "game_pids", return_value=[123]), patch.object(session.saves, "prepare") as prepare:
            with self.assertRaisesRegex(RuntimeError, "already running"):
                session.start(Path("never-created-test-directory"), Path("unused.dll"))
            prepare.assert_not_called()

    def test_unexpected_identity_is_fatal_not_a_retried_read(self):
        calls = []
        def bad():
            calls.append(1)
            raise session.UnsafeTestState("unexpected character")
        with self.assertRaisesRegex(session.UnsafeTestState, "unexpected character"):
            session.wait(bad, 300, "must stop")
        self.assertEqual(calls, [1])

    def test_reads_bind_to_the_verified_process_creation_time(self):
        process = {"processId": 12, "sessionKey": "12:999"}
        with patch.object(session, "PROCESS", process), patch.object(session.le, "send", return_value='{"ok":true}') as send:
            self.assertTrue(session.reply("panel read")["ok"])
            send.assert_called_once_with("panel read", timeout=8, expected_process=process)


if __name__ == "__main__":
    unittest.main()
