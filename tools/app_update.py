"""Install a newer EpochPact release from this repository's GitHub releases.

Only public releases of the EpochPact repository are read; nothing about the
player or the game is sent. The player ZIP is downloaded from that release's
assets (GitHub's own download hosts, nothing else is followed), its SHA-256
must equal the digest GitHub records for the asset, and every extracted file
must match the package's own SHA256SUMS.txt.

The folder swap runs from the verified new EpochPact.exe (--apply-update)
after this app has closed: the old app folder is renamed to a backup, the new
one is copied in and checked, and the new app is started. Any failure puts the
old folder back and reopens the old app with the reason. Settings live in
LocalAppData and are not touched.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import http.client
import json
import logging
import os
from pathlib import Path, PurePosixPath
import re
import secrets
import shutil
import stat
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import zipfile

from . import update_check
from .app_paths import user_root

REPOSITORY = update_check.REPOSITORY
API = f'https://api.github.com/repos/{REPOSITORY}'
RELEASES = update_check.RELEASES
DOWNLOAD = f'https://github.com/{REPOSITORY}/releases/download/'
# github.com answers the download address and redirects to its own asset hosts.
HOSTS = ('github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com')
MAX_ZIP = 200 * 1024 * 1024
MAX_UNPACKED = 1024 * 1024 * 1024
MAX_ENTRIES = 5000
NOTES_LIMIT = 20000
APPLY_REFUSED = 20
log = logging.getLogger(__name__)


class UpdateError(RuntimeError):
    """A reason the player can read."""


def updates_root() -> Path:
    return user_root() / 'updates'


def app_folder() -> Path | None:
    """The folder of the running player EXE; None in a source checkout."""
    return Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def _allowed(url: str) -> bool:
    parts = urllib.parse.urlsplit(url)
    return parts.scheme == 'https' and (parts.hostname or '') in HOSTS


def _inside(path: Path, folder: Path) -> bool:
    try:
        path.resolve().relative_to(folder.resolve())
        return True
    except ValueError:
        return False


def _writable(function, path, _info):
    """rmtree helper: clear the read-only flag once, then retry."""
    os.chmod(path, stat.S_IWRITE)
    function(path)


def remove_tree(path: Path, attempts: int = 1, pause: float = 0.5) -> bool:
    for attempt in range(attempts):
        try:
            if path.exists():
                shutil.rmtree(path, onerror=_writable)
            return True
        except OSError:
            if attempt + 1 < attempts:
                time.sleep(pause)
    return not path.exists()


# ---- release information --------------------------------------------------

def release_info(data, current: str) -> dict:
    """What the player may install from one GitHub release."""
    if not isinstance(data, dict) or data.get('draft') or data.get('prerelease'):
        raise UpdateError('GitHub returned an unexpected release.')
    tag = data.get('tag_name')
    if not isinstance(tag, str) or update_check.parse(tag) is None:
        raise UpdateError('The latest release has an unexpected version.')
    version = tag.removeprefix('v')
    page = data.get('html_url')
    mine = update_check.parse(current)
    info = {'version': version, 'tag': tag,
            'url': page if isinstance(page, str) and page.startswith(RELEASES + '/') else RELEASES,
            'notes': data['body'][:NOTES_LIMIT] if isinstance(data.get('body'), str) else '',
            'published': str(data.get('published_at') or ''),
            'newer': mine is not None and update_check.parse(tag) > mine, 'installable': False, 'reason': ''}
    name = f'EpochPact-{version}.zip'
    asset = next((a for a in data.get('assets') or [] if isinstance(a, dict) and a.get('name') == name), None)
    digest = str((asset or {}).get('digest') or '')
    size = (asset or {}).get('size')
    if asset is None:
        info['reason'] = 'This release has no player ZIP. Use the release page.'
    elif asset.get('browser_download_url') != DOWNLOAD + tag + '/' + name or asset.get('state', 'uploaded') != 'uploaded':
        info['reason'] = 'The release download is not where EpochPact expects it. Use the release page.'
    elif not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
        info['reason'] = 'GitHub has no SHA-256 for this download, so it cannot be checked. Use the release page.'
    elif not isinstance(size, int) or isinstance(size, bool) or not 0 < size <= MAX_ZIP:
        info['reason'] = 'The release download has an unexpected size. Use the release page.'
    else:
        info.update(installable=True, size=size,
                    asset={'name': name, 'url': asset['browser_download_url'], 'size': size, 'sha256': digest[7:]})
    return info


def _get_json(url: str, current: str, opener, timeout: float):
    request = urllib.request.Request(url, headers={'Accept': 'application/vnd.github+json', 'User-Agent': f'EpochPact/{current}'})
    with opener(request, timeout=timeout) as response:
        return json.loads(response.read(2 * 1024 * 1024).decode('utf-8'))


def check(current: str, timeout: float = 8, opener=urllib.request.urlopen) -> dict:
    """The latest release and whether this app can install it."""
    try:
        release = release_info(_get_json(API + '/releases/latest', current, opener, timeout), current)
    except UpdateError as exc:
        return {'ok': False, 'error': str(exc)}
    except (OSError, ValueError, http.client.HTTPException) as exc:
        return {'ok': False, 'error': f'Could not check for updates ({type(exc).__name__}).'}
    return {'ok': True, 'current': current, 'latest': release['version'], 'url': release['url'],
            'updateAvailable': release['newer'], 'installable': release['installable'], 'reason': release['reason'],
            'size': release.get('size', 0), 'release': release}


def history(current: str, timeout: float = 8, opener=urllib.request.urlopen) -> dict:
    """Patch notes of every published release newer than this app (at most ten)."""
    try:
        data = _get_json(API + '/releases?per_page=30', current, opener, timeout)
    except (OSError, ValueError, http.client.HTTPException) as exc:
        return {'ok': False, 'error': f'Could not read the patch notes ({type(exc).__name__}).'}
    releases = []
    for entry in data if isinstance(data, list) else []:
        try:
            info = release_info(entry, current)
        except UpdateError:
            continue
        if info['newer']:
            releases.append({k: info[k] for k in ('version', 'url', 'notes', 'published')})
    releases.sort(key=lambda r: update_check.parse(r['version']), reverse=True)
    return {'ok': True, 'current': current, 'releases': releases[:10]}


# ---- download and verification ----------------------------------------------

class _GitHubRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not _allowed(newurl):
            raise UpdateError('The download was redirected away from GitHub. Nothing was changed.')
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def download(asset: dict, destination: Path, *, current: str = '', progress=None, cancel=None,
             opener=None, attempts: int = 4, pause=time.sleep) -> Path:
    """The release ZIP, resumed after a dropped connection; refused unless its SHA-256 matches."""
    url, size = asset['url'], asset['size']
    if not url.startswith(DOWNLOAD) or not _allowed(url):
        raise UpdateError('The release download is not where EpochPact expects it.')
    opener = opener or urllib.request.build_opener(_GitHubRedirects).open
    destination.parent.mkdir(parents=True, exist_ok=True)
    part = destination.with_name(destination.name + '.part')
    failure = None
    for attempt in range(attempts):
        have = part.stat().st_size if part.exists() else 0
        if have > size:
            part.unlink()
            have = 0
        if have == size:
            break
        headers = {'User-Agent': f'EpochPact/{current}', 'Accept': 'application/octet-stream'}
        if have:
            headers['Range'] = f'bytes={have}-'
        try:
            with opener(urllib.request.Request(url, headers=headers), timeout=30) as response:
                if not _allowed(response.geturl()):
                    raise UpdateError('The download was redirected away from GitHub. Nothing was changed.')
                resumed = have and getattr(response, 'status', 200) == 206
                if not resumed:
                    have = 0
                with part.open('ab' if resumed else 'wb') as stream:
                    while chunk := response.read(256 * 1024):
                        if cancel is not None and cancel.is_set():
                            raise UpdateError('The download was cancelled.')
                        have += len(chunk)
                        if have > size:
                            raise UpdateError('The download is larger than the release says. Nothing was changed.')
                        stream.write(chunk)
                        if progress:
                            progress(have, size)
            if have == size:
                break
            failure = 'the connection closed early'
        except urllib.error.HTTPError as exc:
            if exc.code == 416:
                part.unlink(missing_ok=True)
            failure = f'HTTP {exc.code}'
        except (OSError, http.client.HTTPException) as exc:
            failure = type(exc).__name__
        except UpdateError:
            part.unlink(missing_ok=True)
            raise
        if cancel is not None and cancel.is_set():
            part.unlink(missing_ok=True)
            raise UpdateError('The download was cancelled.')
        if attempt + 1 < attempts:
            pause(min(30, 2 * (attempt + 1) ** 2))
    if not part.exists() or part.stat().st_size != size:
        raise UpdateError(f'The download did not finish ({failure or "no data"}). Check the connection and try again; nothing was changed.')
    if sha256(part) != asset['sha256']:
        part.unlink(missing_ok=True)
        raise UpdateError("The download does not match the SHA-256 GitHub records for this release. It was deleted; nothing was changed.")
    os.replace(part, destination)
    return destination


def _safe_parts(name: str) -> tuple:
    if not name or '\\' in name or ':' in name or name.startswith('/'):
        raise UpdateError('The package contains an unsafe file name.')
    parts = PurePosixPath(name).parts
    if any(p in ('', '.', '..') for p in parts):
        raise UpdateError('The package contains an unsafe file name.')
    return parts


def read_sums(text: str) -> dict:
    sums = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'([0-9a-f]{64})  (.+)', line)
        if not match:
            raise UpdateError('The package checksum list is damaged.')
        _safe_parts(match.group(2))
        sums[match.group(2)] = match.group(1)
    return sums


def verify_tree(root: Path, sums: dict, prefix: str = '') -> None:
    """Every listed file below prefix exists under root with its SHA-256."""
    for name, digest in sums.items():
        if not name.startswith(prefix):
            continue
        path = root.joinpath(*PurePosixPath(name.removeprefix(prefix)).parts)
        if not path.is_file() or sha256(path) != digest:
            raise UpdateError(f'{name} does not match the package checksum list.')


def stage(archive: Path, version: str, target: Path) -> Path:
    """Extract the release ZIP into target and check every file and the version."""
    remove_tree(target, attempts=5)
    try:
        with zipfile.ZipFile(archive) as package:
            entries = package.infolist()
            if len(entries) > MAX_ENTRIES or sum(e.file_size for e in entries) > MAX_UNPACKED:
                raise UpdateError('The package is larger than an EpochPact release can be.')
            files = {}
            for entry in entries:
                if entry.is_dir():
                    continue
                if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                    raise UpdateError('The package contains a link.')
                _safe_parts(entry.filename)
                files[entry.filename] = entry
            if 'SHA256SUMS.txt' not in files:
                raise UpdateError('The package has no checksum list.')
            sums = read_sums(package.read('SHA256SUMS.txt').decode('utf-8'))
            if set(files) - {'SHA256SUMS.txt'} != set(sums):
                raise UpdateError('The package files do not match its checksum list.')
            for required in ('EpochPact/EpochPact.exe', 'EpochPact/build-info.json', 'runtime-manifest.json'):
                if required not in sums:
                    raise UpdateError('The package is not a complete EpochPact player release.')
            for name, entry in files.items():
                path = target.joinpath(*PurePosixPath(name).parts)
                path.parent.mkdir(parents=True, exist_ok=True)
                digest = hashlib.sha256()
                with package.open(entry) as source, path.open('xb') as output:
                    while chunk := source.read(1024 * 1024):
                        digest.update(chunk)
                        output.write(chunk)
                if name != 'SHA256SUMS.txt' and digest.hexdigest() != sums[name]:
                    raise UpdateError(f'{name} does not match the package checksum list.')
        manifest = json.loads((target / 'runtime-manifest.json').read_text(encoding='utf-8'))
        build = json.loads((target / 'EpochPact/build-info.json').read_text(encoding='utf-8'))
        if (manifest.get('kind') != 'Player' or manifest.get('github') != f'https://github.com/{REPOSITORY}'
                or manifest.get('version') != version or build.get('version') != version):
            raise UpdateError('The package is not the EpochPact player release it should be.')
    except UpdateError:
        remove_tree(target, attempts=3)
        raise
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        remove_tree(target, attempts=3)
        raise UpdateError(f'The package could not be checked ({type(exc).__name__}). Nothing was changed.') from exc
    return target


def check_target(folder: Path | None) -> Path:
    """The app folder can be replaced: a complete player folder in a folder we may change."""
    if folder is None:
        raise UpdateError('Install update works in the EpochPact app from the release ZIP. Download the new version from the release page.')
    if not (folder / 'EpochPact.exe').is_file() or not (folder / 'build-info.json').is_file():
        raise UpdateError('This EpochPact folder is incomplete. Download the new version from the release page.')
    if _inside(folder, updates_root()):
        raise UpdateError('This copy of EpochPact is an update in progress, not the installed app.')
    probe = folder.parent / f'.epochpact-write-test-{secrets.token_hex(4)}'
    try:
        probe.mkdir()
        probe.rmdir()
    except OSError:
        raise UpdateError(f'Windows does not allow changes in {folder.parent}. Move the EpochPact folder somewhere you own '
                          '(for example Documents) or download the new version from the release page.') from None
    return folder


class Updater:
    """Download, check and stage one release in the background; the UI polls status()."""

    def __init__(self, current: str, *, opener=None, folder=app_folder):
        self.current = current
        self._opener = opener
        self._folder = folder
        self._lock = threading.Lock()
        self._cancel = threading.Event()
        self._state = {'state': 'idle', 'received': 0, 'total': 0, 'version': '', 'error': ''}
        self.staged = None

    def status(self) -> dict:
        with self._lock:
            return dict(self._state)

    def _set(self, **values):
        with self._lock:
            self._state.update(values)

    def start(self, release: dict | None) -> dict:
        if not release or not release.get('newer'):
            raise UpdateError('No newer EpochPact release is available.')
        if not release.get('installable'):
            raise UpdateError(release.get('reason') or 'This release cannot be installed from the app.')
        check_target(self._folder())
        with self._lock:
            if self._state['state'] in ('downloading', 'verifying', 'installing'):
                return dict(self._state)
            if self._state['state'] == 'ready' and self._state['version'] == release['version']:
                return dict(self._state)
            self._cancel.clear()
            self.staged = None
            self._state.update(state='downloading', received=0, total=release['asset']['size'], version=release['version'], error='')
        threading.Thread(target=self._run, args=(release,), name='epochpact-app-update', daemon=True).start()
        return self.status()

    def _run(self, release):
        try:
            base = updates_root() / release['version']
            archive = download(release['asset'], base / release['asset']['name'], current=self.current, cancel=self._cancel,
                               progress=lambda have, total: self._set(received=have, total=total), opener=self._opener)
            self._set(state='verifying')
            staged = stage(archive, release['version'], base / 'package')
            archive.unlink(missing_ok=True)
            self.staged = staged
            self._set(state='ready')
        except UpdateError as exc:
            self._set(state='idle' if self._cancel.is_set() else 'error', error='' if self._cancel.is_set() else str(exc))
        except Exception as exc:
            log.exception('Update preparation failed')
            self._set(state='error', error=f'The update could not be prepared ({type(exc).__name__}). Nothing was changed.')

    def cancel(self) -> dict:
        self._cancel.set()
        return self.status()

    def install(self, pid: int, relaunch=()) -> dict:
        """Start the verified new EpochPact.exe, which waits for this app to close and swaps the folder."""
        with self._lock:
            if self._state['state'] != 'ready' or self.staged is None:
                raise UpdateError('Download the update first.')
        folder = check_target(self._folder())
        launch_apply(self.staged, folder, pid, relaunch)
        self._set(state='installing')
        return self.status()


# ---- processes ---------------------------------------------------------------

DETACHED = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
BREAKAWAY = 0x01000000              # CREATE_BREAKAWAY_FROM_JOB


def _start(arguments, cwd):
    try:
        return subprocess.Popen(arguments, cwd=str(cwd), creationflags=DETACHED | BREAKAWAY, close_fds=True)
    except OSError:
        # A job that forbids breakaway still lets the child outlive this app.
        return subprocess.Popen(arguments, cwd=str(cwd), creationflags=DETACHED, close_fds=True)


def launch_apply(staged: Path, folder: Path, pid: int, relaunch=()):
    exe = staged / 'EpochPact' / 'EpochPact.exe'
    arguments = [str(exe), '--apply-update', '--install-dir', str(folder), '--wait-pid', str(pid)]
    arguments += [f'--relaunch-arg={a}' for a in relaunch]
    return _start(arguments, exe.parent)


def wait_for_exit(pid: int, seconds: float) -> bool:
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.OpenProcess.argtypes = [ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.OpenProcess(0x00100000, False, pid)  # SYNCHRONIZE
    if not handle:
        return True  # already gone
    try:
        return kernel.WaitForSingleObject(handle, int(seconds * 1000)) == 0
    finally:
        kernel.CloseHandle(handle)


# ---- the swap, run from the new EpochPact.exe ---------------------------------

def _record(result: dict):
    updates_root().mkdir(parents=True, exist_ok=True)
    marker = updates_root() / 'result.json'
    temp = marker.with_name(f'result.{secrets.token_hex(4)}.tmp')
    temp.write_text(json.dumps(result) + '\n', encoding='utf-8')
    os.replace(temp, marker)


def _version(folder: Path) -> str:
    try:
        return str(json.loads((folder / 'build-info.json').read_text(encoding='utf-8'))['version'])
    except (OSError, ValueError, KeyError, TypeError):
        return 'unknown'


def _rename(source: Path, target: Path, attempts: int = 40, pause: float = 0.5):
    """WebView2 and antivirus can hold a file for a moment after the app closed."""
    for attempt in range(attempts):
        try:
            os.rename(source, target)
            return
        except PermissionError:
            if attempt + 1 == attempts:
                raise
            time.sleep(pause)


def _package_root(folder: Path) -> dict | None:
    """The extracted ZIP's top folder, recognised by its own manifest and checksum list."""
    root = folder.parent
    try:
        manifest = json.loads((root / 'runtime-manifest.json').read_text(encoding='utf-8'))
        if manifest.get('kind') == 'Player' and manifest.get('github') == f'https://github.com/{REPOSITORY}':
            return read_sums((root / 'SHA256SUMS.txt').read_text(encoding='utf-8'))
    except (OSError, ValueError, UpdateError):
        pass
    return None


