"""Explicit game selection and installation, reusing the existing session backend."""
from __future__ import annotations

import argparse
import contextlib
import ctypes
import ctypes.wintypes as wt
import hashlib
import io
import json
import logging
import os
from pathlib import Path
import secrets
import subprocess
import sys

from . import le_session as le, progression_backend as progression
from . import filter_manager, melon_compat, overlay_follow, support_report
from .app_paths import (user_root, installer_result_root, validate_installer_result,
                        INSTALL_RESULT_INVALID, INSTALL_RESULT_WRITE_FAILED)
from .ui_language import english_exception


def run_elevated(arguments):
    """Run only the explicit installer helper; the normal interface stays unelevated."""
    class ShellExecuteInfo(ctypes.Structure):
        _fields_ = [('cbSize', wt.DWORD), ('fMask', wt.ULONG), ('hwnd', wt.HWND),
                    ('lpVerb', wt.LPCWSTR), ('lpFile', wt.LPCWSTR), ('lpParameters', wt.LPCWSTR),
                    ('lpDirectory', wt.LPCWSTR), ('nShow', ctypes.c_int), ('hInstApp', wt.HINSTANCE),
                    ('lpIDList', ctypes.c_void_p), ('lpClass', wt.LPCWSTR), ('hkeyClass', wt.HKEY),
                    ('dwHotKey', wt.DWORD), ('hIcon', wt.HANDLE), ('hProcess', wt.HANDLE)]
    api = ctypes.WinDLL('shell32', use_last_error=True)
    api.ShellExecuteExW.argtypes = [ctypes.POINTER(ShellExecuteInfo)]
    api.ShellExecuteExW.restype = wt.BOOL
    info = ShellExecuteInfo()
    info.cbSize = ctypes.sizeof(info)
    info.fMask = 0x40 | 0x100  # NOCLOSEPROCESS | NOASYNC
    info.lpVerb = 'runas'
    info.lpFile = sys.executable
    info.lpDirectory = str(Path(sys.executable).parent)
    info.lpParameters = subprocess.list2cmdline(arguments)
    info.nShow = 0
    if not api.ShellExecuteExW(ctypes.byref(info)):
        error = ctypes.get_last_error()
        if error == 1223:
            raise RuntimeError('Administrator installation was cancelled. No retry was made.')
        raise ctypes.WinError(error)
    try:
        waited = le.kernel32.WaitForSingleObject(info.hProcess, 120000)
        if waited == 258:
            raise RuntimeError('The administrator installer is still running. Do not start another installation.')
        if waited != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        code = wt.DWORD()
        if not le.kernel32.GetExitCodeProcess(info.hProcess, ctypes.byref(code)):
            raise ctypes.WinError(ctypes.get_last_error())
        return code.value
    finally:
        le.kernel32.CloseHandle(info.hProcess)


def app_version() -> str:
    for candidate in (Path(sys.executable).parent / 'build-info.json', le.ROOT / 'docs/nexus/release.json'):
        try:
            return str(json.loads(candidate.read_text(encoding='utf-8'))['version'])
        except (OSError, ValueError, KeyError, TypeError):
            continue
    return 'unknown'


def validate_executable(value: str) -> Path:
    if not isinstance(value, str) or not value.strip() or len(value) > 32767:
        raise ValueError('Select Last Epoch.exe.')
    path = Path(value.strip().strip('"')).expanduser()
    if not path.is_absolute():
        raise ValueError('Choose the full path to Last Epoch.exe.')
    path = path.resolve(strict=True)
    if path.name.casefold() != le.EXE.casefold() or not path.is_file():
        raise ValueError('Select Last Epoch.exe, not a launcher or another program.')
    if not (path.parent / 'GameAssembly.dll').is_file() or not (path.parent / 'Last Epoch_Data').is_dir():
        raise ValueError('This is not a complete Last Epoch installation. Select the executable in the game folder.')
    with path.open('rb') as stream:
        if stream.read(2) != b'MZ':
            raise ValueError('The selected game executable is invalid.')
    return path


def picker_directory() -> Path:
    steam = le._steam_root()
    if steam and steam.is_dir():
        return steam
    for name in ('ProgramFiles(x86)', 'ProgramFiles'):
        root = Path(os.environ.get(name, r'C:\Program Files (x86)' if name.endswith('(x86)') else r'C:\Program Files'))
        if (root / 'Steam').is_dir():
            return root / 'Steam'
    return Path.home()


