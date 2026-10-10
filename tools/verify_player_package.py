"""Exercise the compiled player EXE with no external Python on its PATH.

Uses isolated, non-runnable game fixtures only. Never launches a game, loads a
character, sends game IPC or touches the owner's installation or saves.
Run with the packaging environment (PyInstaller is needed to inspect the archive).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
from pathlib import Path
import socket
import subprocess
import tempfile
import secrets
import stat
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from PyInstaller.archive.readers import CArchiveReader


ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture(root, name, assembly):
    folder = root / name
    folder.mkdir()
    (folder / 'Last Epoch.exe').write_bytes(b'MZ isolated non-runnable installer fixture')
    shutil.copy2(assembly, folder / 'GameAssembly.dll')
    (folder / 'Last Epoch_Data').mkdir()
    return folder / 'Last Epoch.exe'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'dist/EpochPact.exe')
    parser.add_argument('--report', type=Path, default=ROOT / 'research/live/player-package-20261009/verification.json')
    parser.add_argument('--game-assembly', type=Path, required=True,
                        help='Local verified game DLL used only in non-runnable fixtures; never packaged.')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    checks = []
    archive = CArchiveReader(str(exe))
    onedir = (exe.parent / '_internal').is_dir()
    if onedir:
        resource_root = exe.parent / '_internal'
        entries = {p.relative_to(resource_root).as_posix(): p for p in resource_root.rglob('*') if p.is_file()}
        def extract(name):
            return entries[name].read_bytes()
        assert not any(n.lower().endswith(('.zip', '.7z', '.jar', '.tar.gz')) for n in entries), 'Nested archive'
        assert not any(n.split('/')[0] in ('pip', 'setuptools', 'wheel', 'capstone', 'tests', 'research') for n in entries)
        info = json.loads((exe.parent / 'build-info.json').read_text())
        actual = {p.relative_to(exe.parent).as_posix(): p for p in exe.parent.rglob('*') if p.is_file()}
        assert set(actual) == {x['path'] for x in info['files']} | {'build-info.json'}
        for item in info['files']:
            data = actual[item['path']].read_bytes()
            assert len(data) == item['bytes'] and sha(data) == item['sha256'], item['path']
        checks.append('folder bundle inventory matches every file hash; no nested archives or build tools')
    else:
        entries = {name.replace('\\', '/'): name for name in archive.toc}
        def extract(name):
            return archive.extract(entries[name])
    assert any(name.lower().startswith('python') and name.lower().endswith('.dll') for name in entries)
    native_files = {name for name in entries if name.startswith('native/')}
    assert native_files == {'native/build/version.dll', 'native/build/player/EpochPact.Core.dll',
                            'native/build/player/build-info.json'}, native_files
    assert not any(name.startswith(('research/', 'live/', 'docs/')) for name in entries)
    catalog = json.loads(extract('ui/catalog.json'))
    assert catalog['liveState'] == {} and catalog['currentValues'] == {}
    core = extract('native/build/player/EpochPact.Core.dll')
    loader = extract('native/build/version.dll')
    metadata = json.loads(extract('native/build/player/build-info.json'))
    assert metadata['flavor'] == 'player' and metadata['sha256'] == sha(core)
    assert sha(exe.read_bytes()) == json.loads((exe.parent / 'build-info.json').read_text())['sha256']
    assert extract('ui/launcher.js') == (ROOT / 'ui/launcher.js').read_bytes()
    for script in ('app.js', 'session.js'):
        assert extract('ui/' + script) == (ROOT / 'ui' / script).read_bytes(), script
    checks.extend(['bundled Python runtime', 'verified player DLL only',
                   'no historical character snapshots', 'current launcher source in EXE'])

    with tempfile.TemporaryDirectory(prefix='EpochPact-package-check-') as directory:
        root = Path(directory)
        assert root.resolve().parent == Path(tempfile.gettempdir()).resolve()
        user = root / 'user-data'
        user.mkdir()
        # The entire distributable layout must work after extraction into a path
        # unrelated to the build machine, with no external Python on PATH.
        portable = root / 'Extracted app ü'
        portable.mkdir()
        if onedir:
            shutil.copytree(exe.parent, portable / 'EpochPact')
            exe = portable / 'EpochPact/EpochPact.exe'
        else:
            shutil.copy2(exe, portable / exe.name)
            exe = portable / exe.name
        custom = fixture(root, 'Custom install \u00fc', args.game_assembly)
        foreign = fixture(root, 'Other mod loader', args.game_assembly)
        unsupported = fixture(root, 'Unsupported game build', args.game_assembly)
        (unsupported.parent / 'GameAssembly.dll').write_bytes(b'unsupported fixture')
        (foreign.parent / 'version.dll').write_bytes(b'foreign loader: preserve exactly')
        env = os.environ.copy()
        for key in ('PYTHONPATH', 'PYTHONHOME', 'VIRTUAL_ENV', 'EPOCHPACT_GAME_DIR'):
            env.pop(key, None)
        windows = Path(env.get('SystemRoot', r'C:\Windows'))
        env.update(PATH=str(windows / 'System32') + ';' + str(windows), EPOCHPACT_USER_DIR=str(user))

        def helper(game, result_name):
            from tools.app_paths import installer_result_root, validate_installer_result
            # The elevated path intentionally ignores EPOCHPACT_USER_DIR. Only a
            # fresh nonce result file is written to the OS-known local data folder.
            result = installer_result_root() / ('setup-install-' + secrets.token_hex(16) + '.json')
            validate_installer_result(str(result))
            result.parent.mkdir(parents=True, exist_ok=True)
            try:
                process = subprocess.run([str(exe), '--install-plugin', '--game-exe', str(game),
                                          '--install-result', str(result)], env=env, cwd=root, timeout=45)
                return process.returncode, json.loads(result.read_text())
            finally:
                result.unlink(missing_ok=True)

        code, result = helper(custom, 'setup-install-' + 'a' * 32 + '.json')
        assert code == 0 and result['ok'], result
        assert (custom.parent / 'version.dll').read_bytes() == loader
        assert (custom.parent / 'EpochPact/EpochPact.Core.dll').read_bytes() == core
        checks.append('compiled installer helper installs exact DLLs in custom Unicode folder without Python')
        if onedir:
            # Use real Windows read-only flags on the extracted app's payload
            # and the existing owned mod, never on the build or owner's game.
            payload_files = [exe.parent / '_internal/native/build/version.dll',
                             exe.parent / '_internal/native/build/player/EpochPact.Core.dll']
            installed_files = [custom.parent / 'version.dll', custom.parent / 'EpochPact/EpochPact.Core.dll',
                               custom.parent / 'EpochPact/installed-build.json']
            old_bytes = {p.name: p.read_bytes() for p in installed_files}
            sources_attributes = {p: p.stat().st_file_attributes for p in payload_files}
            readonly_fresh = fixture(root, 'Read-only download fresh install', args.game_assembly)
            try:
                for p in payload_files + installed_files:
                    p.chmod(stat.S_IREAD)
                    assert p.stat().st_file_attributes & stat.FILE_ATTRIBUTE_READONLY
                for game in (readonly_fresh, custom):
                    code, result = helper(game, 'readonly-install')
                    assert code == 0 and result['ok'], result
                    installed_loader = game.parent / 'version.dll'
                    installed_core = game.parent / 'EpochPact/EpochPact.Core.dll'
                    assert installed_loader.read_bytes() == loader
                    assert installed_core.read_bytes() == core
                    for p in (installed_loader, installed_core, game.parent / 'EpochPact/installed-build.json'):
                        assert not p.stat().st_file_attributes & stat.FILE_ATTRIBUTE_READONLY
                    assert not list((game.parent / 'EpochPact').glob('.install-*'))
                for p in payload_files:
                    assert p.stat().st_file_attributes & stat.FILE_ATTRIBUTE_READONLY
                backups = list((custom.parent / 'EpochPact/install-backups').iterdir())
                assert len(backups) == 1
                for name, data in old_bytes.items():
                    assert (backups[0] / name).read_bytes() == data
                checks.extend(['compiled EXE fresh install from actual read-only bundled DLLs preserves source flags and exact bytes',
                               'compiled EXE updates actual read-only owned files with exact recovery backups and no leaked stages'])
            finally:
                for p in payload_files:
                    if p.exists():
                        p.chmod(stat.S_IREAD if sources_attributes[p] & stat.FILE_ATTRIBUTE_READONLY else stat.S_IREAD | stat.S_IWRITE)
                for p in installed_files:
                    if p.exists(): p.chmod(stat.S_IREAD | stat.S_IWRITE)
        before = (foreign.parent / 'version.dll').read_bytes()
        code, result = helper(foreign, 'setup-install-' + 'b' * 32 + '.json')
        assert code != 0 and not result['ok']
        assert (foreign.parent / 'version.dll').read_bytes() == before
        assert not (foreign.parent / 'EpochPact').exists()
        checks.append('compiled installer refuses another mod loader without changes')
        code, result = helper(unsupported, 'setup-install-' + 'c' * 32 + '.json')
        assert code != 0 and not result['ok'] and 'Unsupported' in result['error'], result
        assert not (unsupported.parent / 'version.dll').exists()
        assert not (unsupported.parent / 'EpochPact').exists()
        checks.append('compiled installer refuses an unsupported game build before writing files')

        invalid_result_game = fixture(root, 'Invalid receipt folder', args.game_assembly)
        unapproved_result = root / ('setup-install-' + secrets.token_hex(16) + '.json')
        unapproved_result.write_text('keep this unrelated file', encoding='utf-8')
        process = subprocess.run([str(exe), '--install-plugin', '--game-exe', str(invalid_result_game),
                                  '--install-result', str(unapproved_result)], env=env, cwd=root, timeout=45)
        from tools.app_paths import INSTALL_RESULT_INVALID
        assert process.returncode == INSTALL_RESULT_INVALID, process.returncode
        assert unapproved_result.read_text(encoding='utf-8') == 'keep this unrelated file'
        assert not (invalid_result_game.parent / 'version.dll').exists()
        assert not (invalid_result_game.parent / 'EpochPact').exists()
        checks.append('compiled helper returns code 20 for an unapproved receipt folder without installing or overwriting a file')

        with socket.socket() as reservation:
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
        url = f'http://127.0.0.1:{port}'

        def request(path, payload=None, token=None):
            headers = {'Content-Type': 'application/json'}
            if token:
                headers['X-Epoch-Token'] = token
            req = Request(url + path, data=json.dumps(payload).encode() if payload is not None else None, headers=headers)
            with urlopen(req, timeout=3) as reply:
                return reply.read()

        def boot():
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    return json.loads(request('/api/bootstrap'))
                except (URLError, TimeoutError, OSError):
                    time.sleep(.1)
            raise RuntimeError('Compiled interface did not become ready within 30 seconds.')

        durations = []
        def job(payload, token):
            start = time.monotonic()
            queued = json.loads(request('/api/jobs', payload, token))
            deadline = start + 15
            while time.monotonic() < deadline:
                state = json.loads(request('/api/jobs/' + queued['job']))
                if state['done']:
                    durations.append(round((time.monotonic() - start) * 1000, 1))
                    return state['result']
                time.sleep(.05)
            raise RuntimeError('Compiled interface job timed out.')

        def stop(process):
            # Only the process tree created by this verifier is terminated.
            if process.poll() is None:
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               capture_output=True, timeout=10)
            process.wait(timeout=10)
            # Windows can briefly retain open file handles after forceful test
            # process termination. Do not let fixture cleanup mask a test error.
            time.sleep(.2)

        def start():
            return subprocess.Popen([str(exe), '--no-open', '--port', str(port)], env=env, cwd=root)

        process = start()
        try:
            bootstrap = boot()
            assert bootstrap['setup_version'] == 1 and not bootstrap['developer'] and not bootstrap['preview']
            assert bootstrap['catalog']['language'].startswith('en')
            assert not bootstrap['launcher']['selected']
            assert not (user / 'settings.json').exists()
            token = bootstrap['token']
            assert not job({'type': 'connection'}, token)['connected']
            assert not job({'type': 'control', 'id': 'xp', 'args': {'value': 2}}, token)['ok']
            checks.append('first launch performs no selection, installation or gameplay mutation')
            index = request('/').decode()
            assert 'setup-dialog' in index and 'launcher.js' in index
            for resource in ('app.js', 'style.css', 'stat-model.js', 'session.js', 'collection-ui.js', 'collection.css', 'launcher.js',
                             'assets/chronoforge-art.png', 'assets/void-atlas-art.png', 'assets/inter.ttf', 'assets/monster-density.svg'):
                assert len(request('/ui/' + resource)) > 0, resource
            checks.append('compiled interface serves scripts, font and both theme assets')
            try:
                request('/api/jobs', {'type': 'launcher', 'action': 'install'})
                raise AssertionError('Unauthenticated installation accepted')
            except HTTPError as error:
                assert error.code == 403
            checks.append('installation API rejects missing session token')
            for headers in ({'Host': 'untrusted.example', 'X-Epoch-Token': token},
                            {'Origin': 'https://untrusted.example', 'X-Epoch-Token': token}):
                req = Request(url + '/api/jobs', data=json.dumps({'type': 'launcher', 'action': 'install'}).encode(),
                              headers={'Content-Type': 'application/json', **headers})
                try:
                    urlopen(req, timeout=3)
                    raise AssertionError('Untrusted host / origin accepted')
                except HTTPError as error:
                    assert error.code == 403, error.code
            checks.append('compiled API rejects foreign Host and Origin even with a valid session token')
            result = job({'type': 'launcher', 'action': 'select', 'args': {'executable': str(custom)}}, token)
            assert result['ok'] and result['launcher']['installed'], result
            result = job({'type': 'launcher', 'action': 'install'}, token)
            assert result['ok'] and result['launcher']['installed'], result
            checks.append('compiled HTTP selection and explicit installation succeed through the serial job queue')
            assert json.loads((user / 'settings.json').read_text(encoding='utf-8'))['gameExecutable'] == str(custom)
            assert not (custom.parent / 'EpochPact/ipc').exists()
            assert not (custom.parent / 'EpochPact/backups').exists()
            checks.append('fixture game is never executed; no game IPC or save backups created')
        finally:
            stop(process)

        process = start()
        try:
            restarted = boot()
            assert restarted['launcher']['selected'] and restarted['launcher']['executable'] == str(custom)
            assert restarted['launcher']['installed'] and not restarted['launcher']['running']
            assert restarted['token'] != token
            checks.append('fresh EXE process remembers selected path and issues a new session token')
        finally:
            stop(process)
        # Exercise the exact Windows collision that previously let a packaged
        # UI receive responses from a legacy source server without Game setup.
        with socket.socket() as legacy:
            legacy.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            legacy.bind(('127.0.0.1', port)); legacy.listen()
            process = start()
            try:
                deadline = time.monotonic() + 30
                assigned = None
                while time.monotonic() < deadline:
                    log = (user / 'startup.log').read_text(encoding='utf-8')
                    match = re.search(rf'Interface port {port} is occupied; using (\d+)', log)
                    if match:
                        assigned = int(match.group(1)); break
                    if process.poll() is not None:
                        raise RuntimeError('Compiled EXE stopped when its port was occupied.')
                    time.sleep(.1)
                assert assigned and assigned != port, 'No independent interface port opened'
                url = f'http://127.0.0.1:{assigned}'
                independent = boot()
                assert independent['setup_version'] == 1 and independent['launcher']['selected']
                assert independent['launcher']['executable'] == str(custom)
                assert independent['token'] != restarted['token']
                checks.append('compiled EXE rejects a legacy shared port and serves Game setup from its own backend')
            finally:
                stop(process)
        log = (user / 'startup.log').read_text(encoding='utf-8')
        assert 'Traceback' not in log and ' ERROR ' not in log
        checks.append('compiled startup logs contain no exception')

    report = {'file': str(args.exe.resolve()), 'layout': 'onedir' if onedir else 'onefile',
              'sha256': sha(args.exe.read_bytes()), 'bytes': args.exe.stat().st_size,
              'nativePlayerSHA256': sha(core), 'checks': checks, 'jobDurationsMs': durations,
              'scope': 'Final compiled EXE, isolated installer fixtures and HTTP interface; no interactive picker test, UAC approval or live gameplay test.'}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
