"""Optional MelonLoader bootstrap; never rename, replace or remove its loader."""
from __future__ import annotations
import ctypes
import ctypes.wintypes as wt
import os
from pathlib import Path

BRIDGE_NAME = 'EpochPact.MelonBridge.dll'


def _version_strings(path: Path):
    # Read PE resources as data via the real Windows library. Do not LoadLibrary
    # the third-party proxy or invoke any of its exports during installation.
    lib = ctypes.WinDLL(str(Path(os.environ['SystemRoot']) / 'System32/version.dll'), use_last_error=True)
    lib.GetFileVersionInfoSizeW.argtypes = [wt.LPCWSTR, ctypes.POINTER(wt.DWORD)]
    lib.GetFileVersionInfoSizeW.restype = wt.DWORD
    lib.GetFileVersionInfoW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, ctypes.c_void_p]
    lib.VerQueryValueW.argtypes = [ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wt.UINT)]
    dummy = wt.DWORD(); size = lib.GetFileVersionInfoSizeW(str(path), ctypes.byref(dummy))
    if not size or size > 1024 * 1024: return {}
    data = ctypes.create_string_buffer(size)
    if not lib.GetFileVersionInfoW(str(path), 0, size, data): return {}
    pointer = ctypes.c_void_p(); length = wt.UINT()
    if not lib.VerQueryValueW(data, r'\VarFileInfo\Translation', ctypes.byref(pointer), ctypes.byref(length)) or length.value < 4: return {}
    translation = ctypes.cast(pointer, ctypes.POINTER(wt.WORD))
    prefix = f'\\StringFileInfo\\{translation[0]:04x}{translation[1]:04x}\\'
    result = {}
    for key in ('ProductName', 'FileVersion'):
        if lib.VerQueryValueW(data, prefix + key, ctypes.byref(pointer), ctypes.byref(length)) and length.value:
            result[key] = ctypes.wstring_at(pointer, length.value - 1)
    return result


def detected(game: Path) -> bool:
    proxy = game / 'version.dll'
    # Only the .NET 6 bootstrap is supported by this bridge. Missing or linked
    # loader directories are refused, never interpreted as a clean installation.
    paths = [proxy, game / 'MelonLoader', game / 'MelonLoader/net6', game / 'MelonLoader/net6/MelonLoader.dll']
    if any(not p.exists() or p.is_symlink() or (hasattr(p, 'is_junction') and p.is_junction()) for p in paths): return False
    version = _version_strings(proxy)
    return version.get('ProductName') == 'MelonLoader.Bootstrap' and version.get('FileVersion', '').startswith('0.7.')


def owned_bridge(path: Path) -> bool:
    if not path.is_file(): return False
    data = path.read_bytes()
    return b'EpochPact.MelonBridge' in data and b'EpochPactBridge' in data
