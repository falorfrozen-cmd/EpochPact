import json
import os
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import player_setup
from tools.preferences_store import PreferencesStore, MAXIMUM_VALUE


class StoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp())
        self.path = self.temp / 'EpochPact' / 'preferences.json'
        self.store = PreferencesStore(self.path)

    def test_values_survive_a_new_store_on_the_same_file(self):
        self.assertEqual(self.store.values(), {})
        self.store.update({'epochpact.theme': '"void-atlas"', 'epochpact.profiles.live': '{"Farm":{"xp":3}}'})
        again = PreferencesStore(self.path)
        self.assertEqual(again.values(), {'epochpact.theme': '"void-atlas"', 'epochpact.profiles.live': '{"Farm":{"xp":3}}'})
        self.assertFalse(self.path.with_name('preferences.json.tmp').exists(), 'the write is atomic')

    def test_none_removes_a_key_and_json_helpers_round_trip(self):
        self.store.update({'epochpact.a': '1', 'epochpact.b': '2'})
        self.store.update({'epochpact.a': None})
        self.assertEqual(self.store.values(), {'epochpact.b': '2'})
        self.store.set_json('epochpact.windows.v1', {'feed': {'open': True, 'x': 10}})
        self.assertEqual(self.store.get_json('epochpact.windows.v1'), {'feed': {'open': True, 'x': 10}})
        self.assertEqual(self.store.get_json('epochpact.missing', {}), {})

    def test_invalid_changes_are_refused_without_writing(self):
        self.store.update({'epochpact.keep': 'yes'})
        for changes in ({'other.key': '1'}, {'epochpact.bad key': '1'}, {'epochpact.x': 5}, {'epochpact.x': 'v' * (MAXIMUM_VALUE + 1)}, {}, None, ['epochpact.x']):
            with self.assertRaises(ValueError, msg=changes):
                self.store.update(changes)
        with self.assertRaises(ValueError):
            self.store.update({'epochpact.ok': '1', 'not.ours': '2'})  # all or nothing
        self.assertEqual(self.store.values(), {'epochpact.keep': 'yes'})

    def test_bounded_number_of_keys(self):
        self.store.update({f'epochpact.k{i}': 'v' for i in range(300)})
        with self.assertRaises(ValueError):
            self.store.update({'epochpact.one-more': 'v'})
        self.assertEqual(len(self.store.values()), 300)

    def test_a_damaged_file_reads_as_empty_and_is_replaced_on_write(self):
        self.path.parent.mkdir(parents=True)
        self.path.write_text('{not json', encoding='utf-8')
        self.assertEqual(self.store.values(), {})
        self.path.write_text(json.dumps({'values': {'epochpact.ok': 'v', 'bad': 'x', 'epochpact.n': 3}}), encoding='utf-8')
        self.assertEqual(self.store.values(), {'epochpact.ok': 'v'})
        self.store.update({'epochpact.new': 'w'})
        self.assertEqual(json.loads(self.path.read_text(encoding='utf-8'))['values'], {'epochpact.ok': 'v', 'epochpact.new': 'w'})


class PlacementTests(unittest.TestCase):
    def test_remembered_place_and_size(self):
        self.assertEqual(player_setup.window_placement({'x': 100, 'y': 50, 'width': 300, 'height': 900}, 380, 520, (260, 200), on_monitor=lambda x, y: True),
                         {'x': 100, 'y': 50, 'width': 300, 'height': 900})

    def test_gone_monitor_centres_and_bad_sizes_use_defaults(self):
        self.assertEqual(player_setup.window_placement({'x': 5000, 'y': 50, 'width': 300, 'height': 400}, 380, 520, (260, 200), on_monitor=lambda x, y: False),
                         {'width': 300, 'height': 400})
        for entry in (None, {}, {'width': 100, 'height': 400}, {'width': 300, 'height': 9000}, {'width': True, 'height': 400}, {'x': 'a', 'y': 1}, {'x': True, 'y': 1}):
            placed = player_setup.window_placement(entry, 380, 520, (260, 200), on_monitor=lambda x, y: True)
            self.assertNotIn('x', placed, entry)
            if not isinstance(entry, dict) or entry.get('width') != 300 or entry.get('height') != 400:
                self.assertEqual((placed['width'], placed['height']), (380, 520), entry)


class Event(list):
    def __iadd__(self, handler):
        self.append(handler)
        return self


class FakeWindow:
    def __init__(self):
        self.events = type('Events', (), {})()
        for name in ('moved', 'resized', 'closed'):
            setattr(self.events, name, Event())


