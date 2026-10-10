"""Close ordering and actual IPC cleanup paths; no game/save access."""
import json
import threading
import unittest
from unittest.mock import patch
from .ui_bridge import UiBridge
from . import ui_bridge as ui
from epochpact_ui import create_app


class ShutdownTests(unittest.TestCase):
    def setUp(self):
        self.bridge = UiBridge()
        self.commands = []
        self.player = {'id': '42', 'name': 'Test'}
        self.patchers = [patch.object(ui.le, 'game_pids', return_value=[123]),
                         patch.object(ui.le, 'send', side_effect=self.send)]
        for p in self.patchers:
            p.start(); self.addCleanup(p.stop)

    def send(self, command, **kwargs):
        self.commands.append(command)
        if command == 'identityread':
            return json.dumps({'ok': True, 'offline': True, 'player': self.player})
        return 'done'

    def test_open_and_unmodified_close_never_sends_commands(self):
        self.assertEqual(self.bridge.restore_session()['restored'], 0)
        self.assertEqual(self.commands, [])

    def test_close_uses_only_session_resets_and_owned_stat_keys(self):
        self.bridge.temporary_resets = {('xp', None): 'xp 1', ('map_reveal', None): 'mapreveal 0', ('loot_category', 'gold'): 'lootcategory gold 1'}
        self.bridge.actor_owner = ('42', 'Test')
        self.bridge.actor = {'21:0:0:0': {'key': {'sp': 21, 'tags': 0, 'special': 0, 'extra': 0}, 'modes': {'increased': 1}}}
        result = self.bridge.restore_session()
        self.assertEqual(result['restored'], 4)
        self.assertEqual(self.commands, ['identityread', 'xp 1', 'mapreveal 0', 'lootcategory gold 1', 'statraw 21 0 0 0 reset'])
        self.assertFalse(self.bridge.temporary_resets or self.bridge.actor)

    def test_other_loaded_character_is_never_reset(self):
        self.bridge.temporary_resets = {('speed', None): 'speed 1', ('xp', None): 'xp 1'}
        self.bridge.actor_owner = ('old', 'Other')
        self.bridge.actor = {'21:0:0:0': {'key': {'sp': 21, 'tags': 0, 'special': 0, 'extra': 0}, 'modes': {'added': 50}}}
        self.bridge.restore_session()
        self.assertEqual(self.commands, ['identityread', 'xp 1'])

    def test_speed_close_resets_control_meter_and_shared_stat(self):
        self.bridge.actor_owner = ('42', 'Test')
        self.bridge.actor = {'9:0:0:0': {'key': {'sp':9, 'tags':0, 'special':0, 'extra':0}, 'modes': {'increased':.5}}}
        self.bridge.restore_session()
        self.assertEqual(self.commands, ['identityread', 'speed 1', 'statraw 9 0 0 0 reset'])
        self.assertFalse(self.bridge.actor)

    def test_already_closed_game_needs_no_ipc(self):
        self.bridge.temporary_resets[('xp', None)] = 'xp 1'
        with patch.object(ui.le, 'game_pids', return_value=[]):
            self.assertTrue(self.bridge.restore_session()['gameClosed'])
        self.assertFalse(self.commands)

    def test_native_json_rejection_keeps_pending_cleanup(self):
        self.bridge.temporary_resets[('map_reveal', None)] = 'mapreveal 0'
        with patch.object(ui.le, 'send', side_effect=[json.dumps({'ok': True, 'offline': True, 'player': self.player}), json.dumps({'ok': False, 'error': 'game paused'})]):
            with self.assertRaisesRegex(RuntimeError, 'game paused'):
                self.bridge.restore_session()
        self.assertTrue(self.bridge.temporary_resets)

    def test_close_waits_for_accepted_work_then_blocks_new_submissions(self):
        app = create_app(self.bridge)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        client = app.test_client()
        token = client.get('/api/bootstrap').json['token']
        gate, entered = threading.Event(), threading.Event()
        order = []
        def accepted():
            entered.set(); gate.wait(2); order.append('accepted')
        app.extensions['epoch_worker'].submit(accepted)
        entered.wait(1)
        def cleanup():
            order.append('cleanup'); return {'ok': True, 'restored': 1}
        with patch.object(self.bridge, 'restore_session', side_effect=cleanup):
            result = []
            closer = threading.Thread(target=lambda: result.append(app.extensions['epoch_close_session']()))
            closer.start()
            # Queueing the close happens before this sentinel can be accepted.
            import time
            until = time.monotonic() + 1
            while time.monotonic() < until:
                response = client.post('/api/jobs', json={'type':'connection'}, headers={'X-Epoch-Token':token})
                if response.status_code == 503: break
                time.sleep(.001)
            self.assertEqual(response.status_code, 503)
            gate.set(); closer.join(2)
            self.assertFalse(closer.is_alive())
            self.assertEqual(order, ['accepted', 'cleanup'])
            self.assertTrue(result[0]['ok'])
            app.extensions['epoch_close_session']()
            self.assertEqual(order.count('cleanup'), 1)

    def test_cleanup_error_does_not_disable_a_still_open_ui(self):
        app = create_app(self.bridge)
        self.addCleanup(app.extensions['epoch_worker'].shutdown)
        with patch.object(self.bridge, 'restore_session', side_effect=RuntimeError('paused')):
            with self.assertRaisesRegex(RuntimeError, 'paused'):
                app.extensions['epoch_close_session']()
        client = app.test_client()
        token = client.get('/api/bootstrap').json['token']
        self.assertEqual(client.post('/api/jobs', json={'type':'connection'}, headers={'X-Epoch-Token':token}).status_code, 202)


if __name__ == '__main__':
    unittest.main()
