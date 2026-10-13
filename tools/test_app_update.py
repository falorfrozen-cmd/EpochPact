import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
import urllib.error
import zipfile

from tools import app_update as au

REPO_URL = 'https://github.com/falorfrozen-cmd/EpochPact'


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def release(version='0.1.2-dev.23', **asset):
    data = b'zip'
    entry = {'name': f'EpochPact-{version}.zip', 'state': 'uploaded', 'size': 10,
             'digest': 'sha256:' + sha(data), 'browser_download_url': f'{au.DOWNLOAD}v{version}/EpochPact-{version}.zip'}
    entry.update(asset)
    return {'tag_name': f'v{version}', 'html_url': f'{au.RELEASES}/tag/v{version}', 'body': '# Notes', 'draft': False,
            'prerelease': False, 'published_at': '2026-10-14T00:00:00Z', 'assets': [entry]}


class FakeResponse(io.BytesIO):
    def __init__(self, data=b'', status=200, url='https://release-assets.githubusercontent.com/x'):
        super().__init__(data)
        self.status, self._url = status, url

    def geturl(self):
        return self._url

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class Dropping(FakeResponse):
    """Delivers the first `limit` bytes, then the connection drops."""
    def __init__(self, data, limit, **kw):
        super().__init__(data, **kw)
        self.limit = limit

    def read(self, size=-1):
        if self.tell() >= self.limit:
            raise ConnectionResetError('dropped')
        return super().read(min(size, self.limit - self.tell()))


def package_zip(path: Path, version: str, files: dict, *, extra=None, listed=None, sums_override=None, manifest=None):
    """A player ZIP shaped like tools/package_player_folder.py output; `listed` entries are
    also in the checksum list, `extra` entries are not."""
    payload = {f'EpochPact/{k}': v for k, v in files.items()}
    payload.update(listed or {})
    payload.setdefault('EpochPact/EpochPact.exe', b'MZ new')
    payload.setdefault('EpochPact/build-info.json', json.dumps({'version': version, 'files': [{'path': 'EpochPact.exe'}]}).encode())
    payload['README.md'] = b'new readme'
    payload['runtime-manifest.json'] = json.dumps(manifest or {'version': version, 'kind': 'Player', 'github': REPO_URL}).encode()
    sums = sums_override if sums_override is not None else ''.join(f'{sha(d)}  {n}\n' for n, d in sorted(payload.items()))
    payload.update(extra or {})
    payload['SHA256SUMS.txt'] = sums.encode()
    with zipfile.ZipFile(path, 'w') as z:
        for name, data in payload.items():
            z.writestr(name, data)
    return path


class Isolated(unittest.TestCase):
    def setUp(self):
        self.temp = Path(tempfile.mkdtemp())
        self.user = self.temp / 'user'
        patcher = patch.dict(os.environ, {'EPOCHPACT_USER_DIR': str(self.user)})
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(shutil.rmtree, self.temp, True)