class PlayerSetup:
    def __init__(self, bridge, *, config_path=None):
        self.bridge = bridge
        self.config_path = Path(config_path) if config_path is not None else user_root() / 'settings.json'
        self.executable = None
        self.revision = 0
        self.problem = ''
        try:
            config = json.loads(self.config_path.read_text(encoding='utf-8'))
            if config.get('gameExecutable'):
                self._activate(validate_executable(config['gameExecutable']))
        except FileNotFoundError:
            if self.config_path.exists():
                self.problem = 'The saved game location is unavailable. Select Last Epoch.exe again.'
        except (OSError, ValueError, TypeError, AttributeError):
            self.problem = 'The saved game location is unavailable. Select Last Epoch.exe again.'

    def _activate(self, path):
        le.GAME = path.parent
        progression.BACKUPS = le.GAME / 'EpochPact/backups/progression'
        self.executable = path
        self.revision += 1
        self.bridge.cache.clear()
        self.bridge.actor.clear()
        self.bridge.actor_owner = None
        self.bridge.history.clear()
        self.bridge.temporary_resets.clear()
        self.bridge.process = None
        self.bridge._verified_session = None

    def select(self, value):
        path = validate_executable(value)
        # Re-selecting the current installation is harmless while it runs.
        if self.executable != path and le.game_pids():
            raise RuntimeError('Close Last Epoch before changing its location.')
        self.config_path.parent.mkdir(parents=True, exist_ok=True)
        temp = self.config_path.with_name(self.config_path.name + '.' + secrets.token_hex(8) + '.tmp')
        try:
            temp.write_text(json.dumps({'schema': 1, 'gameExecutable': str(path)}, ensure_ascii=False) + '\n', encoding='utf-8')
            os.replace(temp, self.config_path)
        finally:
            temp.unlink(missing_ok=True)
        if self.executable != path:
            self._activate(path)
        self.problem = ''
        return {'ok': True, 'launcher': self.status(), 'message': 'Game location saved.'}

    def status(self, *, check_running=True):
        result = {'selected': False, 'executable': str(self.executable or ''), 'installed': False, 'updateRequired': False,
                  'running': False, 'problem': self.problem, 'revision': self.revision}
        if check_running:
            try:
                result['running'] = bool(le.game_pids())
            except OSError:
                result['runningUnknown'] = True
                result['problem'] = 'Windows could not check whether Last Epoch is running. Installation has been blocked; refresh and try again.'
        if self.executable is None:
            return result
        try:
            validate_executable(str(self.executable))
            result['selected'] = True
            _, digest = le.build_artifact('player')
            loader = le.GAME / 'version.dll'
            core = le.GAME / 'EpochPact/EpochPact.Core.dll'
            result['foreignLoader'] = loader.exists() and not le.is_ours(loader)
            result['melonLoader'] = result['foreignLoader'] and melon_compat.detected(le.GAME)
            bridge = le.GAME / 'Mods' / melon_compat.BRIDGE_NAME
            bundled_bridge = le.BUILD / 'melon' / melon_compat.BRIDGE_NAME
            loader_ready = le.is_ours(loader) or (result['melonLoader'] and melon_compat.owned_bridge(bridge) and
                bundled_bridge.is_file() and hashlib.sha256(bridge.read_bytes()).digest() == hashlib.sha256(bundled_bridge.read_bytes()).digest())
            result['disabled'] = (le.GAME / 'EpochPact/disabled').exists()
            core_matches = core.is_file() and hashlib.sha256(core.read_bytes()).hexdigest() == digest
            result['installed'] = loader_ready and core_matches
            result['updateRequired'] = le.is_ours_core(core) and not core_matches
        except (OSError, ValueError):
            result['problem'] = 'The game location or bundled mod files are unavailable. Select the game again or restore the EpochPact download.'
        return result

    def require_ready(self):
        # The existing game/IPC gate decides whether a loaded actor is ready.
        # Avoid process enumeration for every slider, stat read and game command.
        state = self.status(check_running=False)
        if not state['selected']:
            raise RuntimeError('Select Last Epoch.exe in Game setup first.')
        le.require_supported_build(le.GAME)
        if not state['installed']:
            if state['updateRequired']:
                raise RuntimeError('The installed mod belongs to another EpochPact package. Close Last Epoch and reinstall the mod from this complete package.')
            raise RuntimeError('Install or update the mod in Game setup first. Close the game before installing.')
        if state.get('disabled'):
            raise RuntimeError('EpochPact is disabled in this game folder. Remove the EpochPact disabled file before launching.')

    def install(self):
        state = self.status()
        if state.get('runningUnknown'):
            raise RuntimeError(state['problem'])
        if not state['selected']:
            raise RuntimeError('Select Last Epoch.exe before installing the mod.')
        # Elevation cannot repair unreadable or quarantined bundled files.
        try:
            le.build_artifact('player')
            (le.BUILD / 'version.dll').read_bytes()
        except PermissionError as exc:
            return {'ok': False, 'requiresElevation': False,
                    'error': 'Windows denied access to the downloaded mod files. Check the security protection history and restore a verified EpochPact download. ' + english_exception(exc),
                    'launcher': self.status(check_running=False)}
        except (OSError, ValueError) as exc:
            return {'ok': False, 'requiresElevation': False,
                    'error': 'The bundled mod files are missing, unreadable or do not match this app. Extract the entire EpochPact ZIP with _internal beside EpochPact.exe. ' + english_exception(exc),
                    'launcher': self.status(check_running=False)}
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output):
                code = le.cmd_install(argparse.Namespace(flavor='player'))
        except PermissionError as exc:
            return {'ok': False, 'requiresElevation': True,
                    'error': 'Windows denied access while trying to ' + getattr(exc, 'install_step', 'install the mod') + '. Folder permissions or security software may be responsible. You can explicitly try Install as administrator. ' + english_exception(exc),
                    'launcher': self.status(check_running=False)}
        if code:
            raise RuntimeError(output.getvalue().strip().removeprefix('refused: '))
        state = self.status()
        if not state['installed']:
            raise RuntimeError('Installation verification failed. The game has not been started.')
        return {'ok': True, 'launcher': state, 'message': 'Player mod installed. You can launch offline now.'}

    def uninstall(self):
        """Remove only EpochPact's own loader/bridge and core; logs, backups and saves stay."""
        state = self.status()
        if state.get('runningUnknown'):
            raise RuntimeError(state['problem'])
        if not state['selected']:
            raise RuntimeError('Select Last Epoch.exe before removing the mod.')
        if state['running']:
            raise RuntimeError('Close Last Epoch before removing the mod.')
        output = io.StringIO()
        try:
            with contextlib.redirect_stdout(output):
                code = le.cmd_uninstall(argparse.Namespace())
        except PermissionError as exc:
            raise RuntimeError('Windows denied access while removing the mod files. Close Last Epoch and any program using its folder, then try again. ' + english_exception(exc)) from exc
        if code:
            raise RuntimeError(output.getvalue().strip().removeprefix('refused: '))
        state = self.status()
        if state['installed']:
            raise RuntimeError('The mod files are still present after removal. Check operations.log.')
        return {'ok': True, 'launcher': state,
                'message': 'EpochPact was removed from the game. Saves, logs and recovery backups were kept. Install mod adds it again.'}

    def report(self, args):
        """Write a local support ZIP and show its folder. Nothing is uploaded."""
        description = args.get('description', '')
        if not isinstance(description, str) or len(description) > support_report.DESCRIPTION_LIMIT:
            raise ValueError('Keep the description under 4,000 characters.')

        def window():
            for pid in le.game_pids():
                hwnd = le.game_window(pid)
                if hwnd:
                    return hwnd
            return None
        path = support_report.create(le.GAME if self.executable else None, self.status(), description,
                                     bool(args.get('screenshot')), window, version=app_version())
        reveal = getattr(self, 'reveal', None) or (lambda folder: os.startfile(folder))
        try:
            reveal(path.parent)
        except OSError:
            pass
        return {'ok': True, 'launcher': self.status(), 'report': path.name,
                'message': f'Support report saved as {path.name}. Its folder is open: attach the ZIP to your message in the EpochPact Discord. '
                           'Your Windows user name is masked; logs can contain character names.'}

    def filters(self, action, args):
        """Loot filter files. Existing filters are never edited or deleted."""
        reveal = getattr(self, 'reveal', None) or (lambda folder: os.startfile(folder))
        if action == 'filters':
            return filter_manager.list_filters()
        if action == 'filter_import':
            return filter_manager.import_filter(args.get('path'))
        if action == 'filter_duplicate':
            return filter_manager.duplicate_filter(args.get('file'))
        if action == 'filter_folder':
            folder = filter_manager.filters_root()
            if not folder.is_dir():
                raise ValueError('Last Epoch has not created its loot filter folder yet. Create a filter in the game first.')
            reveal(folder)
            return {'ok': True, 'message': 'The Last Epoch loot filter folder is open.'}
        result = filter_manager.export_filter(args.get('file')) if action == 'filter_export' else filter_manager.backup_filters()
        try:
            reveal(Path(result['folder']))
        except OSError:
            pass
        return result

    def launch(self):
        self.require_ready()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = le.cmd_launch(argparse.Namespace(offline=True))
        if code:
            raise RuntimeError('Last Epoch did not start. Open it manually in offline mode and refresh the connection.')
        return {'ok': True, 'launcher': self.status(), 'message': 'Last Epoch is running. Load an offline character.'}

    def return_to_game(self):
        """Closing the panel returns focus so Unity can process the reset jobs."""
        if not self.executable:
            return
        for pid in le.game_pids():
            hwnd = le.game_window(pid)
            if hwnd:
                # Called on the foreground UI thread; no input injection or
                # AttachThreadInput, and never start/close a game here.
                api = le.user32
                api.IsIconic.argtypes = [wt.HWND]; api.IsIconic.restype = wt.BOOL
                api.ShowWindow.argtypes = [wt.HWND, ctypes.c_int]; api.ShowWindow.restype = wt.BOOL
                api.SetForegroundWindow.argtypes = [wt.HWND]; api.SetForegroundWindow.restype = wt.BOOL
                if api.IsIconic(hwnd): api.ShowWindow(hwnd, 9)
                api.SetForegroundWindow(hwnd)
                return

    def elevated_install(self):
        if not self.status()['selected']:
            raise RuntimeError('Select Last Epoch.exe before installing.')
        result_path = installer_result_root() / ('setup-install-' + secrets.token_hex(16) + '.json')
        validate_installer_result(str(result_path))
        result_path.parent.mkdir(parents=True, exist_ok=True)
        validate_installer_result(str(result_path))
        arguments = ['--install-plugin', '--game-exe', str(self.executable), '--install-result', str(result_path)]
        if not getattr(sys, 'frozen', False):
            arguments.insert(0, str(le.ROOT / 'epochpact_desktop.py'))
        try:
            code = run_elevated(arguments)
            try:
                result = json.loads(result_path.read_text(encoding='utf-8'))
            except FileNotFoundError as exc:
                if code == INSTALL_RESULT_INVALID:
                    raise RuntimeError('Administrator installation stopped before copying files: the result folder could not be validated (installer code 20). If the Windows prompt used a different administrator account, run EpochPact from that account or use manual installation.') from exc
                if code == INSTALL_RESULT_WRITE_FAILED:
                    state = self.status()
                    if state.get('installed'):
                        return {'ok': True, 'launcher': state,
                                'message': 'The mod files are installed and verified, but Windows could not save the installer receipt (installer code 21). You can launch offline.'}
                    raise RuntimeError('Windows could not save the administrator installer result (installer code 21), and the installed mod files could not be verified. Do not launch the mod; share operations.log with support.') from exc
                suffix = f' (installer code 0x{code:08X})' if isinstance(code, int) else ''
                raise RuntimeError('The administrator installer ended without returning a result' + suffix + '. Extract the entire EpochPact ZIP, check Windows security protection history, and share operations.log with support. No installation was automatically retried.') from exc
            except (OSError, ValueError) as exc:
                raise RuntimeError('The administrator installer result could not be read. Check operations.log before trying another installation. ' + english_exception(exc)) from exc
            if not isinstance(result, dict) or type(result.get('ok')) is not bool:
                raise RuntimeError('The administrator installer returned an invalid result. Check operations.log before trying another installation.')
            if not result.get('ok'):
                raise RuntimeError(result.get('error') or 'Administrator installation failed. Check operations.log for the error details.')
            if code != 0:
                raise RuntimeError(f'The administrator installer returned conflicting results (installer code 0x{code:08X}). Check operations.log before trying another installation.')
            state = self.status()
            if not state['installed']:
                raise RuntimeError('Installation verification failed. The game has not been started.')
            return {'ok': True, 'launcher': state, 'message': 'Player mod installed. You can launch offline now.'}
        finally:
            try:
                result_path.unlink(missing_ok=True)
            except OSError as exc:
                # Receipt cleanup must not replace the real installation error.
                logging.getLogger(__name__).warning('Could not remove installer receipt: %s', english_exception(exc))

    def execute(self, action, args):
        with progression._ipc_lock:
            if action == 'status':
                return {'ok': True, 'launcher': self.status()}
            if action == 'select':
                return self.select(args.get('executable'))
            if action == 'install':
                return self.install()
            if action == 'elevated_install':
                return self.elevated_install()
            if action == 'launch':
                return self.launch()
            if action == 'uninstall':
                return self.uninstall()
            if action == 'report':
                return self.report(args)
            if action in FILTER_ACTIONS:
                return self.filters(action, args)
            raise ValueError('Unknown game setup action.')


