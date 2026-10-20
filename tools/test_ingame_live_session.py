"""Fail-closed test-session guards; no game, save or preferences writes."""
import unittest
from pathlib import Path
from unittest.mock import patch
from tools import ingame_live_session as session


class SessionSafetyTests(unittest.TestCase):
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
