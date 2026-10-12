"""Verify a player ZIP against the production C++ protocol simulator.

This is real process/file IPC with simulated game data, not live Last Epoch.
No owner installation, player save, gameplay write or game DLL is executed.
Actual WebView desktop closure and live gameplay are separate checks.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import subprocess
import tempfile
import time
from urllib.request import Request, urlopen
import zipfile
from tools.package_nexus_release import verify_archive


def verify(archive: Path, native_host: Path, assembly: Path):
    verified = verify_archive(archive)
    report = {'version': verified['version'], 'packageSHA256': verified['sha256'],
              'scope': 'Final extracted frozen EXE and real Windows process/file IPC with production C++ parser; game data simulated. No live gameplay or desktop-close claim.',
              'checks': [], 'durationsMs': []}
    with tempfile.TemporaryDirectory(prefix='EpochPact-frozen-connection-') as directory:
        root = Path(directory)
        extracted = root / 'Extracted app ü'
        extracted.mkdir()
        with zipfile.ZipFile(archive) as bundle:
            bundle.extractall(extracted)
        manifest = json.loads((extracted / 'runtime-manifest.json').read_text(encoding='utf-8'))
        for item in manifest['files']:
            file = extracted / item['path']
            assert hashlib.sha256(file.read_bytes()).hexdigest() == item['sha256']
            Path(str(file) + ':Zone.Identifier').write_text('[ZoneTransfer]\nZoneId=3\n', encoding='ascii')
        app = extracted / 'EpochPact/EpochPact.exe'
        game = root / 'Protocol game fixture ü'
        game.mkdir()
        fixture = game / 'Last Epoch.exe'
        shutil.copy2(native_host, fixture)
        shutil.copy2(assembly, game / 'GameAssembly.dll')
        (game / 'Last Epoch_Data').mkdir()
        shutil.copytree(extracted / 'Manual Installation', game, dirs_exist_ok=True)
        ipc = game / 'EpochPact/ipc'
        ipc.mkdir()
        announcement = ipc / 'protocol.json'
        announcement.write_text('{"version":2,"pid":1}', encoding='utf-8')
        os.utime(announcement, (1, 1))
        announcement.chmod(stat.S_IREAD)
        user = root / 'user-data'
        user.mkdir()
        (user / 'settings.json').write_text(json.dumps({'schema': 1, 'gameExecutable': str(fixture)}), encoding='utf-8')
        env = os.environ.copy()
        for key in ('PYTHONPATH', 'PYTHONHOME', 'VIRTUAL_ENV', 'EPOCHPACT_GAME_DIR'):
            env.pop(key, None)
        windows = Path(env.get('SystemRoot') or env.get('SYSTEMROOT') or r'C:\Windows')
        env.update(PATH=str(windows / 'System32') + ';' + str(windows), EPOCHPACT_USER_DIR=str(user))
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            port = listener.getsockname()[1]
        url = f'http://127.0.0.1:{port}'
        host = desktop = None

        def start_host(delay=0):
            (ipc / 'stop').unlink(missing_ok=True)
            return subprocess.Popen([str(fixture), str(ipc), str(delay)], creationflags=subprocess.CREATE_NO_WINDOW)

        def stop_host():
            if host and host.poll() is None:
                (ipc / 'stop').touch()
                host.wait(timeout=15)
            if host:
                assert host.returncode == 0

        def request(path, payload=None, token=None):
            headers = {'Content-Type': 'application/json'}
            if token:
                headers['X-Epoch-Token'] = token
            query = Request(url + path, data=json.dumps(payload).encode() if payload is not None else None, headers=headers)
            with urlopen(query, timeout=4) as reply:
                return reply.read()

        def job(payload, token):
            begin = time.monotonic()
            queued = json.loads(request('/api/jobs', payload, token))
            assert queued['ok']
            while time.monotonic() - begin < 45:
                state = json.loads(request('/api/jobs/' + queued['job']))
                if state['done']:
                    report['durationsMs'].append(round((time.monotonic() - begin) * 1000, 1))
                    return state['result']
                time.sleep(.05)
            raise TimeoutError('Compiled connection verification exceeded 45 seconds')

        try:
            host = start_host()
            desktop = subprocess.Popen([str(app), '--no-open', '--port', str(port)], env=env, cwd=extracted,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    boot = json.loads(request('/api/bootstrap'))
                    break
                except OSError:
                    assert desktop.poll() is None
                    time.sleep(.1)
            else:
                raise TimeoutError('Compiled EXE did not start')
            assert boot['launcher']['installed'] and boot['setup_version'] == 1
            token = boot['token']
            ready = job({'type': 'connection'}, token)
            assert ready['phase'] == 'ready' and ready['offline'] and ready['processId'] == host.pid
            report['checks'].append('extracted frozen EXE connects with stale read-only protocol announcement and no external Python on PATH')
            first = ready['sessionKey']
            stop_host()
            host = start_host(4000)
            ready = job({'type': 'connection'}, token)
            assert ready['phase'] == 'ready' and ready['sessionKey'] != first and ready['processId'] == host.pid
            assert report['durationsMs'][-1] > 8000
            report['checks'].append('same compiled backend reconnects after process restart; separate slow reads finish beyond seven seconds')
            stop_host()
            host = start_host()
            core = game / 'EpochPact/EpochPact.Core.dll'
            expected = core.read_bytes()
            core.write_bytes(expected + b'isolated wrong-package fixture')
            mismatch = job({'type': 'connection'}, token)
            assert mismatch['phase'] == 'update_required' and not mismatch['connected']
            core.write_bytes(expected)
            ready = job({'type': 'connection'}, token)
            assert ready['phase'] == 'ready' and ready['offline']
            report['checks'].append('owned core mismatch blocks connection explicitly; matching bytes restore readiness')
            for asset in ('app.js', 'style.css', 'session.js', 'launcher.js', 'crafting-model.js', 'affix-picker.js', 'assets/chronoforge-art.png', 'assets/inter.ttf'):
                assert request('/ui/' + asset) == (app.parent / '_internal/ui' / asset).read_bytes()
            assert announcement.stat().st_mtime_ns == 1_000_000_000
            assert not (ipc / 'mutations').exists()
            assert not (game / 'EpochPact/backups').exists()
            assert not (game / 'EpochPact/workshop-recipes.txt').exists()
            report['checks'].append('current crafting/UI resources are served exactly; no gameplay mutations, save backups or recipes generated')
            report['verified'] = True
        finally:
            stop_host()
            announcement.chmod(stat.S_IWRITE | stat.S_IREAD)
            if desktop and desktop.poll() is None:
                subprocess.run(['taskkill', '/PID', str(desktop.pid), '/T', '/F'], capture_output=True, timeout=10)
                desktop.wait(timeout=10)
            time.sleep(.2)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zip', type=Path, required=True)
    parser.add_argument('--native-host', type=Path, required=True)
    parser.add_argument('--game-assembly', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    result = verify(args.zip.resolve(strict=True), args.native_host.resolve(strict=True), args.game_assembly.resolve(strict=True))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