class WindowMemoryTests(unittest.TestCase):
    def setUp(self):
        self.store = PreferencesStore(Path(tempfile.mkdtemp()) / 'preferences.json')
        self.dialogs = player_setup.DesktopDialogs()
        self.dialogs._preferences = self.store

    def state(self):
        return self.store.get_json('epochpact.windows.v1', {})

    def test_moves_and_resizes_are_saved_after_the_window_settles(self):
        window = FakeWindow()
        self.dialogs._track('feed', window)
        self.assertEqual(self.state(), {'feed': {'open': True}})
        for x in range(10):
            window.events.moved[0](100 + x, 40)
        window.events.resized[0](300, 900)
        window.events.moved[0](-32000, -32000)  # minimised: ignored
        time.sleep(1.0)
        self.assertEqual(self.state(), {'feed': {'open': True, 'x': 109, 'y': 40, 'width': 300, 'height': 900}})

    def test_rounding_after_a_reopen_does_not_shrink_the_window(self):
        window = FakeWindow()
        self.dialogs._track('feed', window, {'x': 240, 'y': 160, 'width': 330, 'height': 760})
        window.events.resized[0](329, 760)   # scaled desktop reads a pixel smaller
        window.events.moved[0](241, 160)
        time.sleep(1.0)
        self.assertEqual(self.state(), {'feed': {'open': True}}, 'nothing new to save')
        window.events.resized[0](420, 760)
        time.sleep(1.0)
        self.assertEqual(self.state(), {'feed': {'open': True, 'width': 420, 'height': 760}})

    def test_closing_a_window_forgets_it_but_closing_the_app_keeps_it_open(self):
        self.dialogs._track('feed', FakeWindow())
        self.dialogs._track('damage', FakeWindow())
        self.dialogs._feed_closed()
        self.dialogs._prepare_close(True)
        self.dialogs._damage_closed()
        self.assertEqual(self.state(), {'feed': {'open': False}, 'damage': {'open': True}})

    def test_windows_open_at_start_reopen_once(self):
        self.store.set_json('epochpact.windows.v1', {'feed': {'open': True}, 'damage': {'open': False}})
        opened = []
        with patch.object(self.dialogs, 'open_loot_feed', side_effect=lambda: opened.append('feed')), \
             patch.object(self.dialogs, 'open_damage_window', side_effect=lambda: opened.append('damage')):
            self.dialogs._restore_windows()
            self.dialogs._restore_windows()
        self.assertEqual(opened, ['feed'])

    def test_without_a_preferences_file_nothing_is_remembered(self):
        dialogs = player_setup.DesktopDialogs()
        dialogs._track('feed', FakeWindow())
        dialogs._feed_closed()
        dialogs._restore_windows()  # no error, nothing to reopen

    def test_window_helpers_are_hidden_from_the_page_bridge(self):
        public = [name for name in dir(player_setup.DesktopDialogs) if not name.startswith('_')]
        for name in ('prepare_close', 'restore_windows', 'WINDOWS_KEY', 'track'):
            self.assertNotIn(name, public)


class EndpointTests(unittest.TestCase):
    def setUp(self):
        import epochpact_ui
        from tools.ui_bridge import UiBridge
        self.path = Path(tempfile.mkdtemp()) / 'preferences.json'
        self.app = epochpact_ui.create_app(UiBridge(preview=True), preferences=PreferencesStore(self.path))
        self.client = self.app.test_client()

    def test_read_needs_no_token_and_returns_the_session_token(self):
        answer = self.client.get('/api/preferences').get_json()
        self.assertTrue(answer['ok'])
        self.assertEqual(answer['values'], {})
        self.assertEqual(answer['token'], self.client.get('/api/bootstrap').get_json()['token'])

    def test_writes_need_the_session_token_and_land_in_the_file(self):
        body = {'changes': {'epochpact.theme': '"void-atlas"'}}
        self.assertEqual(self.client.post('/api/preferences', json=body).status_code, 403)
        token = self.client.get('/api/preferences').get_json()['token']
        headers = {'X-Epoch-Token': token}
        self.assertTrue(self.client.post('/api/preferences', json=body, headers=headers).get_json()['ok'])
        self.assertEqual(json.loads(self.path.read_text(encoding='utf-8'))['values'], {'epochpact.theme': '"void-atlas"'})
        self.assertEqual(self.client.get('/api/preferences').get_json()['values'], {'epochpact.theme': '"void-atlas"'})
        bad = self.client.post('/api/preferences', json={'changes': {'not.ours': '1'}}, headers=headers)
        self.assertEqual(bad.status_code, 400)
        self.assertEqual(self.client.post('/api/preferences', json={'changes': {'epochpact.theme': None}}, headers=headers).get_json()['ok'], True)
        self.assertEqual(self.client.get('/api/preferences').get_json()['values'], {})

    def test_sync_script_is_served(self):
        with self.client.get('/ui/storage-sync.js') as response:
            self.assertIn(b'EpochStorageSync', response.data)
        for page in ('/', '/feed', '/damage'):
            with self.client.get(page) as response:
                html = response.data.decode()
            self.assertLess(html.index('storage-sync.js'), html.index('session.js'), page)


if __name__ == '__main__':
    unittest.main()