FILTER_ACTIONS = ('filters', 'filter_import', 'filter_export', 'filter_duplicate', 'filter_backup', 'filter_folder')


class FeedWindowApi:
    """js_api of a small window (loot feed, damage). The window has no Windows
    frame: its page moves it (drag region), resizes it from the corner grip and
    hides it. Only the page background is see-through (transparent window)."""
    TITLE = 'EpochPact Loot Feed'
    MINIMUM = (220, 120)

    def __init__(self):
        self._window = None  # private: pywebview must not reflect the native window
        self._on_hide = None  # private: tells EpochPact the player hid this window

    def resize_window(self, width, height):
        if self._window is None or not all(isinstance(v, (int, float)) and not isinstance(v, bool) for v in (width, height)):
            return False
        width, height = int(width), int(height)
        if not (self.MINIMUM[0] <= width <= 8000 and self.MINIMUM[1] <= height <= 8000):
            return False
        self._window.resize(width, height)
        return True

    def hide_window(self):
        """The player hid the window (−): it closes and is not reopened at the
        next start; its place, size and options stay remembered."""
        if self._window is None:
            return False
        if self._on_hide is not None:
            self._on_hide()
        self._window.destroy()
        return True


class DamageWindowApi(FeedWindowApi):
    TITLE = 'EpochPact Damage'
    MINIMUM = (260, 120)


