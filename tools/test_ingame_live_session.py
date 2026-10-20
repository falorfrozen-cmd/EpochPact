"""Fail-closed test-session guards; no real game, save or preferences writes."""
import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch
from tools import ingame_live_session as session


class SessionSafetyTests(unittest.TestCase):
    def test_character_select_state_is_not_enough_until_the_tile_is_loaded(self):
        responses=iter(['characters: list/character is still loading','offline characters:\nEpCraftTest level=55',
                        'loadname: normal tile double-click requested for EpCraftTest (verify playerread before edits)'])
        def waiter(fn,*args):
            for _ in range(5):
                value=fn()
                if value:return value
            self.fail('Test never became ready')
        with patch.object(session,'reply',return_value={'state':'CharacterSelect','transitioning':False}), \
             patch.object(session,'wait',side_effect=waiter),patch.object(session.le,'send',side_effect=lambda *a,**k:next(responses)) as send:
            session.select_clone('EpCraftTest')
        self.assertEqual([c.args[0] for c in send.call_args_list],['characters','characters','loadname EpCraftTest'])

    def test_unacknowledged_character_load_is_not_retried(self):
        responses=iter(['offline characters:\nEpCraftTest level=55','characters: list/character is still loading'])
        with patch.object(session,'reply',return_value={'state':'CharacterSelect','transitioning':False}), \
             patch.object(session,'wait',side_effect=lambda fn,*a:fn()),patch.object(session.le,'send',side_effect=lambda *a,**k:next(responses)) as send:
            with self.assertRaisesRegex(session.UnsafeTestState,'not acknowledged'):session.select_clone('EpCraftTest')
        self.assertEqual(len([c for c in send.call_args_list if c.args[0].startswith('loadname ')]),1)

    def test_arbitrary_character_name_is_rejected_before_preparation(self):
        with patch.object(session.saves, 'prepare') as prepare:
            with self.assertRaises(session.UnsafeTestState):
                session.start(Path('unused'), Path('unused.dll'), test_name='Falor', save_tests=True)
            prepare.assert_not_called()

    def test_restart_rejects_reused_pid_before_closing_or_launching(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp)
            state={'phase':'running','pid':12,'process':{'sessionKey':'12:before'}}
            (folder/'session.json').write_text(json.dumps(state))
            with patch.object(session.le,'game_pids',return_value=[12]), \
                 patch.object(session.le,'process_identity',return_value={'sessionKey':'12:after'}), \
                 patch.object(session.le,'cmd_close') as close, patch.object(session.le,'cmd_launch') as launch:
                with self.assertRaises(session.UnsafeTestState):session.restart(folder)
                close.assert_not_called();launch.assert_not_called()

    def test_restart_does_not_launch_again_after_an_abnormal_shutdown(self):
        with tempfile.TemporaryDirectory() as temp:
            folder=Path(temp); process={'sessionKey':'12:before'}
            (folder/'session.json').write_text(json.dumps({'phase':'running','pid':12,'process':process}))
            with patch.object(session.le,'game_pids',side_effect=[[12],[]]), \
                 patch.object(session.le,'process_identity',return_value=process), \
                 patch.object(session.le,'cmd_close',return_value=1), patch.object(session.le,'cmd_launch') as launch:
                with self.assertRaisesRegex(session.UnsafeTestState,'did not close normally'):session.restart(folder)
                launch.assert_not_called()
            self.assertEqual(json.loads((folder/'session.json').read_text())['restartCloseCodes'],[1])

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