class ReleaseTests(Isolated):
    def test_installable_release_needs_repository_asset_digest_and_size(self):
        info = au.release_info(release(), '0.1.2-dev.22')
        self.assertTrue(info['newer'] and info['installable'])
        self.assertEqual(info['asset']['sha256'], sha(b'zip'))
        self.assertEqual(info['url'], f'{au.RELEASES}/tag/v0.1.2-dev.23')
        cases = {'browser_download_url': 'https://evil.example/EpochPact-0.1.2-dev.23.zip', 'digest': '', 'size': 0,
                 'state': 'starter'}
        for key, value in cases.items():
            info = au.release_info(release(**{key: value}), '0.1.2-dev.22')
            self.assertFalse(info['installable'], key)
            self.assertTrue(info['reason'], key)
        self.assertFalse(au.release_info(release(name='Other.zip'), '0.1.2-dev.22')['installable'])
        self.assertFalse(au.release_info(release(size=au.MAX_ZIP + 1), '0.1.2-dev.22')['installable'])

    def test_drafts_prereleases_older_versions_and_foreign_pages(self):
        for flag in ('draft', 'prerelease'):
            with self.assertRaises(au.UpdateError):
                au.release_info({**release(), flag: True}, '0.1.2-dev.22')
        self.assertFalse(au.release_info(release('0.1.2-dev.22'), '0.1.2-dev.22')['newer'])
        self.assertFalse(au.release_info(release('0.1.2-dev.21'), '0.1.2-dev.22')['newer'])
        self.assertEqual(au.release_info({**release(), 'html_url': 'https://evil.example'}, '0.1.2-dev.22')['url'], au.RELEASES)

    def test_check_and_history_read_only_the_repository(self):
        seen = []

        def opener(request, timeout):
            seen.append(request.full_url)
            if request.full_url.endswith('/latest'):
                return FakeResponse(json.dumps(release()).encode())
            return FakeResponse(json.dumps([release('0.1.2-dev.21'), release('0.1.2-dev.24'), release('0.1.2-dev.23'),
                                            {**release('0.1.2-dev.25'), 'prerelease': True}]).encode())
        result = au.check('0.1.2-dev.22', opener=opener)
        self.assertTrue(result['ok'] and result['updateAvailable'] and result['installable'])
        self.assertEqual(result['latest'], '0.1.2-dev.23')
        notes = au.history('0.1.2-dev.22', opener=opener)
        self.assertEqual([r['version'] for r in notes['releases']], ['0.1.2-dev.24', '0.1.2-dev.23'])
        self.assertTrue(all(url.startswith(au.API) for url in seen))
        self.assertFalse(au.check('0.1.2-dev.22', opener=lambda r, timeout: (_ for _ in ()).throw(OSError('offline')))['ok'])


class DownloadTests(Isolated):
    def asset(self, data):
        return {'name': 'EpochPact-0.1.2-dev.23.zip', 'url': f'{au.DOWNLOAD}v0.1.2-dev.23/EpochPact-0.1.2-dev.23.zip',
                'size': len(data), 'sha256': sha(data)}

    def test_dropped_connection_resumes_with_range_and_checks_the_digest(self):
        data = os.urandom(600 * 1024)
        requests = []

        def opener(request, timeout):
            requests.append(request.get_header('Range'))
            if len(requests) == 1:
                return Dropping(data, 300 * 1024)
            start = int(request.get_header('Range').split('=')[1].rstrip('-'))
            return FakeResponse(data[start:], status=206)
        progress = []
        path = au.download(self.asset(data), self.temp / 'u.zip', opener=opener, pause=lambda s: None,
                           progress=lambda have, total: progress.append(have))
        self.assertEqual(path.read_bytes(), data)
        self.assertEqual(requests, [None, f'bytes={300 * 1024}-'])
        self.assertEqual(progress[-1], len(data))
        self.assertFalse((self.temp / 'u.zip.part').exists())

    def test_server_ignoring_range_restarts_cleanly(self):
        data = os.urandom(400 * 1024)
        calls = []

        def opener(request, timeout):
            calls.append(1)
            return Dropping(data, 100 * 1024) if len(calls) == 1 else FakeResponse(data, status=200)
        self.assertEqual(au.download(self.asset(data), self.temp / 'u.zip', opener=opener, pause=lambda s: None).read_bytes(), data)

    def test_digest_mismatch_redirects_and_oversize_are_refused_without_a_file(self):
        data = b'release bytes'
        bad = {**self.asset(data), 'sha256': sha(b'other')}
        with self.assertRaisesRegex(au.UpdateError, 'SHA-256'):
            au.download(bad, self.temp / 'a.zip', opener=lambda r, timeout: FakeResponse(data), pause=lambda s: None)
        with self.assertRaisesRegex(au.UpdateError, 'redirected'):
            au.download(self.asset(data), self.temp / 'b.zip', pause=lambda s: None,
                        opener=lambda r, timeout: FakeResponse(data, url='https://evil.example/x'))
        with self.assertRaisesRegex(au.UpdateError, 'larger'):
            au.download({**self.asset(data), 'size': 4}, self.temp / 'c.zip', pause=lambda s: None,
                        opener=lambda r, timeout: FakeResponse(data))
        with self.assertRaises(au.UpdateError):
            au.download({**self.asset(data), 'url': 'https://evil.example/EpochPact.zip'}, self.temp / 'd.zip')
        self.assertEqual(sorted(p.name for p in self.temp.iterdir()), ['user'] if (self.temp / 'user').exists() else [])
        handler = au._GitHubRedirects()
        with self.assertRaises(au.UpdateError):
            handler.redirect_request(None, None, 302, 'Found', {}, 'http://objects.githubusercontent.com/x')
        with self.assertRaises(au.UpdateError):
            handler.redirect_request(None, None, 302, 'Found', {}, 'https://github.com.evil.example/x')

    def test_cancel_removes_the_partial_download(self):
        data = os.urandom(800 * 1024)
        cancel = threading.Event()

        def progress(have, total):
            cancel.set()
        with self.assertRaisesRegex(au.UpdateError, 'cancelled'):
            au.download(self.asset(data), self.temp / 'u.zip', opener=lambda r, timeout: FakeResponse(data),
                        cancel=cancel, progress=progress, pause=lambda s: None)
        self.assertFalse((self.temp / 'u.zip.part').exists())

    def test_failures_after_every_attempt_report_and_keep_resume_data(self):
        data = os.urandom(300 * 1024)
        with self.assertRaisesRegex(au.UpdateError, 'did not finish'):
            au.download(self.asset(data), self.temp / 'u.zip', opener=lambda r, timeout: Dropping(data, 0),
                        pause=lambda s: None, attempts=2)

        def http_error(request, timeout):
            raise urllib.error.HTTPError(request.full_url, 503, 'busy', {}, None)
        with self.assertRaisesRegex(au.UpdateError, 'HTTP 503'):
            au.download(self.asset(data), self.temp / 'v.zip', opener=http_error, pause=lambda s: None, attempts=2)