def overlay_window(webview, api, url, placement):
    """Create a small window: frameless, transparent background, moved and resized by its page."""
    window = webview.create_window(api.TITLE, url, js_api=api, min_size=api.MINIMUM, on_top=False, frameless=True,
                                   easy_drag=False, transparent=True, background_color='#091310', **placement)
    api._window = window
    size, first = (placement.get('width'), placement.get('height')), [True]

    def loaded():
        # Without a frame the window opens smaller by the frame it was created
        # with: set the remembered size exactly once the page is there.
        if first[0] and all(isinstance(v, int) for v in size):
            first[0] = False
            window.resize(*size)
        reshow_window(api.TITLE)
    window.events.loaded += loaded
    return window


def reshow_window(title, user32=None):
    """A transparent WebView2 window opened while EpochPact runs is sometimes
    drawn with an opaque grey background until it is shown again (measured:
    about half of the windows; after one hide/show all of them were clear).
    Hide and show it once its page has loaded, without taking the focus."""
    if user32 is None:
        if sys.platform != 'win32':
            return False
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        user32.FindWindowW.argtypes = [wt.LPCWSTR, wt.LPCWSTR]; user32.FindWindowW.restype = wt.HWND
        user32.IsWindowVisible.argtypes = [wt.HWND]; user32.IsWindowVisible.restype = wt.BOOL
        user32.ShowWindowAsync.argtypes = [wt.HWND, ctypes.c_int]; user32.ShowWindowAsync.restype = wt.BOOL
    hwnd = user32.FindWindowW(None, title)
    if not hwnd or not user32.IsWindowVisible(hwnd):
        return False  # gone, or hidden while the game is minimised
    SW_HIDE, SW_SHOWNOACTIVATE = 0, 4
    user32.ShowWindowAsync(hwnd, SW_HIDE)
    user32.ShowWindowAsync(hwnd, SW_SHOWNOACTIVATE)
    return True


