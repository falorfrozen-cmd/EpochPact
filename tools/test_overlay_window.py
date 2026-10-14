import unittest
import unittest.mock
from unittest.mock import MagicMock

from tools import player_setup


class FakeEvent(list):
    def __iadd__(self, handler):
        self.append(handler)
        return self


class FakeWebview:
    def __init__(self):
        self.calls = []

    def create_window(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        window = MagicMock()
        window.events.loaded = FakeEvent()
        return window


class OverlayWindowTests(unittest.TestCase):
    def test_small_windows_are_frameless_with_a_see_through_background(self):
        webview, api = FakeWebview(), player_setup.FeedWindowApi()
        window = player_setup.overlay_window(webview, api, 'http://127.0.0.1:1/feed', {'width': 380, 'height': 520})
        (args, kwargs), = webview.calls
        self.assertEqual(args[0], 'EpochPact Loot Feed', 'the title the stacking helper finds the window by')
        self.assertTrue(kwargs['frameless'])
        self.assertTrue(kwargs['transparent'])
        self.assertFalse(kwargs['easy_drag'], 'only the bar moves the window; sliders and lists stay usable')
        self.assertEqual(kwargs['min_size'], api.MINIMUM)
        self.assertEqual((kwargs['width'], kwargs['height']), (380, 520))
        self.assertIs(api._window, window)

    def test_the_window_is_shown_again_once_its_page_has_loaded(self):
        webview, api = FakeWebview(), player_setup.FeedWindowApi()
        window = player_setup.overlay_window(webview, api, 'http://127.0.0.1:1/feed', {'width': 460, 'height': 380})
        self.assertEqual(len(window.events.loaded), 1)
        with unittest.mock.patch.object(player_setup, 'reshow_window') as reshow:
            window.events.loaded[0]()
            window.events.loaded[0]()  # a reload (language change) keeps the size the player gave it
        window.resize.assert_called_once_with(460, 380)
        self.assertEqual(reshow.call_count, 2)

    def test_reshow_hides_and_shows_without_taking_the_focus(self):
        user32 = MagicMock()
        user32.FindWindowW.return_value = 42
        user32.IsWindowVisible.return_value = True
        self.assertTrue(player_setup.reshow_window('EpochPact Damage', user32))
        self.assertEqual([c.args for c in user32.ShowWindowAsync.call_args_list], [(42, 0), (42, 4)])
        user32.ShowWindowAsync.reset_mock()
        user32.IsWindowVisible.return_value = False  # hidden while the game is minimised: leave it
        self.assertFalse(player_setup.reshow_window('EpochPact Damage', user32))
        user32.FindWindowW.return_value = 0
        self.assertFalse(player_setup.reshow_window('EpochPact Damage', user32))
        user32.ShowWindowAsync.assert_not_called()

    def test_resize_from_the_page_checks_the_size(self):
        api = player_setup.DamageWindowApi()
        self.assertFalse(api.resize_window(400, 300), 'no window yet')
        api._window = MagicMock()
        self.assertTrue(api.resize_window(400.6, 300))
        api._window.resize.assert_called_once_with(400, 300)
        for bad in ((100, 300), (400, 50), (9000, 300), ('400', 300), (True, 300), (None, 1)):
            self.assertFalse(api.resize_window(*bad), bad)
        self.assertEqual(api._window.resize.call_count, 1)

    def test_hide_closes_the_window(self):
        api = player_setup.FeedWindowApi()
        self.assertFalse(api.hide_window())
        api._window = MagicMock()
        hidden = []
        api._on_hide = lambda: hidden.append(True)
        self.assertTrue(api.hide_window())
        api._window.destroy.assert_called_once_with()
        self.assertEqual(hidden, [True], 'EpochPact is told the player hid it')

    def test_the_page_bridge_sees_only_the_window_actions(self):
        cls = player_setup.FeedWindowApi
        public = sorted(n for n in dir(cls) if not n.startswith('_') and callable(getattr(cls, n)))
        self.assertEqual(public, ['hide_window', 'resize_window'])

    def test_main_window_buttons_follow_the_real_state(self):
        dialogs = player_setup.DesktopDialogs()
        self.assertEqual(dialogs.overlay_windows(), {'feed': False, 'damage': False})
        dialogs._feed = feed = MagicMock()
        self.assertEqual(dialogs.overlay_windows(), {'feed': True, 'damage': False})
        self.assertTrue(dialogs.hide_loot_feed())
        feed.destroy.assert_called_once_with()
        self.assertTrue(dialogs.hide_damage_window(), 'nothing open: nothing to do')


if __name__ == '__main__':
    unittest.main()
