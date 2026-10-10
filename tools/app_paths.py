"""Separate immutable bundled resources from persistent, per-user player data."""
from pathlib import Path
import os
import sys

# Dedicated helper exit codes distinguish a refused output path (before any
# copying) from a failed receipt write after the installation attempt.
INSTALL_RESULT_INVALID = 20
INSTALL_RESULT_WRITE_FAILED = 21


def resource_root() -> Path:
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))


def user_root() -> Path:
    override = os.environ.get('EPOCHPACT_USER_DIR')
    if override:
        return Path(override).expanduser().resolve()
    return Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'EpochPact'


def runtime_root() -> Path:
    return user_root() if getattr(sys, 'frozen', False) else resource_root()


def installer_result_root() -> Path:
    """OS-owned folder lookup; elevated output must not follow environment overrides."""
    import ctypes
    import ctypes.wintypes as wt
    import uuid
    class GUID(ctypes.Structure):
        _fields_ = [('data1', wt.DWORD), ('data2', wt.WORD), ('data3', wt.WORD),
                    ('data4', ctypes.c_ubyte * 8)]
    folder = GUID.from_buffer_copy(uuid.UUID('f1b32785-6fba-4fcf-9d55-7b8e7f157091').bytes_le)
    shell = ctypes.WinDLL('shell32', use_last_error=True)
    shell.SHGetKnownFolderPath.argtypes = [ctypes.POINTER(GUID), wt.DWORD, wt.HANDLE,
                                         ctypes.POINTER(ctypes.c_void_p)]
    shell.SHGetKnownFolderPath.restype = ctypes.c_long
    memory = ctypes.c_void_p()
    result = shell.SHGetKnownFolderPath(ctypes.byref(folder), 0, None, ctypes.byref(memory))
    if result != 0:
        raise OSError(f'Cannot locate the installer result folder (HRESULT 0x{result & 0xffffffff:08X}).')
    ole = ctypes.WinDLL('ole32')
    ole.CoTaskMemFree.argtypes = [ctypes.c_void_p]; ole.CoTaskMemFree.restype = None
    try:
        return Path(ctypes.wstring_at(memory)) / 'EpochPact'
    finally:
        ole.CoTaskMemFree(memory)


def validate_installer_result(value: str) -> Path:
    """Validate without resolving away a junction; never overwrite an existing file."""
    import re
    path = Path(value)
    root = installer_result_root()
    if not path.is_absolute() or path.parent != root or not re.fullmatch(r'setup-install-[0-9a-f]{32}\.json', path.name):
        raise ValueError('Invalid installer result path.')
    for part in (path, *path.parents):
        if part.is_symlink() or part.is_junction():
            raise ValueError('Linked installer result paths are not allowed.')
    if path.exists():
        raise ValueError('The installer result file already exists.')
    return path