def _point_on_monitor(x: int, y: int) -> bool:
    """Whether a desktop point lies on a connected monitor (always true off Windows)."""
    if sys.platform != 'win32':
        return True
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    user32.MonitorFromPoint.argtypes = [wt.POINT, wt.DWORD]; user32.MonitorFromPoint.restype = wt.HANDLE
    return bool(user32.MonitorFromPoint(wt.POINT(int(x), int(y)), 0))  # MONITOR_DEFAULTTONULL


def window_placement(entry, width: int, height: int, minimum: tuple[int, int], on_monitor=_point_on_monitor) -> dict:
    """create_window size/position from a remembered entry; unusable parts fall back
    to the default size and a centred window (for example a monitor that is gone)."""
    entry = entry if isinstance(entry, dict) else {}
    w, h = entry.get('width'), entry.get('height')
    sized = all(isinstance(v, int) and not isinstance(v, bool) and low <= v <= 8000 for v, low in ((w, minimum[0]), (h, minimum[1])))
    out = {'width': w if sized else width, 'height': h if sized else height}
    x, y = entry.get('x'), entry.get('y')
    if all(isinstance(v, int) and not isinstance(v, bool) and -20000 < v < 20000 for v in (x, y)) and on_monitor(x + 40, y + 20):
        out.update(x=x, y=y)
    return out