class StageTests(Isolated):
    def test_complete_package_is_staged_and_checked(self):
        archive = package_zip(self.temp / 'p.zip', '0.1.2-dev.23', {'_internal/a.pyc': b'a'})
        staged = au.stage(archive, '0.1.2-dev.23', self.temp / 'package')
        self.assertEqual((staged / 'EpochPact/_internal/a.pyc').read_bytes(), b'a')
        au.verify_tree(staged, au.read_sums((staged / 'SHA256SUMS.txt').read_text()))

    def test_unsafe_or_mismatched_packages_leave_nothing(self):
        cases = {
            'slip': dict(extra={'../evil.txt': b'x'}),
            'listed slip': dict(listed={'../evil.txt': b'x'}),
            'listed absolute': dict(listed={'C:/evil.txt': b'x'}),
            'unlisted': dict(extra={'EpochPact/extra.dll': b'x'}),
            'damaged sums': dict(sums_override='not a checksum line\n'),
            'other version': dict(manifest={'version': '0.1.2-dev.99', 'kind': 'Player', 'github': REPO_URL}),
            'other repository': dict(manifest={'version': '0.1.2-dev.23', 'kind': 'Player', 'github': 'https://github.com/x/y'}),
        }
        for label, options in cases.items():
            archive = package_zip(self.temp / f'{label}.zip', '0.1.2-dev.23', {'_internal/a.pyc': b'a'}, **options)
            with self.assertRaises(au.UpdateError, msg=label):
                au.stage(archive, '0.1.2-dev.23', self.temp / 'package')
            self.assertFalse((self.temp / 'package').exists(), label)
            self.assertFalse((self.temp / 'evil.txt').exists(), label)
        archive = package_zip(self.temp / 'tampered.zip', '0.1.2-dev.23', {'_internal/a.pyc': b'a'})
        with zipfile.ZipFile(archive) as z:
            items = {i.filename: z.read(i) for i in z.infolist()}
        items['EpochPact/_internal/a.pyc'] = b'changed'
        with zipfile.ZipFile(archive, 'w') as z:
            for name, data in items.items():
                z.writestr(name, data)
        with self.assertRaisesRegex(au.UpdateError, 'checksum'):
            au.stage(archive, '0.1.2-dev.23', self.temp / 'package')
        link = self.temp / 'link.zip'
        package_zip(link, '0.1.2-dev.23', {})
        with zipfile.ZipFile(link, 'a') as z:
            info = zipfile.ZipInfo('EpochPact/link')
            info.external_attr = 0o120777 << 16
            z.writestr(info, 'target')
        with self.assertRaisesRegex(au.UpdateError, 'link'):
            au.stage(link, '0.1.2-dev.23', self.temp / 'package')


