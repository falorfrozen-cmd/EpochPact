"""Separate immutable bundled resources from persistent, per-user player data."""
from pathlib import Path
import os
import sys


def resource_root() -> Path:
    return Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parents[1]))


def user_root() -> Path:
    override = os.environ.get('EPOCHPACT_USER_DIR')
    if override:
        return Path(override).expanduser().resolve()
    return Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'EpochPact'


def runtime_root() -> Path:
    return user_root() if getattr(sys, 'frozen', False) else resource_root()