class DesktopDialogs:
    # Loot feed and damage windows: open or closed, place and size, kept in the
    # preferences file and restored when EpochPact starts again.
    _WINDOWS_KEY = 'epochpact.windows.v1'

    def __init__(self):
        # pywebview reflects public js_api attributes recursively. Never expose
        # its native window/.NET object graph to that bridge.
        self._window = None
        self._feed = None
        self._damage = None
        self._url = None
        self._follower = None
        self._preferences = None
        self._shutting_down = False
        self._restored = False
        self._moves = {}
        self._known = {}
        self._move_timer = None
        import threading
        self._state_lock = threading.Lock()

    def _windows(self) -> dict:
        state = self._preferences.get_json(self._WINDOWS_KEY, {}) if self._preferences is not None else {}
        return state if isinstance(state, dict) else {}

    def _remember(self, name, **fields):
        if self._preferences is None:
            return
        with self._state_lock:
            state = self._windows()
            entry = state.get(name) if isinstance(state.get(name), dict) else {}
            entry.update(fields)
            state[name] = entry
            try:
                self._preferences.set_json(self._WINDOWS_KEY, state)
            except (OSError, ValueError):
                logging.getLogger(__name__).exception('Window place could not be saved')

    def _placed(self, name, fields):
        # Moving and resizing fire many events: write once the window settles.
        import threading
        if any(isinstance(v, int) and v <= -10000 for v in fields.values()):
            return  # minimised windows report -32000
        with self._state_lock:
            # pywebview reports logical pixels rounded down: on a scaled desktop
            # a reopened window reads a pixel smaller. Ignore that, or the size
            # would shrink by a pixel at every start.
            known = self._known.setdefault(name, {})
            if all(isinstance(known.get(k), int) and abs(v - known[k]) <= 2 for k, v in fields.items()):
                return
            known.update(fields)
            self._moves.setdefault(name, {}).update(fields)
            if self._move_timer is not None:
                self._move_timer.cancel()
            self._move_timer = threading.Timer(0.6, self._flush_moves)
            self._move_timer.daemon = True
            self._move_timer.start()

    def _flush_moves(self):
        with self._state_lock:
            moves, self._moves = self._moves, {}
        for name, fields in moves.items():
            self._remember(name, **fields)

    def _track(self, name, window, placement=None):
        with self._state_lock:
            self._known[name] = dict(placement or {})
        window.events.moved += lambda x, y: self._placed(name, {'x': x, 'y': y})
        window.events.resized += lambda width, height: self._placed(name, {'width': width, 'height': height})
        self._remember(name, open=True)

    def _prepare_close(self, closing: bool):
        """The main window is closing: windows that close with it stay remembered as open."""
        self._shutting_down = closing
        if closing:
            self._flush_moves()

    def _close_windows(self):
        """EpochPact's main window has closed: the loot and damage windows close
        with it (they have no title bar of their own) and stay remembered as open."""
        self._shutting_down = True
        for window in (self._feed, self._damage):
            if window is not None:
                try:
                    window.destroy()
                except Exception:
                    logging.getLogger(__name__).exception('Small window could not be closed')

    def _restore_windows(self):
        """Reopen the loot feed / damage windows that were open when EpochPact closed."""
        if self._restored:
            return
        self._restored = True
        state = self._windows()
        if (state.get('feed') or {}).get('open'):
            self.open_loot_feed()
        if (state.get('damage') or {}).get('open'):
            self.open_damage_window()

    def _follow_game(self):
        """The small windows stay above the game only (not above EpochPact or other programs)."""
        if self._follower is None:
            def find_game():
                for pid in le.game_pids():
                    hwnd = le.game_window(pid)
                    if hwnd:
                        return hwnd
                return None
            self._follower = overlay_follow.OverlayFollower((FeedWindowApi.TITLE, DamageWindowApi.TITLE), find_game)
        self._follower.start()

    def open_loot_feed(self):
        """A small always-on-top window over a windowed/borderless game."""
        import webview
        if self._feed is not None:
            try:
                self._feed.restore()
                self._feed.show()
                return True
            except Exception:
                self._feed = None
        if not self._url:
            return False
        placement = window_placement(self._windows().get('feed'), 380, 520, FeedWindowApi.MINIMUM)
        api = FeedWindowApi()
        api._on_hide = lambda: self._hidden('feed')
        self._feed = overlay_window(webview, api, self._url + '/feed', placement)
        self._feed.events.closed += self._feed_closed
        self._track('feed', self._feed, placement)
        self._follow_game()
        return True

    def _feed_closed(self):
        self._feed = None
        self._closed('feed')

    # Only hiding (− in the window, Hide in EpochPact) forgets a window. Any
    # other close — EpochPact's X, the taskbar's "Close all windows", Alt+F4,
    # in whatever order Windows closes them — keeps it for the next start.
    def _hidden(self, name):
        self._remember(name, open=False)
        logging.getLogger(__name__).info('%s window hidden by the player', name)

    def _closed(self, name):
        state = self._windows().get(name) or {}
        logging.getLogger(__name__).info('%s window closed (reopens at the next start: %s, EpochPact closing: %s)',
                                         name, bool(state.get('open')), self._shutting_down)

    def open_damage_window(self):
        """The damage session over a windowed/borderless game."""
        import webview
        if self._damage is not None:
            try:
                self._damage.restore()
                self._damage.show()
                return True
            except Exception:
                self._damage = None
        if not self._url:
            return False
        placement = window_placement(self._windows().get('damage'), 520, 360, DamageWindowApi.MINIMUM)
        api = DamageWindowApi()
        api._on_hide = lambda: self._hidden('damage')
        self._damage = overlay_window(webview, api, self._url + '/damage', placement)
        self._damage.events.closed += self._damage_closed
        self._track('damage', self._damage, placement)
        self._follow_game()
        return True

    def _damage_closed(self):
        self._damage = None
        self._closed('damage')

    def overlay_windows(self):
        """Which small windows are open (for the Show/Hide buttons in EpochPact)."""
        return {'feed': self._feed is not None, 'damage': self._damage is not None}

    def hide_loot_feed(self):
        window = self._feed
        if window is not None:
            self._hidden('feed')
            window.destroy()
        return True

    def hide_damage_window(self):
        window = self._damage
        if window is not None:
            self._hidden('damage')
            window.destroy()
        return True

    def choose_filter_file(self):
        import webview
        start = filter_manager.filters_root().parent
        chosen = self._window.create_file_dialog(webview.FileDialog.OPEN, directory=str(start if start.is_dir() else Path.home()),
                                                allow_multiple=False, file_types=('Loot filters and backups (*.xml;*.zip)',))
        return chosen[0] if chosen else None

    def choose_game_executable(self):
        import webview
        chosen = self._window.create_file_dialog(webview.FileDialog.OPEN, directory=str(picker_directory()),
                                                allow_multiple=False, file_types=('Last Epoch executable (*.exe)',))
        return chosen[0] if chosen else None