class ApplyTests(Isolated):
    def setUp(self):
        super().setUp()
        self.root = self.temp / 'Games ü' / 'EpochPact-0.1.2-dev.22'
        self.app = self.root / 'EpochPact'
        (self.app / '_internal').mkdir(parents=True)
        old = {'EpochPact/EpochPact.exe': b'MZ old', 'EpochPact/START-HERE.txt': b'old start',
               'EpochPact/_internal/old.pyc': b'old', 'README.md': b'old readme', 'Manual Installation/version.dll': b'old dll',
               'runtime-manifest.json': json.dumps({'version': '0.1.2-dev.22', 'kind': 'Player', 'github': REPO_URL}).encode()}
        for name, data in old.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.app / 'build-info.json').write_text(json.dumps({'version': '0.1.2-dev.22', 'files': [
            {'path': 'EpochPact.exe'}, {'path': 'START-HERE.txt'}, {'path': '_internal/old.pyc'}]}))
        (self.root / 'SHA256SUMS.txt').write_text(''.join(f'{sha(d)}  {n}\n' for n, d in sorted(old.items())))
        (self.app / 'my-notes.txt').write_text('player file')
        (self.root / 'my-list.txt').write_text('player file at the root')
        archive = package_zip(self.temp / 'p.zip', '0.1.2-dev.23', {'_internal/new.pyc': b'new'})
        self.staged = au.stage(archive, '0.1.2-dev.23', au.updates_root() / '0.1.2-dev.23' / 'package')
        self.started = []

    def start(self, arguments, cwd):
        self.started.append((arguments, Path(cwd)))

    def test_swap_keeps_player_files_updates_unchanged_root_files_and_starts_the_new_app(self):
        (self.root / 'README.md').write_text('edited by the player')  # not EpochPact's any more: kept
        code = au.apply_update(self.staged / 'EpochPact', self.app, 1234, ['--no-open'], wait=lambda pid, s: True, start=self.start)
        self.assertEqual(code, 0)
        self.assertEqual((self.app / 'EpochPact.exe').read_bytes(), b'MZ new')
        self.assertEqual((self.app / '_internal/new.pyc').read_bytes(), b'new')
        self.assertFalse((self.app / '_internal/old.pyc').exists())
        self.assertFalse((self.app / 'START-HERE.txt').exists())  # shipped by the old version only
        self.assertEqual((self.app / 'my-notes.txt').read_text(), 'player file')
        self.assertEqual((self.root / 'README.md').read_text(), 'edited by the player')
        self.assertEqual((self.root / 'my-list.txt').read_text(), 'player file at the root')
        self.assertFalse((self.root / 'Manual Installation').exists())  # no longer in the package
        self.assertEqual(json.loads((self.root / 'runtime-manifest.json').read_text())['version'], '0.1.2-dev.23')
        self.assertEqual(self.started, [([str(self.app.resolve() / 'EpochPact.exe'), '--no-open'], self.app.resolve())])
        result = au.take_result()
        self.assertTrue(result['ok'] and result['from'] == '0.1.2-dev.22' and result['to'] == '0.1.2-dev.23')
        backup = Path(result['backup'])
        self.assertEqual((backup / 'EpochPact.exe').read_bytes(), b'MZ old')
        self.assertIsNone(au.take_result())  # reported once
        au.clean_up('0.1.2-dev.23', result, self.app.resolve())
        self.assertFalse(backup.exists())
        self.assertFalse((au.updates_root() / '0.1.2-dev.23').exists())

    def test_failure_while_copying_restores_the_old_folder_and_reopens_it(self):
        with patch.object(au.shutil, 'copytree', side_effect=OSError('disk full')):
            code = au.apply_update(self.staged / 'EpochPact', self.app, 1234, wait=lambda pid, s: True, start=self.start)
        self.assertEqual(code, 1)
        self.assertEqual((self.app / 'EpochPact.exe').read_bytes(), b'MZ old')
        self.assertEqual([p.name for p in self.root.iterdir() if p.name.startswith('EpochPact.previous')], [])
        self.assertEqual(self.started[0][0], [str(self.app.resolve() / 'EpochPact.exe')])
        result = au.take_result()
        self.assertFalse(result['ok'])
        self.assertIn('OSError', result['error'])
        au.clean_up('0.1.2-dev.22', result, self.app.resolve())
        self.assertFalse((au.updates_root() / '0.1.2-dev.23').exists())  # downloaded again next time

    def test_old_app_still_running_or_tampered_download_changes_nothing(self):
        code = au.apply_update(self.staged / 'EpochPact', self.app, 1234, wait=lambda pid, s: False, start=self.start)
        self.assertEqual(code, 1)
        self.assertEqual(self.started, [])  # the old app is still open
        self.assertIn('did not close', au.take_result()['error'])
        (self.staged / 'EpochPact/_internal/new.pyc').write_bytes(b'tampered')
        with patch.object(au, '_rename', side_effect=AssertionError('the app folder must not be touched')):
            code = au.apply_update(self.staged / 'EpochPact', self.app, 1234, wait=lambda pid, s: True, start=self.start)
        self.assertEqual(code, 1)
        self.assertEqual((self.app / 'EpochPact.exe').read_bytes(), b'MZ old')
        self.assertIn('checksum', au.take_result()['error'])

    def test_a_file_damaged_while_copying_rolls_back(self):
        copy = shutil.copytree

        def damaging(source, target, *args, **kw):  # copytree calls itself for sub-folders
            result = copy(source, target, *args, **kw)
            if Path(target).name == 'EpochPact':
                (Path(target) / '_internal/new.pyc').write_bytes(b'damaged')
            return result
        with patch.object(au.shutil, 'copytree', side_effect=damaging):
            code = au.apply_update(self.staged / 'EpochPact', self.app, 1234, wait=lambda pid, s: True, start=self.start)
        self.assertEqual(code, 1)
        self.assertEqual((self.app / 'EpochPact.exe').read_bytes(), b'MZ old')
        self.assertFalse((self.app / '_internal/new.pyc').exists())
        self.assertIn('checksum', au.take_result()['error'])

    def test_only_an_update_staged_by_epochpact_is_applied(self):
        elsewhere = self.temp / 'elsewhere' / 'EpochPact'
        shutil.copytree(self.staged / 'EpochPact', elsewhere)
        code = au.apply_update(elsewhere, self.app, 1234, wait=lambda pid, s: True, start=self.start)
        self.assertEqual(code, au.APPLY_REFUSED)
        self.assertEqual((self.app / 'EpochPact.exe').read_bytes(), b'MZ old')
        self.assertEqual(len(self.started), 1)  # the unchanged app is reopened
        self.assertIn('download folder', au.take_result()['error'])
        not_app = self.temp / 'not-an-app'
        not_app.mkdir()
        self.assertEqual(au.apply_update(self.staged / 'EpochPact', not_app, 1, wait=lambda p, s: True, start=self.start), au.APPLY_REFUSED)
        self.assertEqual(len(self.started), 1)

    def test_clean_up_removes_only_its_own_backup_and_older_downloads(self):
        stranger = self.root / 'EpochPact.previous-mine'
        stranger.mkdir()
        elsewhere = self.temp / 'EpochPact.previous-0.1.2-dev.22-abc'
        elsewhere.mkdir()
        (au.updates_root() / '0.1.2-dev.21').mkdir(parents=True)
        (au.updates_root() / '0.1.2-dev.30').mkdir(parents=True)
        au.clean_up('0.1.2-dev.23', {'ok': True, 'to': '0.1.2-dev.23', 'backup': str(elsewhere)}, self.app)
        self.assertTrue(elsewhere.exists())  # not beside this app
        self.assertTrue(stranger.exists())
        self.assertFalse((au.updates_root() / '0.1.2-dev.21').exists())
        self.assertFalse((au.updates_root() / '0.1.2-dev.23').exists())
        self.assertTrue((au.updates_root() / '0.1.2-dev.30').exists())


