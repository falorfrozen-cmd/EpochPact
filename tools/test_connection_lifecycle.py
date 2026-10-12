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
        with patch.object(le,'game_pids',return_value=[1]), \
             patch.object(le,'process_identity',side_effect=PermissionError('cannot inspect game')):
            with self.assertRaises(PermissionError): le.selected_game_process()

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