def _update_root_files(folder: Path, staged: Path, new_sums: dict):
    """README, checksums and Manual Installation beside the app folder. A file is
    replaced or removed only while it is still exactly the one EpochPact shipped."""
    old_sums = _package_root(folder)
    if old_sums is None:
        return 0
    root, changed = folder.parent, 0
    for name, digest in new_sums.items():
        if name.startswith('EpochPact/'):
            continue
        path = root.joinpath(*PurePosixPath(name).parts)
        if path.exists() and (name not in old_sums or sha256(path) != old_sums[name]):
            continue  # changed or created by the player: leave it
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(staged.joinpath(*PurePosixPath(name).parts), path)
        changed += 1
    for name, digest in old_sums.items():
        path = root.joinpath(*PurePosixPath(name).parts)
        if name.startswith('EpochPact/') or name in new_sums or not path.is_file() or sha256(path) != digest:
            continue
        path.unlink()
        with_parent = path.parent
        while with_parent != root and not any(with_parent.iterdir()):
            with_parent.rmdir()
            with_parent = with_parent.parent
        changed += 1
    shutil.copy2(staged / 'SHA256SUMS.txt', root / 'SHA256SUMS.txt')
    return changed + 1


def _carry_over(backup: Path, folder: Path):
    """Files the player put directly in the app folder move to the new one;
    files the old version shipped stay behind."""
    try:
        shipped = {f['path'] for f in json.loads((backup / 'build-info.json').read_text(encoding='utf-8'))['files']}
    except (OSError, ValueError, KeyError, TypeError):
        return
    for path in backup.iterdir():
        if path.is_file() and path.name not in shipped and path.name != 'build-info.json' and not (folder / path.name).exists():
            shutil.copy2(path, folder / path.name)


