import unittest

from tools import overlay_follow as of

GAME, FEED, DAMAGE, APP, TOPMOST_APP = 100, 200, 300, 400, 500


class FakeUser32:
    def __init__(self):
        self.windows = {'EpochPact Loot Feed': FEED, 'EpochPact Damage': DAMAGE}
        self.foreground = GAME
        self.minimized = False
        self.calls = []

    def FindWindowW(self, cls, title): return self.windows.get(title, 0)
    def GetForegroundWindow(self): return self.foreground
    def IsIconic(self, hwnd): return self.minimized
    def IsWindow(self, hwnd): return True
    def ShowWindow(self, hwnd, cmd): self.calls.append(('show', hwnd, cmd)); return True
    def SetWindowPos(self, hwnd, after, x, y, cx, cy, flags):
        assert flags & of.SWP_NOACTIVATE and flags & of.SWP_NOMOVE and flags & of.SWP_NOSIZE
        self.calls.append(('pos', hwnd, after)); return True
    def GetWindowLongPtrW(self, hwnd, index): return 0x8 if hwnd == TOPMOST_APP else 0


class OverlayFollowTests(unittest.TestCase):
    def test_decide(self):
        self.assertEqual(of.decide(GAME, GAME, {FEED}, False), 'top')
        self.assertEqual(of.decide(FEED, GAME, {FEED}, False), 'top')
        self.assertEqual(of.decide(APP, GAME, {FEED}, False), 'behind')
        self.assertEqual(of.decide(APP, GAME, {FEED}, True), 'hide')
        self.assertEqual(of.decide(APP, None, {FEED}, False), 'normal')

    def follower(self):
        user = FakeUser32()
        return user, of.OverlayFollower(('EpochPact Loot Feed', 'EpochPact Damage'), lambda: GAME, user32=user)

    def test_above_the_game_then_behind_epochpact_then_hidden_with_the_game(self):
        user, follow = self.follower()
        self.assertEqual(follow.step(), 2)
        self.assertEqual(sorted(user.calls), [('pos', FEED, of.HWND_TOPMOST), ('pos', DAMAGE, of.HWND_TOPMOST)])
        user.calls.clear(); follow.step()
        self.assertEqual(user.calls, [], 'no repeated window changes while nothing changes')
        user.foreground = APP; follow.step()
        self.assertIn(('pos', FEED, of.HWND_NOTOPMOST), user.calls)
        self.assertIn(('pos', FEED, APP), user.calls)
        user.calls.clear(); user.minimized = True; follow.step()
        self.assertEqual(sorted(user.calls), [('show', FEED, of.SW_HIDE), ('show', DAMAGE, of.SW_HIDE)])
        user.calls.clear(); user.minimized = False; user.foreground = GAME; follow.step()
        self.assertIn(('show', FEED, of.SW_SHOWNOACTIVATE), user.calls)
        self.assertIn(('pos', FEED, of.HWND_TOPMOST), user.calls)

    def test_never_placed_below_a_topmost_window(self):
        user, follow = self.follower()
        follow.step(); user.calls.clear()
        user.foreground = TOPMOST_APP; follow.step()
        self.assertIn(('pos', FEED, of.HWND_NOTOPMOST), user.calls)
        self.assertNotIn(('pos', FEED, TOPMOST_APP), user.calls)

    def test_without_a_game_windows_are_ordinary(self):
        user = FakeUser32()
        follow = of.OverlayFollower(('EpochPact Loot Feed',), lambda: None, user32=user)
        follow.step()
        self.assertEqual(user.calls, [('pos', FEED, of.HWND_NOTOPMOST)])


if __name__ == '__main__':
    unittest.main()
