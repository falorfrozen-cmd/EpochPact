"""Real C++ transport + compiled production packet parser, isolated from Last Epoch."""
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import time
import unittest
from unittest.mock import patch
from . import le_session as le
from .ui_bridge import UiBridge

HOST=Path(os.environ.get('EPOCHPACT_IPC_TEST_HOST',Path(__file__).resolve().parents[1]/'native/build/ipc_protocol_host.exe'))

class AuthenticatedTransportTests(unittest.TestCase):
    def host(self, delay=0):
        temp=tempfile.TemporaryDirectory(prefix='EpochPact-connection-fixture-')
        self.addCleanup(temp.cleanup)
        root=Path(temp.name); ipc=root/'EpochPact/ipc';ipc.mkdir(parents=True)
        process=subprocess.Popen([str(HOST),str(ipc),str(delay)],creationflags=subprocess.CREATE_NO_WINDOW)
        def stop():
            (ipc/'stop').touch()
            try: process.wait(timeout=12)
            except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
            self.assertEqual(process.returncode,0)
        self.addCleanup(stop)
        deadline=time.monotonic()+5
        while not (ipc/'owner.json').exists():
            if time.monotonic()>deadline: self.fail('Native protocol host did not start')
            time.sleep(.01)
        owner=le.process_identity(process.pid)
        self.assertIsNotNone(owner)
        return root,ipc,owner

    def test_missing_stale_and_readonly_announcements_do_not_gate_connection(self):
        for mode in ('missing','stale','readonly'):
            with self.subTest(mode=mode):
                root,ipc,owner=self.host()
                if mode!='missing':
                    announcement=ipc/'protocol.json'
                    announcement.write_text(json.dumps({'version':2,'pid':1}))
                    os.utime(announcement,(1,1))
                    if mode=='readonly':
                        announcement.chmod(stat.S_IREAD)
                        self.addCleanup(lambda p=announcement:p.chmod(stat.S_IWRITE))
                with patch.object(le,'GAME',root),patch.object(le,'selected_game_process',return_value=owner):
                    result=UiBridge().connection()
                self.assertEqual(result['phase'],'ready');self.assertTrue(result['offline'])
                self.assertEqual(result['sessionKey'],owner['sessionKey'])
                seen=(ipc/'seen').read_text().splitlines()
                self.assertEqual(seen,['connectionping','status','sessionread','identityread','statraw 9 0 0 0'])

    def test_separate_slow_reads_exceed_seven_seconds_and_still_connect(self):
        root,ipc,owner=self.host(delay=4000)
        start=time.monotonic()
        with patch.object(le,'GAME',root),patch.object(le,'selected_game_process',return_value=owner):
            result=UiBridge().connection()
        elapsed=time.monotonic()-start
        self.assertGreater(elapsed,8);self.assertLess(elapsed,14)
        self.assertEqual(result['phase'],'ready');self.assertTrue(result['offline'])

    def test_wrong_or_reused_pid_is_rejected_before_native_mutation_and_never_resent(self):
        root,ipc,owner=self.host()
        for key in ('1:1',str(owner['processId'])+':1'):
            wrong={'processId':int(key.split(':')[0]),'sessionKey':key}
            with self.subTest(key=key),patch.object(le,'GAME',root),self.assertRaises(le.GameSessionError):
                le.send('mutation',timeout=2,expected_process=wrong)
        self.assertFalse((ipc/'mutations').exists())
        self.assertEqual((ipc/'seen').read_text().splitlines(),['!1:1 mutation','!'+str(owner['processId'])+':1 mutation'])

    def test_stale_reply_nonce_is_ignored_and_nested_binding_is_reset(self):
        root,ipc,owner=self.host()
        (ipc/'reply.json').write_text(json.dumps({'nonce':'old','processId':owner['processId'],'sessionKey':owner['sessionKey'],'reply':'stale'}))
        with patch.object(le,'GAME',root):
            with le.bound_process(lambda:owner):
                reply=json.loads(le.send('connectionping',timeout=2))
                self.assertEqual(reply['sessionKey'],owner['sessionKey'])
                with le.bound_process({'processId':1,'sessionKey':'1:1'}):
                    with self.assertRaises(le.GameSessionError):le.send('mutation',timeout=2)
                self.assertEqual(le.send('mutation',timeout=2),'applied')
            self.assertIsNone(le._ipc_process.get())
        self.assertEqual((ipc/'mutations').read_text(),'1')

    def test_matching_nonce_without_verified_owner_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc=Path(directory)
            original=le.os.replace
            def replace(src,dest):
                original(src,dest)
                nonce=(ipc/'cmd.txt').read_text().split()[0][1:]
                (ipc/'reply.json').write_text(json.dumps({'nonce':nonce,'reply':'accepted'}))
            with patch.object(le.os,'replace',side_effect=replace),self.assertRaises(le.GameSessionError):
                le.send_packet(ipc,'mutation',.2,expected_process={'processId':123,'sessionKey':'123:100'})