def apply_update(staged_app: Path, install_dir: Path, wait_pid: int, relaunch=(), *,
                 wait=wait_for_exit, start=_start) -> int:
    """Replace install_dir with the verified staged app; restore it on any failure."""
    staged_app, install_dir = staged_app.resolve(), install_dir.resolve()
    package, root = staged_app.parent, updates_root().resolve()
    new_version = package.parent.name
    installed = (install_dir / 'EpochPact.exe').is_file() and (install_dir / 'build-info.json').is_file() and not _inside(install_dir, root)
    old_version = _version(install_dir) if installed else 'unknown'
    result = {'ok': False, 'from': old_version, 'to': new_version, 'at': time.strftime('%Y-%m-%dT%H:%M:%S')}
    if not installed:
        return APPLY_REFUSED  # not an EpochPact app folder: nothing is changed or started
    if staged_app.name != 'EpochPact' or package.name != 'package' or package.parent.parent != root:
        # Only an update staged by EpochPact itself is applied; the app is reopened unchanged.
        wait(wait_pid, 60)
        _record({**result, 'error': 'The update did not start from its download folder, so nothing was changed.'})
        start([str(install_dir / 'EpochPact.exe'), *relaunch], install_dir)
        return APPLY_REFUSED
    backup = install_dir.with_name(f'{install_dir.name}.previous-{old_version}-{secrets.token_hex(3)}')
    moved = closed = False
    try:
        if not wait(wait_pid, 60):
            raise UpdateError('EpochPact did not close in time, so nothing was changed. Close every EpochPact window and try again.')
        closed = True
        sums = read_sums((package / 'SHA256SUMS.txt').read_text(encoding='utf-8'))
        verify_tree(package, sums)
        if _version(staged_app) != new_version:
            raise UpdateError('The downloaded update is not the expected version.')
        try:
            _rename(install_dir, backup)
        except PermissionError:
            raise UpdateError('Windows kept the EpochPact folder in use, so nothing was changed. Close other EpochPact windows '
                              'and programs using that folder, then try again.') from None
        moved = True
        shutil.copytree(staged_app, install_dir)
        verify_tree(install_dir, sums, 'EpochPact/')
        _carry_over(backup, install_dir)
        result.update(ok=True, backup=str(backup))
    except Exception as exc:
        result['error'] = str(exc) if isinstance(exc, UpdateError) else f'The update could not be installed ({type(exc).__name__}). Your previous EpochPact was kept.'
        if moved:
            try:
                if install_dir.exists():
                    shutil.rmtree(install_dir, onerror=_writable)
                os.rename(backup, install_dir)
            except OSError:
                result['error'] += f' Your previous EpochPact is in {backup}.'
                result['backup'] = str(backup)
                _record(result)
                ctypes.windll.user32.MessageBoxW(None, result['error'], 'EpochPact update', 0x10)
                return 2
        _record(result)
        if closed:  # otherwise the old app is still open and shows nothing new
            start([str(install_dir / 'EpochPact.exe'), *relaunch], install_dir)
        return 1
    # The new app is in place; nothing below undoes it.
    try:
        result['rootFiles'] = _update_root_files(install_dir, package, sums)
    except OSError as exc:  # README, checksums and Manual Installation stay as they were
        result['rootFilesError'] = type(exc).__name__
    _record(result)
    try:
        start([str(install_dir / 'EpochPact.exe'), *relaunch], install_dir)
    except OSError:
        ctypes.windll.user32.MessageBoxW(None, f'EpochPact was updated to {new_version}. Open it again from {install_dir}.', 'EpochPact update', 0x40)
    return 0


