"""Profile files: one saved profile as a small JSON file to share.

    {"epochpact": "profile", "version": 1, "name": "Balanced", "settings": {"xp": 3, "gold": 2}}

The in-game window (native/core/ingame_settings.cpp) reads and writes the same format. A file is
data only: setting IDs with numbers, on/off or null (the game's value). The interface keeps the
settings its catalog accepts; nothing here applies a setting or touches another profile.
"""
from __future__ import annotations

import json
import math
import re
from pathlib import Path

from .app_paths import user_root
from .filter_manager import safe_name

FILE_LIMIT = 256 * 1024
SETTING_LIMIT = 500
NAME_LIMIT = 128
SETTING_ID = re.compile(r'[a-z0-9_]{1,64}')


def _settings(value) -> dict:
    if not isinstance(value, dict) or len(value) > SETTING_LIMIT:
        raise ValueError('This is not an EpochPact profile file.')
    settings = {}
    for key, v in value.items():
        if not isinstance(key, str) or not SETTING_ID.fullmatch(key):
            continue
        if v is None or isinstance(v, bool) or (isinstance(v, (int, float)) and math.isfinite(v)):
            settings[key] = v
    return settings


def _name(value) -> str:
    name = re.sub(r'\s+', ' ', value if isinstance(value, str) else '').strip()[:NAME_LIMIT].strip()
    return name or 'Imported profile'


def export_profile(name: str, settings: dict, output: Path | None = None) -> dict:
    name, settings = _name(name), _settings(settings)
    if not settings:
        raise ValueError('This profile has no settings to export.')
    folder = output or (user_root() / 'profile-exports')
    folder.mkdir(parents=True, exist_ok=True)
    stem, n = safe_name(name), 2
    if stem == 'Imported filter':
        stem = 'Profile'
    taken = {p.name.lower() for p in folder.iterdir()}
    base = stem
    while stem.lower() + '.json' in taken:
        stem, n = f'{base} ({n})', n + 1
    target = folder / (stem + '.json')
    data = json.dumps({'epochpact': 'profile', 'version': 1, 'name': name, 'settings': settings}, ensure_ascii=False, indent=1)
    with target.open('x', encoding='utf-8') as stream:
        stream.write(data)
    return {'ok': True, 'file': target.name, 'path': str(target), 'folder': str(folder)}


def import_profile(path) -> dict:
    if not isinstance(path, str) or not path.strip():
        raise ValueError('Choose a profile file.')
    source = Path(path.strip().strip('"'))
    if not source.is_absolute() or source.suffix.lower() != '.json' or not source.is_file():
        raise ValueError('Choose an EpochPact profile .json file using its full path.')
    if source.stat().st_size > FILE_LIMIT:
        raise ValueError('This profile file is too large.')
    try:
        data = json.loads(source.read_text(encoding='utf-8-sig'))
    except (OSError, UnicodeDecodeError, ValueError):
        raise ValueError('This is not an EpochPact profile file.') from None
    if not isinstance(data, dict) or data.get('epochpact') != 'profile' or not isinstance(data.get('version'), int) or data['version'] < 1:
        raise ValueError('This is not an EpochPact profile file.')
    settings = _settings(data.get('settings'))
    if not settings:
        raise ValueError('This profile has no settings EpochPact can use.')
    return {'ok': True, 'name': _name(data.get('name')), 'settings': settings}
