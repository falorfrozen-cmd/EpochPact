"""Test loader forwarding and DLL search rules using compiled harmless fixtures.

Never launches the game or actual core. Requires MSVC x64. Fixture DLLs only
return a digit / write an isolated marker and must not enter player packages.
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

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--loader', type=Path, required=True)
    parser.add_argument('--report', type=Path, default=ROOT / 'research/live/security-review/loader.json')
    args = parser.parse_args()
    loader = args.loader.resolve(strict=True)
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    vs = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property',
                                 'installationPath'], text=True).strip()
    if not vs:
        raise RuntimeError('MSVC x64 required')
    checks = []
    with tempfile.TemporaryDirectory(prefix='EpochPact-loader-check-') as temporary:
        root = Path(temporary).resolve()
        assert root.parent == Path(tempfile.gettempdir()).resolve()
        host = root / 'Host'; host.mkdir()
        core_dir = host / 'EpochPact'; core_dir.mkdir()
        current = root / 'Untrusted current directory'; current.mkdir()
        source = ROOT / 'native/tests'
        lines = [f'@call "{Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"}" >nul || exit /b 1',
                 '@echo off',
                 f'cl /nologo /O2 /MT /EHsc /std:c++20 /Fe"{host / "Last Epoch.exe"}" "{source / "loader_probe_host.cpp"}" || exit /b 1',
                 f'cl /nologo /O2 /MT /LD /DPROBE_VALUE=1 /Fe"{core_dir / "EpochPactSecurityProbe.dll"}" "{source / "loader_probe_dependency.cpp"}" /link /IMPLIB:"{root / "probe.lib"}" || exit /b 1',
                 f'cl /nologo /O2 /MT /LD /DPROBE_VALUE=2 /Fe"{current / "EpochPactSecurityProbe.dll"}" "{source / "loader_probe_dependency.cpp"}" || exit /b 1',
                 f'cl /nologo /O2 /MT /LD /Fe"{core_dir / "EpochPact.Core.dll"}" "{source / "loader_probe_core.cpp"}" "{root / "probe.lib"}" || exit /b 1']
        batch = root / 'compile.bat'; batch.write_text('\n'.join(lines) + '\n')
        built = subprocess.run(['cmd', '/d', '/c', str(batch)], cwd=root, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)
        installed = host / 'version.dll'; shutil.copy2(loader, installed)
        environment = os.environ.copy()
        marker = root / 'probe-result.txt'
        environment['EPOCHPACT_LOADER_PROBE'] = str(marker)
        environment['PATH'] = str(current)  # A same-named dependency is deliberately on PATH too.
        def run(expected):
            process = subprocess.run([str(host / 'Last Epoch.exe'), str(installed), str(expected)],
                                     cwd=current, env=environment, capture_output=True, text=True, timeout=10)
            if process.returncode:
                raise AssertionError(f'Loader fixture failed ({process.returncode}): {process.stdout} {process.stderr}')
        run(1)
        assert marker.read_text() == '1'
        checks.append('17 exports present; forwarded version API matches System32; core resolves dependency beside itself')
        marker.unlink()
        (core_dir / 'EpochPactSecurityProbe.dll').rename(root / 'trusted-dependency.dll')
        run(0)
        assert not marker.exists()
        checks.append('missing trusted dependency fails closed; same-named DLL in current directory / PATH is not loaded')
        (root / 'trusted-dependency.dll').rename(core_dir / 'EpochPactSecurityProbe.dll')
        (core_dir / 'disabled').write_text('isolated test')
        run(0)
        assert not marker.exists()
        checks.append('disabled marker prevents core initialization without breaking version forwarding')
    report = dict(loaderSHA256=hashlib.sha256(loader.read_bytes()).hexdigest(), checks=checks,
                  scope='Compiled benign fixtures only; no game launch, game hooks, saves or live gameplay validation.')
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
