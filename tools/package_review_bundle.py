"""Package an audited onedir build without changing published Nexus artifacts."""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path
import zipfile
from tools.package_nexus_release import ROOT, sha, safe_archive_name, verify_archive


def read_bundle(bundle):
    if bundle.is_symlink() or bundle.is_junction():
        raise ValueError('Linked bundle root')
    bundle = bundle.resolve(strict=True)
    info = json.loads((bundle / 'build-info.json').read_text())
    if info.get('layout') != 'onedir' or info.get('containsResearchBuild') is not False \
            or info.get('containsHistoricalSnapshots') is not False:
        raise ValueError('Only a verified player folder build may be packaged')
    files = {}
    for path in bundle.rglob('*'):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Linked path in bundle')
        if path.is_file():
            name = safe_archive_name(path.relative_to(bundle).as_posix())
            if path.resolve().is_relative_to(bundle) is False:
                raise ValueError('Bundle path escaped its root')
            files[name] = path
    listed = [safe_archive_name(x['path']) for x in info['files']]
    if len({n.casefold() for n in listed}) != len(listed):
        raise ValueError('Duplicate bundle manifest entry')
    if set(files) != set(listed) | {'build-info.json'}:
        raise ValueError('Unexpected or missing bundle files')
    for item in info['files']:
        data = files[item['path']].read_bytes()
        if len(data) != item['bytes'] or sha(data) != item['sha256']:
            raise ValueError('Bundle digest mismatch: ' + item['path'])
    if sha(files['EpochPact.exe'].read_bytes()) != info['sha256']:
        raise ValueError('EXE digest mismatch')
    for name in files:
        if name.lower().endswith(('.zip', '.7z', '.jar', '.whl', '.tar', '.gz', '.log', '.sav', '.obj', '.pdb', '.lib', '.exp')):
            raise ValueError('Archive, log, save or development file in bundle: ' + name)
        # Jinja's tests.pyc implements template predicates; it is runtime code.
        # Reject our test/research directories and build-tool packages, not any
        # third-party module whose filename happens to contain "test".
        if any(p in ('research', 'live', 'tests', 'test', 'setuptools', 'pip', 'wheel', 'capstone') for p in name.split('/')[:-1]) \
                or name.startswith('_internal/tools/test_') \
                or name in {f'_internal/{p}.pyc' for p in ('setuptools', 'pip', 'wheel', 'capstone')}:
            raise ValueError('Development-only component in bundle: ' + name)
    resources = bundle / '_internal'
    native = resources / 'native/build'
    native_names = {p.relative_to(native).as_posix() for p in native.rglob('*') if p.is_file()}
    if native_names != {'version.dll', 'player/EpochPact.Core.dll', 'player/build-info.json'}:
        raise ValueError('Unexpected native artifact in bundle')
    core = native / 'player/EpochPact.Core.dll'
    core_info = json.loads((core.parent / 'build-info.json').read_text())
    if core_info['flavor'] != 'player' or core_info['sha256'] != sha(core.read_bytes()) \
            or info['nativePlayerSHA256'] != core_info['sha256']:
        raise ValueError('Player core mismatch')
    if sha((native / 'version.dll').read_bytes()) != info['loaderSHA256']:
        raise ValueError('Loader mismatch')
    catalog = json.loads((resources / 'ui/catalog.json').read_text(encoding='utf-8'))
    if catalog['liveState'] or catalog['currentValues']:
        raise ValueError('Historical character snapshot in player resources')
    return info, files


def package(bundle, output):
    info, files = read_bundle(bundle)
    version = info['version']
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', version):
        raise ValueError('Invalid candidate version')
    payload = {'EpochPactUI/' + n: p.read_bytes() for n, p in files.items()}
    resource = bundle / '_internal'
    additions = {
        'CopyToGame/version.dll': resource / 'native/build/version.dll',
        'CopyToGame/EpochPact/EpochPact.Core.dll': resource / 'native/build/player/EpochPact.Core.dll',
        'README-MANUAL.md': ROOT / 'docs/security/README-REVIEW-MANUAL.md',
        'DISTRIBUTION-PERMISSIONS.txt': ROOT / 'docs/nexus/DISTRIBUTION-PERMISSIONS.txt',
        'THIRD-PARTY-NOTICES.txt': resource / 'THIRD-PARTY-NOTICES.txt',
        'INTER-OFL.txt': resource / 'ui/assets/inter-OFL.txt',
        'supported-game-builds.json': resource / 'ui/supported-game-builds.json',
    }
    payload.update({n: p.read_bytes() for n, p in additions.items()})
    manifest = dict(version=version, game='Last Epoch', author='Falor', kind='Manual-Review',
                    securityStatus='Review candidate; previous detections unexplained; no Nexus clearance',
                    github='https://github.com/falorfrozen-cmd/EpochPact', discord='https://discord.gg/q6aexZZSAf',
                    runtime=[n for n in payload if n.lower().endswith(('.exe', '.dll', '.pyd', '.pyc'))],
                    files=[dict(path=n, bytes=len(d), sha256=sha(d)) for n, d in sorted(payload.items())])
    payload['runtime-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    payload['SHA256SUMS.txt'] = ''.join(f'{sha(d)}  {n}\n' for n, d in sorted(payload.items())).encode()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'EpochPact-{version}-Manual-Review.zip'
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(safe_archive_name(name), date_time=(2026, 10, 10, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    result = verify_archive(destination)
    (output / (destination.name + '.sha256')).write_text(result['sha256'] + '  ' + destination.name + '\n')
    (output / (destination.name + '.verification.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/security-review/packages')
    args = parser.parse_args()
    result = package(args.bundle, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != 'runtime'} | {
        'runtimeFiles': len(result.get('runtime', []))}, indent=2))
