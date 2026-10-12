"""Keep EpochPact's small windows (loot feed, damage) above the game only.

They are not "always on top" for every program: while the game (or the small
window itself) is in front they stay above it; when another window such as
EpochPact is activated they go behind that window; while the game is
minimised they are hidden. Without a running game they behave as ordinary
windows. Only window positions are changed; no input is sent.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import threading
import time

HWND_TOPMOST, HWND_NOTOPMOST = -1, -2
SWP_NOSIZE, SWP_NOMOVE, SWP_NOACTIVATE = 0x1, 0x2, 0x10
SW_HIDE, SW_SHOWNOACTIVATE = 0, 4


def decide(foreground: int | None, game: int | None, overlays: set[int], game_minimized: bool) -> str:
    """'top' above the game, 'behind' below the window in front, 'hide' while the game is minimised, 'normal' without a game."""
    if not game:
        return 'normal'
    if game_minimized:
        return 'hide'
    if foreground == game or foreground in overlays:
        return 'top'
    return 'behind'


class OverlayFollower:
    def __init__(self, titles, find_game, user32=None, interval: float = 0.25):
        self.titles = tuple(titles)
        self.find_game = find_game
        self.interval = interval
        self.user32 = user32 or self._user32()
        self.state: dict[int, str] = {}
        self.hidden: set[int] = set()
        self._thread = None
        self._lock = threading.Lock()
        self._game = None
        self._game_checked = 0.0

    @staticmethod
    def _user32():
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        user32.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]; user32.FindWindowW.restype = wt.HWND
        user32.GetForegroundWindow.argtypes = []; user32.GetForegroundWindow.restype = wt.HWND
        user32.IsIconic.argtypes = [wt.HWND]; user32.IsIconic.restype = wt.BOOL
        user32.IsWindow.argtypes = [wt.HWND]; user32.IsWindow.restype = wt.BOOL
        user32.ShowWindow.argtypes = [wt.HWND, ctypes.c_int]; user32.ShowWindow.restype = wt.BOOL
        user32.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wt.UINT]
        user32.SetWindowPos.restype = wt.BOOL
        user32.GetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int]; user32.GetWindowLongPtrW.restype = ctypes.c_ssize_t
        return user32

    def topmost(self, hwnd: int) -> bool:
        GWL_EXSTYLE, WS_EX_TOPMOST = -20, 0x8
        try:
            return bool(self.user32.GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST)
        except (AttributeError, OSError):
            return True  # unknown: do not risk making ours topmost

    def overlays(self) -> set[int]:
        return {h for h in (self.user32.FindWindowW(None, t) for t in self.titles) if h}

    def game(self) -> int | None:
        now = time.monotonic()
        if now - self._game_checked > 2 or (self._game and not self.user32.IsWindow(self._game)):
            self._game_checked = now
            try:
                self._game = self.find_game()
            except OSError:
                self._game = None
        return self._game

    def step(self) -> int:
        """One pass; returns how many small windows are open."""
        overlays = self.overlays()
        game = self.game()
        foreground = self.user32.GetForegroundWindow()
        action = decide(foreground, game, overlays, bool(game) and bool(self.user32.IsIconic(game)))
        flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
        for hwnd in overlays:
            if action == 'hide':
                if hwnd not in self.hidden:
                    self.user32.ShowWindow(hwnd, SW_HIDE); self.hidden.add(hwnd)
                self.state[hwnd] = action
                continue
            if hwnd in self.hidden:
                self.user32.ShowWindow(hwnd, SW_SHOWNOACTIVATE); self.hidden.discard(hwnd)
            if action == 'top':
                if self.state.get(hwnd) != 'top':
                    self.user32.SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags)
            elif action == 'behind':
                if self.state.get(hwnd) == 'top' or self.state.get(hwnd) is None:
                    self.user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags)
                if self.state.get(hwnd) != 'behind' and foreground and not self.topmost(foreground):
                    # Directly below the window the player switched to (never
                    # below a topmost window: that would make ours topmost).
                    self.user32.SetWindowPos(hwnd, foreground, 0, 0, 0, 0, flags)
            elif self.state.get(hwnd) != 'normal':
                self.user32.SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags)
            self.state[hwnd] = action
        for gone in set(self.state) - overlays:
            self.state.pop(gone, None); self.hidden.discard(gone)
        return len(overlays)

    def _run(self):
        idle = 0.0
        while idle < 5:
            try:
                open_windows = self.step()
            except Exception:
                open_windows = 1
            idle = 0.0 if open_windows else idle + self.interval
            time.sleep(self.interval)
        with self._lock:
            self._thread = None

    def start(self):
        with self._lock:
            if self._thread is None:
                self._thread = threading.Thread(target=self._run, name='epochpact-overlay-follow', daemon=True)
                self._thread.start()
