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
        result = {'selected': False, 'executable': str(self.executable or ''), 'installed': False,
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
            result['disabled'] = (le.GAME / 'EpochPact/disabled').exists()
            result['installed'] = (le.is_ours(loader) and core.is_file() and
                                   hashlib.sha256(core.read_bytes()).hexdigest() == digest)
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
                    'error': 'Windows denied access while installing in the game folder. Folder permissions or security software may be responsible. You can explicitly try Install as administrator. ' + english_exception(exc),
                    'launcher': self.status(check_running=False)}
        if code:
            raise RuntimeError(output.getvalue().strip().removeprefix('refused: '))
        state = self.status()
        if not state['installed']:
            raise RuntimeError('Installation verification failed. The game has not been started.')
        return {'ok': True, 'launcher': state, 'message': 'Player mod installed. You can launch offline now.'}

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
            raise ValueError('Unknown game setup action.')


class DesktopDialogs:
    def __init__(self):
        # pywebview reflects public js_api attributes recursively. Never expose
        # its native window/.NET object graph to that bridge.
        self._window = None

    def choose_game_executable(self):
        import webview
        chosen = self._window.create_file_dialog(webview.FileDialog.OPEN, directory=str(picker_directory()),
                                                allow_multiple=False, file_types=('Last Epoch executable (*.exe)',))
        return chosen[0] if chosen else None
