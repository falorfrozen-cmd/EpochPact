"""Interface preferences kept in a file, independent of the WebView origin.

The desktop interface keeps its own choices (profiles, last used settings,
theme, window options, overlay window places) under ``epochpact.*`` keys in the
page's localStorage. localStorage belongs to one address (127.0.0.1:<port>):
when the usual port is busy the app uses another one and everything looks
lost. The pages therefore mirror those keys into this file and read it back at
start, so a port change, an app update or a new WebView profile keeps them.

Values are the strings the pages store. Nothing here is sent to the game.
"""
from __future__ import annotations

import json
import os
import re
import threading
from pathlib import Path

from tools.app_paths import user_root

KEY = re.compile(r'epochpact\.[A-Za-z0-9._-]{1,120}')
MAXIMUM_KEYS = 300
MAXIMUM_VALUE = 64 * 1024
MAXIMUM_TOTAL = 1024 * 1024


def default_path() -> Path:
    return user_root() / 'preferences.json'


class PreferencesStore:
    def __init__(self, path: Path | None = None):
        self.path = Path(path) if path is not None else default_path()
        self._lock = threading.Lock()

    def _read(self) -> dict[str, str]:
        try:
            data = json.loads(self.path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            return {}
        values = data.get('values') if isinstance(data, dict) else None
        if not isinstance(values, dict):
            return {}
        return {k: v for k, v in values.items() if isinstance(k, str) and KEY.fullmatch(k) and isinstance(v, str) and len(v) <= MAXIMUM_VALUE}

    def values(self) -> dict[str, str]:
        with self._lock:
            return self._read()

    def get(self, key: str, default=None):
        return self.values().get(key, default)

    def update(self, changes: dict) -> dict[str, str]:
        """Apply {key: string | None}; None removes the key. All or nothing."""
        if not isinstance(changes, dict) or not changes:
            raise ValueError('No preferences to save.')
        for key, value in changes.items():
            if not isinstance(key, str) or not KEY.fullmatch(key):
                raise ValueError('Invalid preference name.')
            if value is not None and (not isinstance(value, str) or len(value) > MAXIMUM_VALUE):
                raise ValueError('Preference value is too large.')
        with self._lock:
            values = self._read()
            for key, value in changes.items():
                if value is None:
                    values.pop(key, None)
                else:
                    values[key] = value
            if len(values) > MAXIMUM_KEYS or sum(len(k) + len(v) for k, v in values.items()) > MAXIMUM_TOTAL:
                raise ValueError('Too many preferences are stored.')
            self.path.parent.mkdir(parents=True, exist_ok=True)
            temporary = self.path.with_name(self.path.name + '.tmp')
            temporary.write_text(json.dumps({'version': 1, 'values': values}, ensure_ascii=False, indent=1), encoding='utf-8')
            os.replace(temporary, self.path)
            return values

    def get_json(self, key: str, default=None):
        try:
            value = json.loads(self.get(key, 'null'))
        except ValueError:
            return default
        return default if value is None else value

    def set_json(self, key: str, value) -> None:
        self.update({key: json.dumps(value, separators=(',', ':'))})
