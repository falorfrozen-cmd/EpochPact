"""Allowlisted Last Epoch player ZIPs with independently verified payload hashes.

No game binaries, development SDK, tests, logs or saves are copied. Does not build,
install, launch, upload or alter a game. Unlike ModLab this needs EXE/DLL runtime.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def safe_archive_name(name):
    reserved = {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{i}' for p in ('COM', 'LPT') for i in range(1, 10)}
    if not isinstance(name, str) or not name or '\\' in name or '\x00' in name:
        raise ValueError('Unsafe archive path')
    for part in name.split('/'):
        if not part or part in ('.', '..') or ':' in part or part.endswith(('.', ' ')) \
                or part.split('.')[0].upper() in reserved:
            raise ValueError('Unsafe archive path: ' + name)
    return name


def verify_archive(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        for name in names:
            safe_archive_name(name)
        if len({name.casefold() for name in names}) != len(names) or any(i.flag_bits & 1 for i in archive.infolist()):
            raise ValueError('Duplicate or encrypted entries')
        if archive.testzip() is not None:
            raise ValueError('ZIP CRC failure')
        manifest = json.loads(archive.read('runtime-manifest.json'))
        manifest_names = [safe_archive_name(x['path']) for x in manifest['files']]
        if len({n.casefold() for n in manifest_names}) != len(manifest_names):
            raise ValueError('Duplicate manifest entries')
        expected = {x['path'] for x in manifest['files']} | {'runtime-manifest.json', 'SHA256SUMS.txt'}
        if set(names) != expected:
            raise ValueError('Archive contains unexpected or missing entries')
        for item in manifest['files']:
            name = item['path']
            data = archive.read(name)
            if len(data) != item['bytes'] or sha(data) != item['sha256']:
                raise ValueError('Manifest mismatch: ' + name)
        sums = ''.join(f"{sha(archive.read(n))}  {n}\n" for n in sorted(expected - {'SHA256SUMS.txt'}))
        if archive.read('SHA256SUMS.txt').decode('utf-8') != sums:
            raise ValueError('SHA256SUMS mismatch')
        return {'file': Path(path).name, 'bytes': Path(path).stat().st_size,
                'sha256': sha(Path(path).read_bytes()), 'entries': len(names),
                'version': manifest['version'], 'runtime': manifest['runtime'], 'verified': True}


def package(kind, output):
    release = json.loads((ROOT / 'docs/nexus/release.json').read_text())
    version = release['version']
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', version):
        raise ValueError('Invalid release version')
    info = json.loads((ROOT / 'dist/build-info.json').read_text())
    core = ROOT / 'native/build/player/EpochPact.Core.dll'
    native = json.loads((core.parent / 'build-info.json').read_text())
    exe = ROOT / 'dist/EpochPact.exe'
    if sha(exe.read_bytes()) != info['sha256'] or exe.stat().st_size != info['bytes']:
        raise ValueError('Rebuild player EXE: metadata mismatch')
    if native['flavor'] != 'player' or sha(core.read_bytes()) != native['sha256'] or info['nativePlayerSHA256'] != native['sha256']:
        raise ValueError('Player DLL build mismatch')
    if info.get('version') != version:
        raise ValueError('EXE build version does not match Nexus version')
    prefix = 'EpochPactUI/' if kind == 'Manual' else ''
    files = {prefix + 'EpochPact.exe': exe,
             'README-MANUAL.md': ROOT / 'docs/nexus/README-MANUAL.md',
             'THIRD-PARTY-NOTICES.txt': ROOT / 'dist/THIRD-PARTY-NOTICES.txt',
             'INTER-OFL.txt': ROOT / 'ui/assets/inter-OFL.txt',
             'DISTRIBUTION-PERMISSIONS.txt': ROOT / 'docs/nexus/DISTRIBUTION-PERMISSIONS.txt',
             'supported-game-builds.json': ROOT / 'ui/supported-game-builds.json'}
    runtime = [prefix + 'EpochPact.exe']
    if kind == 'Manual':
        files['CopyToGame/version.dll'] = ROOT / 'native/build/version.dll'
        files['CopyToGame/EpochPact/EpochPact.Core.dll'] = core
        runtime += ['CopyToGame/version.dll', 'CopyToGame/EpochPact/EpochPact.Core.dll']
    else:
        files['README-APP-SETUP.md'] = ROOT / 'docs/nexus/README-APP-SETUP.md'
    payload = {name: file.read_bytes() for name, file in files.items()}
    # Explicitly validate the embedded runtime too; manual and app paths must agree.
    from PyInstaller.archive.readers import CArchiveReader
    embedded = CArchiveReader(str(exe))
    entries = {name.replace('\\', '/'): name for name in embedded.toc}
    for local, key in [(core, 'native/build/player/EpochPact.Core.dll'),
                       (ROOT / 'native/build/version.dll', 'native/build/version.dll'),
                       (ROOT / 'ui/supported-game-builds.json', 'ui/supported-game-builds.json')]:
        if embedded.extract(entries[key]) != local.read_bytes():
            raise ValueError('Embedded runtime mismatch: ' + key)
    catalog = json.loads(embedded.extract(entries['ui/catalog.json']))
    if catalog['liveState'] or catalog['currentValues']:
        raise ValueError('Private character snapshot in EXE')
    if any(name.startswith(('research/', 'docs/', 'live/', 'native/build/research/', 'native/build/test/')) for name in entries):
        raise ValueError('Developer resources in EXE')
    manifest = dict(release, kind=kind, runtime=runtime,
                    files=[{'path': n, 'bytes': len(d), 'sha256': sha(d)} for n, d in sorted(payload.items())])
    payload['runtime-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    payload['SHA256SUMS.txt'] = ''.join(f'{sha(d)}  {n}\n' for n, d in sorted(payload.items())).encode()
    output.mkdir(parents=True, exist_ok=True)
    path = output / f'EpochPact-{version}-{kind}.zip'
    # Never replace an uploaded artifact; preserve it for scan investigations.
    with zipfile.ZipFile(path, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026, 10, 10, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    result = verify_archive(path)
    with zipfile.ZipFile(path) as archive:
        if any(archive.read(n) != d for n, d in payload.items()):
            raise ValueError('ZIP bytes differ from staged payload')
    (output / (path.name + '.sha256')).write_text(result['sha256'] + '  ' + path.name + '\n')
    (output / (path.name + '.verification.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/nexus')
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--expected-sha256')
    args = parser.parse_args()
    if args.verify:
        result = verify_archive(args.verify)
        if args.expected_sha256 and result['sha256'] != args.expected_sha256.lower():
            raise ValueError('Downloaded ZIP differs from uploaded artifact')
        print(json.dumps(result, indent=2))
    else:
        print(json.dumps([package(x, args.output) for x in ('Manual', 'App-Setup')], indent=2))


if __name__ == '__main__':
    main()
