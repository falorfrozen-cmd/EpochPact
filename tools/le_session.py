r"""Install EpochPact into Last Epoch, run a game session, and close it cleanly.

    py -3 tools/le_session.py status
    py -3 tools/le_session.py install            # build\ -> <game>\version.dll + <game>\EpochPact\
    py -3 tools/le_session.py uninstall
    py -3 tools/le_session.py launch             # backs the saves up, starts the game through Steam
    py -3 tools/le_session.py wait-dump [--timeout 600]
    py -3 tools/le_session.py close [--timeout 60]

Never overwrites a version.dll that is not EpochPact's, and refuses to install or
uninstall while the game runs. The game is closed with WM_CLOSE to its window, the same
as its own close button, so it saves and exits normally.
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import datetime as _dt
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Last Epoch")
SAVES = Path(os.environ["USERPROFILE"]) / "AppData" / "LocalLow" / "Eleventh Hour Games" / "Last Epoch" / "Saves"
ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "native" / "build"
OUT = ROOT / "research-out"
STEAM_APP = 899770
EXE = "Last Epoch.exe"
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
WM_CLOSE = 0x0010
SYNCHRONIZE = 0x00100000
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


def game_pids() -> list[int]:
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {EXE}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, errors="replace").stdout
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


def cmd_install(_: argparse.Namespace) -> int:
    if refuse_if_running():
        return 2
    loader, core_src = BUILD / "version.dll", BUILD / "EpochPact.Core.dll"
    if not loader.is_file() or not core_src.is_file():
        print("refused: build first (native\\build.bat)")
        return 2
    target = GAME / "version.dll"
    if target.exists() and not is_ours(target):
        print("refused: the game folder has a version.dll that is not EpochPact's (another mod loader?)")
        return 2
    (GAME / "EpochPact").mkdir(exist_ok=True)
    shutil.copy2(loader, target)
    shutil.copy2(core_src, GAME / "EpochPact" / "EpochPact.Core.dll")
    print(f"installed: {target} and {GAME / 'EpochPact' / 'EpochPact.Core.dll'}")
    return 0


def cmd_uninstall(_: argparse.Namespace) -> int:
    if refuse_if_running():
        return 2
    target = GAME / "version.dll"
    if target.exists():
        if not is_ours(target):
            print("refused: version.dll is not EpochPact's; left alone")
            return 2
        target.unlink()
    core = GAME / "EpochPact" / "EpochPact.Core.dll"
    if core.exists():
        core.unlink()
    print("uninstalled (logs and dumps in <game>\\EpochPact are kept)")
    return 0


def backup_saves() -> Path | None:
    if not SAVES.is_dir():
        return None
    dest = OUT / "saves-backups" / _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    shutil.copytree(SAVES, dest)
    return dest


def cmd_launch(_: argparse.Namespace) -> int:
    if game_pids():
        print(f"already running: {game_pids()}")
        return 0
    dest = backup_saves()
    print(f"saves backed up: {dest}")
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
        print("no game window to close")
        return 1
    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    if handle and kernel32.WaitForSingleObject(handle, int(args.timeout * 1000)) == 0:
        code = wt.DWORD()
        kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
        kernel32.CloseHandle(handle)
        print(f"closed: exit code {code.value} (0x{code.value:08X})")
        return 0
    print(f"still running after {args.timeout} s")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("install").set_defaults(fn=cmd_install)
    sub.add_parser("uninstall").set_defaults(fn=cmd_uninstall)
    sub.add_parser("launch").set_defaults(fn=cmd_launch)
    w = sub.add_parser("wait-dump")
    w.add_argument("--timeout", type=float, default=600)
    w.set_defaults(fn=cmd_wait_dump)
    c = sub.add_parser("close")
    c.add_argument("--timeout", type=float, default=60)
    c.set_defaults(fn=cmd_close)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
