"""Local support report: one ZIP the player can attach to a Discord message.

Nothing is uploaded. Saves, recipe files and game binaries are never included.
The Windows user name, computer name and profile paths are masked in every text
file; character names that appear in logs are kept so the report stays useful,
and the interface says so before the player shares it.
"""
from __future__ import annotations

import ctypes
import ctypes.wintypes as wt
import datetime as _dt
import json
import os
import platform
import re
import struct
import sys
import zipfile
import zlib
from pathlib import Path

from .app_paths import user_root, runtime_root

LOG_LIMIT = 512 * 1024
DESCRIPTION_LIMIT = 4000


def _tail(path: Path, limit: int = LOG_LIMIT) -> str | None:
    try:
        with path.open('rb') as stream:
            stream.seek(0, os.SEEK_END)
            size = stream.tell()
            stream.seek(max(0, size - limit))
            data = stream.read()
    except OSError:
        return None
    text = data.decode('utf-8', errors='replace')
    if size > limit:
        text = f'[first {size - limit} bytes omitted]\n' + text.split('\n', 1)[-1]
    return text


def redactor():
    names = {n for n in (os.environ.get('USERNAME'), os.environ.get('COMPUTERNAME'), platform.node()) if n and len(n) >= 2}
    home = str(Path.home())
    patterns = [(re.compile(re.escape(home), re.IGNORECASE), '%USERPROFILE%'),
                (re.compile(re.escape(home.replace('\\', '/')), re.IGNORECASE), '%USERPROFILE%')]
    for name in sorted(names, key=len, reverse=True):
        patterns.append((re.compile(r'(?<![A-Za-z0-9])' + re.escape(name) + r'(?![A-Za-z0-9])', re.IGNORECASE), '<user>'))

    def redact(text: str) -> str:
        for pattern, replacement in patterns:
            text = pattern.sub(replacement, text)
        return text
    return redact


def _png(width: int, height: int, bgra: bytes) -> bytes:
    rows = bytearray()
    stride = width * 4
    for y in range(height):
        row = bgra[y * stride:(y + 1) * stride]
        rows.append(0)
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]; rgb[1::3] = row[1::4]; rgb[2::3] = row[0::4]
        rows += rgb

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(bytes(rows), 6)) + chunk(b'IEND', b''))


def capture_window(hwnd: int) -> bytes | None:
    """The game window's own rendering as PNG, even behind other windows (not when minimised)."""
    if sys.platform != 'win32' or not hwnd:
        return None
    user32 = ctypes.WinDLL('user32', use_last_error=True)
    gdi32 = ctypes.WinDLL('gdi32', use_last_error=True)
    user32.IsIconic.argtypes = [wt.HWND]
    if user32.IsIconic(hwnd):
        return None
    rect = wt.RECT()
    user32.GetClientRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
        return None
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0 or width * height > 16_000_000:
        return None
    user32.GetDC.restype = wt.HDC; user32.GetDC.argtypes = [wt.HWND]
    gdi32.CreateCompatibleDC.restype = wt.HDC; gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
    gdi32.CreateCompatibleBitmap.restype = wt.HBITMAP; gdi32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
    gdi32.SelectObject.restype = wt.HGDIOBJ; gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
    user32.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]; user32.PrintWindow.restype = wt.BOOL
    gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]; gdi32.DeleteDC.argtypes = [wt.HDC]
    user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
    screen = user32.GetDC(None)
    memory = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    old = gdi32.SelectObject(memory, bitmap)
    try:
        PW_CLIENTONLY, PW_RENDERFULLCONTENT = 0x1, 0x2
        if not user32.PrintWindow(hwnd, memory, PW_CLIENTONLY | PW_RENDERFULLCONTENT):
            return None

        class BITMAPINFOHEADER(ctypes.Structure):
            _fields_ = [('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG), ('biPlanes', wt.WORD),
                        ('biBitCount', wt.WORD), ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD),
                        ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG), ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD)]
        header = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        buffer = ctypes.create_string_buffer(width * height * 4)
        gdi32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p, ctypes.c_void_p, wt.UINT]
        if gdi32.GetDIBits(memory, bitmap, 0, height, buffer, ctypes.byref(header), 0) != height:
            return None
        return _png(width, height, buffer.raw)
    finally:
        gdi32.SelectObject(memory, old)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory)
        user32.ReleaseDC(None, screen)


def create(game_dir: Path | None, setup_state: dict, description: str = '', screenshot: bool = False,
           window=None, output: Path | None = None, version: str = '') -> Path:
    """Write the report ZIP and return its path. `window` returns the game window handle."""
    if not isinstance(description, str):
        raise ValueError('Invalid description.')
    description = description.strip()[:DESCRIPTION_LIMIT]
    redact = redactor()
    # Next to the in-game (hotkey) reports in the game folder; the user folder
    # when the game is unknown or its folder cannot be written.
    folder = output
    if folder is None and game_dir:
        try:
            folder = Path(game_dir) / 'EpochPact' / 'reports'
            folder.mkdir(parents=True, exist_ok=True)
        except OSError:
            folder = None
    folder = folder or (user_root() / 'reports')
    folder.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    path = folder / f'EpochPact-report-{stamp}.zip'
    counter = 1
    while path.exists():
        counter += 1
        path = folder / f'EpochPact-report-{stamp}-{counter}.zip'
    summary = {
        'epochpact': version, 'created': _dt.datetime.now().isoformat(timespec='seconds'),
        'windows': platform.platform(), 'python': platform.python_version(),
        'frozen': bool(getattr(sys, 'frozen', False)),
        'setup': {k: v for k, v in (setup_state or {}).items() if k not in ('executable', 'problem')},
        'gameFolderSelected': bool(game_dir),
    }
    logs = []
    if game_dir:
        for name in ('core.log', 'loader.log'):
            logs.append((f'game-logs/{name}', Path(game_dir) / 'EpochPact/logs' / name))
        for name in ('installed-build.json',):
            logs.append((f'game-logs/{name}', Path(game_dir) / 'EpochPact' / name))
    logs += [('app-logs/startup.log', user_root() / 'startup.log'),
             ('app-logs/operations.log', runtime_root() / 'live/ui/operations.log')]
    included = []
    with zipfile.ZipFile(path, 'x', zipfile.ZIP_DEFLATED) as archive:
        if description:
            archive.writestr('description.txt', redact(description) + '\n')
        for name, source in logs:
            text = _tail(source)
            if text is not None:
                archive.writestr(name, redact(text))
                included.append(name)
        if screenshot and window is not None:
            try:
                image = capture_window(window())
            except OSError:
                image = None
            if image:
                archive.writestr('screenshot.png', image)
                included.append('screenshot.png')
        summary['included'] = included
        archive.writestr('summary.json', redact(json.dumps(summary, indent=2, ensure_ascii=False, default=str)) + '\n')
    return path
