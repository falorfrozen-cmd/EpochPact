"""Test the player loader (core linked into version.dll) in real processes, without the game.

A compiled stand-in host is copied as "Last Epoch.exe" and as "UnityCrashHandler64.exe" into a
temporary folder beside the loader. The stand-in checks all 17 exports and that the forwarded
version APIs return what System32's version.dll returns. The linked core must start only in the
"Last Epoch.exe" host (it writes EpochPact\\logs\\core.log), never in the crash handler, and not
when EpochPact\\disabled exists. Requires MSVC x64. Never launches the game.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--loader', type=Path, default=ROOT / 'native/build/player/version.dll')
    parser.add_argument('--report', type=Path, default=ROOT / 'research/live/security-review/loader-linked.json')
    args = parser.parse_args()
    loader = args.loader.resolve(strict=True)
    from tools import le_session as le
    if not le.is_linked_loader(loader):
        raise ValueError('Not the linked player loader: ' + str(loader))
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property',
                                  'installationPath'], text=True).strip()
    if not vs:
        raise RuntimeError('MSVC x64 required')
    checks = []
    with tempfile.TemporaryDirectory(prefix='EpochPact-linked-loader-check-') as temporary:
        root = Path(temporary).resolve()
        assert root.parent == Path(tempfile.gettempdir()).resolve()
        host = root / 'Host'
        host.mkdir()
        batch = root / 'compile.bat'
        batch.write_text(f'@call "{Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"}" >nul || exit /b 1\n'
                         f'@cl /nologo /O2 /MT /EHsc /std:c++20 /Fe"{host / "Last Epoch.exe"}" '
                         f'"{ROOT / "native/tests/loader_probe_host.cpp"}" >nul || exit /b 1\n')
        built = subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=root, capture_output=True, text=True, timeout=120)
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)
        shutil.copy2(host / 'Last Epoch.exe', host / 'UnityCrashHandler64.exe')
        installed = host / 'version.dll'
        shutil.copy2(loader, installed)
        environment = dict(os.environ, EPOCHPACT_LOADER_PROBE=str(root / 'never-written.txt'))
        core_log = host / 'EpochPact/logs/core.log'
        loader_log = host / 'EpochPact/logs/loader.log'

        def run(name):
            process = subprocess.run([str(host / name), str(installed), '0'], cwd=root, env=environment,
                                     capture_output=True, text=True, timeout=30)
            if process.returncode:
                raise AssertionError(f'{name} failed ({process.returncode}): {process.stdout} {process.stderr}')

        def wait_for(path, seconds=5.0):
            end = time.time() + seconds
            while time.time() < end:
                if path.exists() and path.read_text(encoding='utf-8', errors='replace').strip():
                    return True
                time.sleep(0.05)
            return False

        run('UnityCrashHandler64.exe')
        time.sleep(0.5)
        assert not core_log.exists() and not (host / 'EpochPact').exists(), 'core started inside the crash handler'
        checks.append('crash-handler host: 17 exports forward like System32; no core, no log, no files written')
        run('Last Epoch.exe')
        assert wait_for(core_log), 'the linked core did not start in Last Epoch.exe'
        assert 'loaded (pid' in core_log.read_text(encoding='utf-8', errors='replace')
        assert 'linked' in loader_log.read_text(encoding='utf-8', errors='replace')
        checks.append('game host: 17 exports forward like System32; the linked core starts with no separate core file present')
        assert not list(host.rglob('EpochPact.Core.dll')), 'a separate core appeared'
        shutil.rmtree(host / 'EpochPact')
        (host / 'EpochPact').mkdir()
        (host / 'EpochPact/disabled').write_text('isolated test')
        run('Last Epoch.exe')
        time.sleep(0.5)
        assert not core_log.exists(), 'the linked core started despite EpochPact\\disabled'
        checks.append('disabled marker keeps the linked core from starting without breaking version forwarding')
    report = dict(loaderSHA256=hashlib.sha256(loader.read_bytes()).hexdigest(), checks=checks,
                  scope='Compiled benign host only; no game launch, game hooks, saves or live gameplay validation.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
