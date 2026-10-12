"""Is a newer EpochPact release published? One read-only GitHub request.

Only the public "latest release" of the EpochPact repository is read; nothing
about the player or the game is sent. The link offered to the player is
accepted only when it points to that repository's releases.
"""
from __future__ import annotations

import json
import re
import urllib.request

REPOSITORY = 'falorfrozen-cmd/EpochPact'
LATEST = f'https://api.github.com/repos/{REPOSITORY}/releases/latest'
RELEASES = f'https://github.com/{REPOSITORY}/releases'
_STAGES = {'alpha': 0, 'beta': 1, 'dev': 2, 'rc': 3, '': 4}


def parse(version: str):
    """'v0.1.2-dev.19' -> (0, 1, 2, 2, 19); None when it is not a version."""
    match = re.fullmatch(r'v?(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|dev|rc)\.(\d+))?', (version or '').strip())
    if not match:
        return None
    major, minor, patch, stage, number = match.groups()
    return int(major), int(minor), int(patch), _STAGES[stage or ''], int(number or 0)


def compare(current: str, latest_tag: str, url: str | None = None) -> dict:
    mine, theirs = parse(current), parse(latest_tag)
    safe_url = url if isinstance(url, str) and url.startswith(RELEASES + '/') else RELEASES
    if theirs is None:
        return {'ok': False, 'error': 'The latest release has an unexpected version.'}
    return {'ok': True, 'current': current, 'latest': latest_tag.lstrip('v'), 'url': safe_url,
            'updateAvailable': bool(mine is not None and theirs > mine)}


def check(current: str, timeout: float = 6, opener=urllib.request.urlopen) -> dict:
    request = urllib.request.Request(LATEST, headers={'Accept': 'application/vnd.github+json', 'User-Agent': f'EpochPact/{current}'})
    try:
        with opener(request, timeout=timeout) as response:
            data = json.loads(response.read(256 * 1024).decode('utf-8'))
    except (OSError, ValueError) as exc:
        return {'ok': False, 'error': f'Could not check for updates ({type(exc).__name__}).'}
    if not isinstance(data, dict) or not isinstance(data.get('tag_name'), str):
        return {'ok': False, 'error': 'GitHub returned an unexpected answer.'}
    return compare(current, data['tag_name'], data.get('html_url'))
