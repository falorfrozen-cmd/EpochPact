"""Connection identity/protocol regressions; no game installation or saves are changed."""
import json
import os
from pathlib import Path
import tempfile
import time
import unittest
from unittest.mock import patch

from . import le_session as le


class ConnectionIdentityTests(unittest.TestCase):
    def test_real_process_identity_includes_executable_and_creation_time(self):
        identity = le.process_identity(os.getpid())
        self.assertEqual(identity['processId'], os.getpid())
        self.assertTrue(Path(identity['executable']).is_file())
        self.assertLess(identity['startedNs'], time.time_ns())
        self.assertEqual(identity, le.process_identity(os.getpid()))

    def test_selected_installation_only_and_ambiguous_copies_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            game=Path(directory)
            first={'processId':1,'sessionKey':'1:10','executable':str(game/le.EXE)}
            other={'processId':2,'sessionKey':'2:20','executable':str(game/'other'/le.EXE)}
            with patch.object(le,'GAME',game), patch.object(le,'game_pids',return_value=[1,2]), \
                 patch.object(le,'process_identity',side_effect=[first,other]):
                self.assertEqual(le.selected_game_process(),first)
            with patch.object(le,'GAME',game), patch.object(le,'game_pids',return_value=[2]), \
                 patch.object(le,'process_identity',return_value=other):
                with self.assertRaisesRegex(RuntimeError,'different folder'): le.selected_game_process()
            with patch.object(le,'GAME',game), patch.object(le,'game_pids',return_value=[1,2]), \
                 patch.object(le,'process_identity',side_effect=[first,{**first,'processId':2}]):
                with self.assertRaisesRegex(RuntimeError,'More than one'): le.selected_game_process()

    def test_permission_error_is_not_a_stopped_game(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)), \
             patch.object(le,'game_pids',return_value=[1]), \
             patch.object(le,'process_identity',side_effect=PermissionError('cannot inspect game')):
            with self.assertRaises(le.ProcessQueryRefused): le.selected_game_process()

    @staticmethod
    def refused(api='QueryFullProcessImageNameW'):
        error = PermissionError(13, 'A device attached to the system is not functioning.', None, 31)
        error.api = api
        return error

    @staticmethod
    def announce(game, data):
        path = Path(game)/'EpochPact/ipc/protocol.json'; path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data) if not isinstance(data, str) else data, encoding='utf-8')

    def test_refused_query_connects_through_the_mods_own_announcement(self):
        # A player's PC answered every query for the running game with WinError 31.
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)), \
             patch.object(le,'game_pids',return_value=[29744]), \
             patch.object(le,'process_identity',side_effect=self.refused()):
            with self.assertRaisesRegex(le.ProcessQueryRefused, r'QueryFullProcessImageNameW, WinError 31'):
                le.selected_game_process()  # the mod has not announced itself yet: wait, never "not running"
            self.announce(directory, {'version':3,'pid':29744,'sessionKey':'29744:134368524034151528'})
            process = le.selected_game_process()
            self.assertEqual((process['processId'], process['sessionKey'], process['announced']), (29744, '29744:134368524034151528', True))
            self.assertEqual(process['executable'], str(Path(directory)/le.EXE))
            self.assertEqual(process['startedNs'], (134368524034151528 - 116444736000000000) * 100)

    def test_old_or_foreign_announcements_are_not_used(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)), \
             patch.object(le,'game_pids',return_value=[500]), \
             patch.object(le,'process_identity',side_effect=self.refused('OpenProcess')):
            for data in ({'version':3,'pid':499,'sessionKey':'499:10'},      # a previous game process
                         {'version':3,'pid':500,'sessionKey':'501:10'},      # key of another PID
                         {'version':3,'pid':500,'sessionKey':'500:x'},
                         {'version':2,'pid':500,'sessionKey':'500:10'},
                         {'version':3,'pid':'500','sessionKey':'500:10'},
                         'partly written'):
                self.announce(directory, data)
                with self.assertRaisesRegex(le.ProcessQueryRefused, 'OpenProcess'):
                    le.selected_game_process()

    def test_a_refused_second_process_is_accepted_only_for_the_announced_session(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            verified = {'processId':1,'sessionKey':'1:10','executable':str(game/le.EXE)}
            def identity(pid):
                if pid == 2: raise self.refused()
                return verified
            with patch.object(le,'GAME',game), patch.object(le,'game_pids',return_value=[1,2]), \
                 patch.object(le,'process_identity',side_effect=identity):
                self.announce(directory, {'version':3,'pid':1,'sessionKey':'1:10'})
                self.assertEqual(le.selected_game_process(), verified)  # the mod in this folder is the described one
                self.announce(directory, {'version':3,'pid':2,'sessionKey':'2:20'})
                with self.assertRaises(le.ProcessQueryRefused): le.selected_game_process()  # may be a second copy

    def test_unbound_requests_use_the_announcement_when_the_query_is_refused(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)), \
             patch.object(le,'game_pids',return_value=[29744]), \
             patch.object(le,'process_identity',side_effect=self.refused()):
            self.announce(directory, {'version':3,'pid':29744,'sessionKey':'29744:77'})
            self.assertTrue(le.modern_ipc(Path(directory)/'EpochPact/ipc'))
            self.assertEqual(le.session_of(29744)['sessionKey'], '29744:77')
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)), \
             patch.object(le,'game_pids',return_value=[]), \
             patch.object(le,'process_identity',side_effect=self.refused()):
            self.announce(directory, {'version':3,'pid':29744,'sessionKey':'29744:77'})
            self.assertFalse(le.modern_ipc(Path(directory)/'EpochPact/ipc'))  # that game is no longer running
            with self.assertRaises(le.ProcessQueryRefused): le.session_of(29744)

    def test_protocol_from_previous_process_or_reused_pid_is_not_ready(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(le,'GAME',Path(directory)):
            path=Path(directory)/'EpochPact/ipc/protocol.json';path.parent.mkdir(parents=True)
            current={'processId':123,'sessionKey':'123:100','startedNs':time.time_ns()-1_000_000_000}
            self.assertFalse(le.ipc_ready(current))
            path.write_text(json.dumps({'version':2,'pid':122}))
            self.assertFalse(le.ipc_ready(current))
            path.write_text(json.dumps({'version':3,'pid':123,'sessionKey':'123:100'}))
            self.assertTrue(le.ipc_ready(current))
            os.utime(path,(1,1))
            self.assertTrue(le.ipc_ready(current))  # mtime is never authentication.
            self.assertFalse(le.ipc_ready({**current,'sessionKey':'123:200'}))
            for data in ({'version':1,'pid':123},{'version':2,'pid':'123'}):
                path.write_text(json.dumps(data));self.assertFalse(le.ipc_ready(current))
            path.write_text('incomplete publication');self.assertFalse(le.ipc_ready(current))

    def test_exited_game_is_not_trusted(self):
        with patch.object(le,'game_pids',return_value=[1]), patch.object(le,'process_identity',return_value=None):
            self.assertIsNone(le.selected_game_process())