class TargetTests(Isolated):
    def test_only_a_complete_app_folder_in_a_changeable_folder_is_accepted(self):
        with self.assertRaisesRegex(au.UpdateError, 'release ZIP'):
            au.check_target(None)
        app = self.temp / 'Apps' / 'EpochPact'
        app.mkdir(parents=True)
        (app / 'EpochPact.exe').write_bytes(b'MZ')
        with self.assertRaisesRegex(au.UpdateError, 'incomplete'):
            au.check_target(app)
        (app / 'build-info.json').write_text('{}')
        self.assertEqual(au.check_target(app), app)
        self.assertEqual(sorted(p.name for p in app.parent.iterdir()), ['EpochPact'])  # the write probe is gone
        staged = au.updates_root() / '0.1.2-dev.23' / 'package' / 'EpochPact'
        shutil.copytree(app, staged)
        with self.assertRaisesRegex(au.UpdateError, 'in progress'):
            au.check_target(staged)
        with patch.object(au.Path, 'mkdir', side_effect=PermissionError('denied')):
            with self.assertRaisesRegex(au.UpdateError, 'does not allow changes'):
                au.check_target(app)


class UpdaterTests(Isolated):
    def test_background_download_stage_and_install_states(self):
        archive = package_zip(self.temp / 'p.zip', '0.1.2-dev.23', {'_internal/a.pyc': b'a'})
        data = archive.read_bytes()
        info = au.release_info(release(size=len(data), digest='sha256:' + sha(data)), '0.1.2-dev.22')
        app = self.temp / 'Apps' / 'EpochPact'
        app.mkdir(parents=True)
        (app / 'EpochPact.exe').write_bytes(b'MZ')
        (app / 'build-info.json').write_text('{"version": "0.1.2-dev.22"}')
        updater = au.Updater('0.1.2-dev.22', opener=lambda r, timeout: FakeResponse(data), folder=lambda: app)
        self.assertEqual(updater.start(info)['state'], 'downloading')
        for _ in range(200):
            if updater.status()['state'] not in ('downloading', 'verifying'):
                break
            time.sleep(0.02)
        self.assertEqual(updater.status()['state'], 'ready', updater.status())
        self.assertFalse((au.updates_root() / '0.1.2-dev.23' / info['asset']['name']).exists())
        launched = []
        with patch.object(au, 'launch_apply', side_effect=lambda *a: launched.append(a)):
            self.assertEqual(updater.install(99, ['--no-open'])['state'], 'installing')
        self.assertEqual(launched[0][1:], (app, 99, ['--no-open']))
        self.assertEqual(launched[0][0], au.updates_root() / '0.1.2-dev.23' / 'package')

    def test_refusals_before_anything_is_downloaded(self):
        updater = au.Updater('0.1.2-dev.22', folder=lambda: None)
        with self.assertRaisesRegex(au.UpdateError, 'release ZIP'):
            updater.start(au.release_info(release(), '0.1.2-dev.22'))
        with self.assertRaises(au.UpdateError):
            updater.start(au.release_info(release('0.1.2-dev.22'), '0.1.2-dev.22'))
        with self.assertRaisesRegex(au.UpdateError, 'SHA-256'):
            updater.start(au.release_info(release(digest=''), '0.1.2-dev.22'))
        with self.assertRaisesRegex(au.UpdateError, 'first'):
            updater.install(1)
        self.assertFalse(au.updates_root().exists())