def apply_main(argv) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--apply-update', action='store_true')
    parser.add_argument('--install-dir', required=True, type=Path)
    parser.add_argument('--wait-pid', required=True, type=int)
    parser.add_argument('--relaunch-arg', action='append', default=[])
    args = parser.parse_args(argv)
    updates_root().mkdir(parents=True, exist_ok=True)
    handler = logging.FileHandler(updates_root() / 'apply.log', mode='w', encoding='utf-8')
    logging.basicConfig(handlers=[handler], level=logging.INFO, format='%(asctime)s %(message)s')
    try:
        code = apply_update(Path(sys.executable).parent, args.install_dir, args.wait_pid, args.relaunch_arg)
        log.info('apply-update finished with code %s', code)
        return code
    except Exception:
        log.exception('apply-update failed')
        return 1
    finally:
        handler.close()


# ---- after the new app started -------------------------------------------------

def take_result() -> dict | None:
    """The outcome the last update left for this app; reported once."""
    marker = updates_root() / 'result.json'
    try:
        result = json.loads(marker.read_text(encoding='utf-8'))
        marker.unlink()
    except FileNotFoundError:
        return None
    except (OSError, ValueError):
        marker.unlink(missing_ok=True)
        return None
    return result if isinstance(result, dict) else None


def clean_up(current: str, result: dict | None, folder: Path | None = None):
    """Leftovers of finished updates: the previous app folder and old downloads."""
    folder = folder or app_folder()
    if result and not result.get('ok') and update_check.parse(str(result.get('to'))) is not None:
        remove_tree(updates_root() / str(result['to']), attempts=20)  # a failed update is downloaded again next time
    if result and result.get('ok') and result.get('to') == current and folder is not None and result.get('backup'):
        backup = Path(result['backup'])
        # Only the backup this update made: beside this app, named after it.
        if backup.parent == folder.parent and backup.name.startswith(folder.name + '.previous-'):
            remove_tree(backup, attempts=20)
    mine = update_check.parse(current)
    if updates_root().is_dir() and mine is not None:
        for entry in updates_root().iterdir():
            version = update_check.parse(entry.name)
            if entry.is_dir() and version is not None and version <= mine:
                remove_tree(entry, attempts=20)
