r"""Install EpochPact into Last Epoch, run a game session, and close it cleanly.

    py -3 tools/le_session.py status
    py -3 tools/le_session.py install            # build\ -> <game>\version.dll + <game>\EpochPact\
    py -3 tools/le_session.py uninstall
    py -3 tools/le_session.py launch             # backs the saves up, starts the game through Steam
    py -3 tools/le_session.py launch --offline   # backs up, starts the installed game with --offline
    py -3 tools/le_session.py wait-dump [--timeout 600]
    py -3 tools/le_session.py close [--timeout 60]
    py -3 tools/le_session.py cmd <command ...> [--timeout 10]   # through <game>\EpochPact\ipc
    py -3 tools/le_session.py restore-saves <backup folder>       # put a session's saves back

Never overwrites a version.dll that is not EpochPact's, and refuses to install or
uninstall while the game runs. The game is closed with WM_CLOSE to its window, the same
as its own close button, so it saves and exits normally.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import datetime as _dt
import json
import logging
import re
import secrets
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    from .app_paths import resource_root, runtime_root
    from .game_compatibility import require_supported_build
except ImportError:
    from app_paths import resource_root, runtime_root
    from game_compatibility import require_supported_build

EXE = "Last Epoch.exe"


def _steam_root() -> Path | None:
    import winreg
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
            return Path(winreg.QueryValueEx(key, 'SteamPath')[0])
    except OSError:
        return None


def resolve_game_dir(env=None, steam_root=None) -> Path:
    env = os.environ if env is None else env
    configured = env.get('EPOCHPACT_GAME_DIR', '').strip().strip('"')
    if configured:
        path = Path(configured).expanduser().resolve()
        if not (path / EXE).is_file():
            raise FileNotFoundError(f'EPOCHPACT_GAME_DIR must contain {EXE}: {path}')
        return path
    steam = Path(steam_root) if steam_root is not None else _steam_root()
    libraries = [steam] if steam else []
    if steam:
        try:
            text = (steam / 'steamapps/libraryfolders.vdf').read_text(encoding='utf-8-sig')
            # Current Steam uses numbered objects with a path key. Older
            # library files use a numbered key followed directly by the path.
            for key, value in re.findall(r'"([^"\\]+)"\s*"((?:\\.|[^"\\])*)"', text):
                if key == 'path' or key.isdigit():
                    path = Path(value.replace('\\\\', '\\').replace('\\"', '"'))
                    if path.is_absolute():
                        libraries.append(path)
        except (OSError, UnicodeError):
            pass
    for library in libraries:
        candidate = library / 'steamapps/common/Last Epoch'
        if (candidate / EXE).is_file():
            return candidate.resolve()
    return Path(r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch")


GAME = resolve_game_dir()
SAVES = Path(os.environ["USERPROFILE"]) / "AppData" / "LocalLow" / "Eleventh Hour Games" / "Last Epoch" / "Saves"
ROOT = resource_root()
BUILD = ROOT / "native" / "build"
OUT = runtime_root() / ('backups' if getattr(sys, 'frozen', False) else 'research/live')
STEAM_APP = 899770
MARK = "EpochPact\\EpochPact.Core.dll".encode("utf-16-le")  # only EpochPact's loader carries this

user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
WNDENUMPROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
user32.EnumWindows.argtypes = [WNDENUMPROC, wt.LPARAM]
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
kernel32.OpenProcess.restype = wt.HANDLE
kernel32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
kernel32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
kernel32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.CreateMutexW.restype = wt.HANDLE
kernel32.CreateMutexW.argtypes = [ctypes.c_void_p, wt.BOOL, wt.LPCWSTR]
kernel32.ReleaseMutex.argtypes = [wt.HANDLE]
WM_CLOSE = 0x0010
SYNCHRONIZE = 0x00100000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def game_pids() -> list[int]:
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {EXE}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, errors="replace",
                         creationflags=subprocess.CREATE_NO_WINDOW).stdout
    pids = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].lower() == EXE.lower():
            pids.append(int(parts[1]))
    return pids


def game_window(pid: int) -> int | None:
    found: list[int] = []

    def visit(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid:
            cls = ctypes.create_unicode_buffer(64)
            user32.GetClassNameW(hwnd, cls, 64)
            if cls.value == "UnityWndClass":
                found.append(hwnd)
                return False
        return True

    user32.EnumWindows(WNDENUMPROC(visit), 0)
    return found[0] if found else None


def is_ours(dll: Path) -> bool:
    return dll.is_file() and MARK in dll.read_bytes()


def is_ours_core(dll: Path) -> bool:
    return dll.is_file() and b'EpochPact_Start\x00' in dll.read_bytes()


def cmd_status(_: argparse.Namespace) -> int:
    loader = GAME / "version.dll"
    print(f"game running: {game_pids() or 'no'}")
    print(f"loader: {'EpochPact' if is_ours(loader) else ('FOREIGN version.dll' if loader.exists() else 'not installed')}")
    core = GAME / "EpochPact" / "EpochPact.Core.dll"
    print(f"core: {'installed' if core.is_file() else 'not installed'}"
          f"{' (disabled)' if (GAME / 'EpochPact' / 'disabled').exists() else ''}")
    done = GAME / "EpochPact" / "dump" / "done.txt"
    print(f"dump: {done.read_text().split() if done.is_file() else 'not written'}")
    return 0


def refuse_if_running() -> bool:
    if game_pids():
        print("refused: Last Epoch is running; close it first")
        return True
    return False


def build_artifact(flavor='player') -> tuple[Path, str]:
    if flavor not in ('player', 'research', 'test'):
        raise ValueError('Unknown build flavor.')
    path = BUILD / flavor / 'EpochPact.Core.dll'
    metadata = json.loads((path.parent / 'build-info.json').read_text(encoding='utf-8'))
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if metadata.get('flavor') != flavor or metadata.get('sha256') != digest:
        raise ValueError('Build flavor or SHA256 mismatch; rebuild before installing.')
    return path, digest


def cmd_install(args: argparse.Namespace) -> int:
    if refuse_if_running():
        return 2
    flavor = getattr(args, 'flavor', 'player')
    if flavor == 'player':
        try:
            require_supported_build(GAME)
        except (OSError, ValueError) as exc:
            print(f'refused: {exc}')
            return 2
    loader = BUILD / 'version.dll'
    try:
        core_src, digest = build_artifact(flavor)
    except (OSError, ValueError) as exc:
        print(f'refused: build native\\build.bat {flavor} first: {exc}')
        return 2
    if not loader.is_file() or not (GAME / EXE).is_file():
        print('refused: loader build or game executable missing')
        return 2
    target = GAME / "version.dll"
    if target.exists() and not is_ours(target):
        print("refused: the game folder has a version.dll that is not EpochPact's (another mod loader?)")
        return 2
    mod_dir = GAME / 'EpochPact'
    old_core = mod_dir / 'EpochPact.Core.dll'
    if ((mod_dir.exists() and _linked(mod_dir)) or
        (target.exists() and _linked(target)) or
        (old_core.exists() and (_linked(old_core) or not is_ours_core(old_core))) or
        ((mod_dir / 'install-backups').exists() and _linked(mod_dir / 'install-backups'))):
        print('refused: linked mod path or unrecognized core file; left alone')
        return 2
    mod_dir.mkdir(exist_ok=True)
    record = {'flavor': flavor, 'sha256': digest, 'source': str(core_src), 'time': time.time()}
    # Prepare every file and its previous version before changing the installed
    # mod. Publish the loader last; a denied copy must not leave mixed versions.
    stage = mod_dir / ('.install-' + secrets.token_hex(16))
    stage.mkdir()
    destinations = [mod_dir / 'EpochPact.Core.dll', mod_dir / 'installed-build.json', target]
    committed = []
    preserve_stage = False
    try:
        shutil.copy2(core_src, stage / 'new-0')
        (stage / 'new-1').write_text(json.dumps(record) + '\n', encoding='utf-8')
        shutil.copy2(loader, stage / 'new-2')
        for index, destination in enumerate(destinations):
            if destination.exists():
                shutil.copy2(destination, stage / f'old-{index}')
        # Retain successful-update recovery files as well as the transactional
        # rollback copies. This directory contains mod files only, never saves.
        old_files = [(index, destination) for index, destination in enumerate(destinations)
                     if (stage / f'old-{index}').exists()]
        if old_files:
            recovery = mod_dir / 'install-backups' / stage.name.removeprefix('.install-')
            recovery.mkdir(parents=True)
            for index, destination in old_files:
                shutil.copy2(stage / f'old-{index}', recovery / destination.name)
            (recovery / 'README.txt').write_text(
                'Close Last Epoch first. To restore this previous mod installation, copy version.dll\n'
                'to the game root, and EpochPact.Core.dll / installed-build.json to its EpochPact folder.\n'
                'Only restore files present in this backup. Player saves are not included or changed.\n', encoding='utf-8')
        if refuse_if_running():
            return 2
        if target.exists() and not is_ours(target):
            print('refused: another loader appeared during installation; left alone')
            return 2
        for index, destination in enumerate(destinations):
            os.replace(stage / f'new-{index}', destination)
            committed.append((index, destination))
    except OSError as original:
        failures = []
        for index, destination in reversed(committed):
            try:
                previous = stage / f'old-{index}'
                if previous.exists():
                    os.replace(previous, destination)
                else:
                    destination.unlink(missing_ok=True)
            except OSError as exc:
                failures.append(str(exc))
        if failures:
            preserve_stage = True
            raise RuntimeError(f'Installation failed and recovery could not finish. Close the game and reinstall. Previous files are kept in {stage}.') from original
        raise
    finally:
        if not preserve_stage:
            for file in stage.iterdir():
                file.unlink()
            stage.rmdir()
    print(f"installed: {flavor}, SHA256 {digest}: {target} and {GAME / 'EpochPact' / 'EpochPact.Core.dll'}")
    return 0


def cmd_uninstall(_: argparse.Namespace) -> int:
    if refuse_if_running():
        return 2
    target = GAME / "version.dll"
    core = GAME / "EpochPact" / "EpochPact.Core.dll"
    if ((core.parent.exists() and _linked(core.parent)) or
        (target.exists() and _linked(target)) or
        (core.exists() and (_linked(core) or not is_ours_core(core)))):
        print('refused: linked mod path or unrecognized core file; left alone')
        return 2
    if target.exists():
        if not is_ours(target):
            print("refused: version.dll is not EpochPact's; left alone")
            return 2
        target.unlink()
    if core.exists():
        core.unlink()
    print("uninstalled (logs and dumps in <game>\\EpochPact are kept)")
    return 0


def backup_saves() -> Path | None:
    if not SAVES.is_dir():
        return None
    dest = OUT / "saves-backups" / (_dt.datetime.now().strftime("%Y%m%d-%H%M%S-%f") + "-" + secrets.token_hex(4))
    shutil.copytree(SAVES, dest)
    # Only new, explicitly managed backups participate in automatic retention.
    # Historical owner/test backups are never removed merely for their age.
    (dest / '.epochpact-retention').write_text('1\n', encoding='ascii')
    try:
        prune_session_backups(dest.parent, keep=20, newest=dest)
    except (OSError, ValueError) as exc:
        logging.getLogger(__name__).warning('Backup kept; retention deferred: %s', exc)
    return dest


def _linked(path: Path) -> bool:
    # st_reparse_tag works on older supported Python versions too. Cloud
    # placeholders are not junctions and may remain under OneDrive.
    return path.is_symlink() or getattr(path.lstat(), 'st_reparse_tag', 0) in (0xA0000003, 0xA000000C)


def prune_session_backups(root: Path, *, keep: int, newest: Path) -> None:
    if keep < 1 or _linked(root):
        raise ValueError('Unsafe backup retention root or limit.')
    resolved_root = root.resolve(strict=True)
    candidates = []
    for path in root.iterdir():
        try:
            if not path.is_dir() or _linked(path):
                continue
            if path.resolve().parent != resolved_root:
                continue
            marker = path / '.epochpact-retention'
            if not marker.is_file() or _linked(marker) or marker.read_text(encoding='ascii') != '1\n':
                continue
            # Refuse the entire tree if it contains any linked directory/file.
            if any(_linked(p) for p in path.rglob('*')):
                continue
            candidates.append((marker.stat().st_mtime_ns, path))
        except (OSError, UnicodeError):
            continue
    candidates.sort(key=lambda entry: (entry[0], entry[1].name), reverse=True)
    for _, path in candidates[keep:]:
        try:
            if path == newest or _linked(path) or path.resolve().parent != resolved_root or any(_linked(p) for p in path.rglob('*')):
                continue
            shutil.rmtree(path)
        except OSError as exc:
            logging.getLogger(__name__).warning('Backup kept because retention failed: %s', exc)


def cmd_launch(args: argparse.Namespace) -> int:
    if game_pids():
        print(f"already running: {game_pids()}")
        return 0
    require_supported_build(GAME)
    dest = backup_saves()
    print(f"saves backed up: {dest}")
    if args.offline:
        os.startfile(str(GAME / EXE), arguments="--offline", cwd=str(GAME))
    else:
        os.startfile(f"steam://rungameid/{STEAM_APP}")
    for _ in range(240):
        pids = game_pids()
        if pids:
            print(f"started: pid {pids[0]}")
            return 0
        time.sleep(0.5)
    print("the game did not start within 120 s")
    return 1


def cmd_wait_dump(args: argparse.Namespace) -> int:
    done = GAME / "EpochPact" / "dump" / "done.txt"
    log = GAME / "EpochPact" / "logs" / "core.log"
    deadline = time.time() + args.timeout
    while time.time() < deadline:
        if done.is_file():
            print(done.read_text().strip().replace("\n", ", "))
            return 0
        if not game_pids():
            print("the game exited before the dump finished")
            break
        time.sleep(2)
    if log.is_file():
        print("core.log tail:\n" + "\n".join(log.read_text(errors="replace").splitlines()[-15:]))
    return 1


def cmd_close(args: argparse.Namespace) -> int:
    pids = game_pids()
    if not pids:
        print("not running")
        return 0
    pid = pids[0]
    handle = kernel32.OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    hwnd = game_window(pid)
    if not hwnd:
        if handle: kernel32.CloseHandle(handle)
        print("no game window to close")
        return 1
    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    if handle and kernel32.WaitForSingleObject(handle, int(args.timeout * 1000)) == 0:
        code = wt.DWORD()
        read_exit = kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        if not read_exit:
            print("closed, but the game's exit code could not be verified")
            return 1
        print(f"closed: exit code {code.value} (0x{code.value:08X})")
        return 0 if code.value == 0 else 1
    if handle: kernel32.CloseHandle(handle)
    print(f"still running after {args.timeout} s")
    return 1


def modern_ipc(ipc: Path) -> bool:
    """A stale capability file from a stopped DLL is never trusted."""
    try:
        protocol = json.loads((ipc / "protocol.json").read_text(encoding="utf-8"))
        if protocol.get("version") != 2 or type(protocol.get("pid")) is not int:
            return False
        handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, protocol["pid"])
        if not handle:
            return False
        try:
            code = wt.DWORD()
            return bool(kernel32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259
        finally:
            kernel32.CloseHandle(handle)
    except (OSError, ValueError, TypeError):
        return False


def send_packet(ipc: Path, command: str, timeout: float) -> str | None:
    nonce = secrets.token_hex(16)
    tmp = ipc / "cmd.tmp"
    tmp.write_text(f"@{nonce} {command}\n", encoding="utf-8")
    deadline = time.monotonic() + timeout
    # A failed atomic rename has not submitted the command. Retry publication
    # with the SAME nonce; never publish it again once the rename succeeds.
    while True:
        try:
            os.replace(tmp, ipc / "cmd.txt")
            break
        except PermissionError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(.01)
    while time.monotonic() < deadline:
        try:
            packet = json.loads((ipc / "reply.json").read_text(encoding="utf-8"))
            if packet.get("nonce") == nonce and isinstance(packet.get("reply"), str):
                return packet["reply"].strip()
        except (OSError, ValueError):
            pass  # Atomic publication or a transient Windows lock; never resend.
        time.sleep(0.01)
    return None


def send(command: str, timeout: float = 10.0) -> str | None:
    """Serialize request/reply pairs across all UI and CLI processes for this game."""
    identity = hashlib.sha256(os.path.normcase(str((GAME / 'EpochPact/ipc').resolve())).encode()).hexdigest()
    handle = kernel32.CreateMutexW(None, False, 'Local\\EpochPact.IPC.' + identity)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    start = time.monotonic()
    acquired = False
    try:
        waited = kernel32.WaitForSingleObject(handle, max(0, int(timeout * 1000)))
        if waited not in (0, 0x80):  # WAIT_OBJECT_0 / WAIT_ABANDONED: ownership acquired.
            if waited == 0x102:
                raise TimeoutError('The game connection is busy in another EpochPact process. This command was not submitted.')
            raise ctypes.WinError(ctypes.get_last_error())
        acquired = True
        remaining = timeout - (time.monotonic() - start)
        return _send_locked(command, remaining) if remaining > 0 else None
    finally:
        if acquired:
            kernel32.ReleaseMutex(handle)
        kernel32.CloseHandle(handle)


def _send_locked(command: str, timeout: float) -> str | None:
    """Writes one command to cmd.txt and returns its reply from out.txt (None on timeout)."""
    ipc = GAME / "EpochPact" / "ipc"
    ipc.mkdir(parents=True, exist_ok=True)
    if modern_ipc(ipc):
        return send_packet(ipc, command, timeout)
    out = ipc / "out.txt"
    start = out.stat().st_size if out.exists() else 0
    tmp = ipc / "cmd.tmp"
    tmp.write_text(command + "\n", encoding="utf-8")
    os.replace(tmp, ipc / "cmd.txt")
    marker = f"> {command}\r\n"
    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(0.15)
        if not out.exists():
            continue
        try:
            with open(out, "rb") as f:
                f.seek(start)
                text = f.read().decode("utf-8", errors="replace")
        except PermissionError:
            # The native append may briefly hold the file exclusively on Windows.
            # Retry the read within the deadline; never resend a mutation.
            continue
        at = text.find(marker)
        if at < 0:
            continue
        body = text[at + len(marker):]
        nxt = body.find("\r\n> ")
        if nxt >= 0:
            return body[:nxt].strip()
        if body.endswith("\r\n"):
            time.sleep(0.2)  # a multi-line reply is written in one append; settle once
            try:
                with open(out, "rb") as f:
                    f.seek(start)
                    text = f.read().decode("utf-8", errors="replace")
            except PermissionError:
                continue
            return text[text.find(marker) + len(marker):].split("\r\n> ")[0].strip()
    return None


def cmd_cmd(args: argparse.Namespace) -> int:
    reply = send(" ".join(args.words), args.timeout)
    if reply is None:
        print("no reply (is the game running with EpochPact?)")
        return 1
    print(reply)
    return 0


def cmd_restore_saves(args: argparse.Namespace) -> int:
    if refuse_if_running():
        return 2
    src = Path(args.backup)
    if not src.is_dir() or not any(src.iterdir()):
        print(f"refused: {src} is not a saves backup")
        return 2
    shutil.rmtree(SAVES)
    shutil.copytree(src, SAVES)
    print(f"saves restored from {src}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    install = sub.add_parser("install")
    install.add_argument('--flavor', choices=('player', 'research', 'test'), default='player')
    install.set_defaults(fn=cmd_install)
    sub.add_parser("uninstall").set_defaults(fn=cmd_uninstall)
    launch = sub.add_parser("launch")
    launch.add_argument("--offline", action="store_true", help="use the game's official --offline launch argument")
    launch.set_defaults(fn=cmd_launch)
    w = sub.add_parser("wait-dump")
    w.add_argument("--timeout", type=float, default=600)
    w.set_defaults(fn=cmd_wait_dump)
    c = sub.add_parser("close")
    c.add_argument("--timeout", type=float, default=60)
    c.set_defaults(fn=cmd_close)
    k = sub.add_parser("cmd")
    k.add_argument("words", nargs="+")
    k.add_argument("--timeout", type=float, default=10)
    k.set_defaults(fn=cmd_cmd)
    r = sub.add_parser("restore-saves")
    r.add_argument("backup")
    r.set_defaults(fn=cmd_restore_saves)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