class EndpointTests(Isolated):
    def test_update_endpoints_need_the_session_token_and_a_newer_release(self):
        import epochpact_ui
        from tools.ui_bridge import UiBridge
        with patch.object(au, 'check', return_value={'ok': True, 'updateAvailable': False, 'release': None}):
            app = epochpact_ui.create_app(UiBridge(preview=True))
            client = app.test_client()
            token = client.get('/api/bootstrap').get_json()['token']
            self.assertIsNone(client.get('/api/bootstrap').get_json()['updated'])
            self.assertEqual(client.post('/api/update/download').status_code, 403)
            self.assertEqual(client.post('/api/update/install').status_code, 403)
            headers = {'X-Epoch-Token': token}
            self.assertFalse(client.post('/api/update/download', headers=headers).get_json()['ok'])
            self.assertIn('desktop app', client.post('/api/update/install', headers=headers).get_json()['error'])
            app.extensions['epoch_request_exit'] = lambda: None
            self.assertIn('Download the update first', client.post('/api/update/install', headers=headers).get_json()['error'])
            self.assertEqual(client.get('/api/update/progress').get_json()['state'], 'idle')
            for _ in range(100):  # the background check finishes while the fake answer is in place
                answer = client.get('/api/update').get_json()
                if not answer.get('pending'):
                    break
                time.sleep(0.02)
            self.assertFalse(answer['canInstall'])
            self.assertFalse(answer['updateAvailable'])


if __name__ == '__main__':
    unittest.main()
