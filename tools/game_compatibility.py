"""Refuse unverified player game builds; metadata-only selection remains possible."""
import hashlib
import json
from pathlib import Path
from functools import lru_cache
try:
    from .app_paths import resource_root
except ImportError:
    from app_paths import resource_root


@lru_cache(maxsize=16)
def _digest(path, size, modified_ns, changed_ns):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require_supported_build(game, *, manifest=None):
    manifest = Path(manifest) if manifest else resource_root() / 'ui/supported-game-builds.json'
    builds = json.loads(manifest.read_text(encoding='utf-8'))['builds']
    path = (Path(game) / 'GameAssembly.dll').resolve(strict=True)
    info = path.stat()
    candidates = [x for x in builds if x['bytes'] == info.st_size]
    if candidates:
        digest = _digest(str(path), info.st_size, info.st_mtime_ns, info.st_ctime_ns)
        for build in candidates:
            if digest == build['sha256']:
                return dict(build)
    versions = ', '.join(dict.fromkeys(x['gameVersion'] for x in builds))
    raise ValueError('Unsupported Last Epoch build. This EpochPact release supports Steam ' +
                     versions + ' only. No game or mod files were changed. Get a compatible EpochPact release.')
