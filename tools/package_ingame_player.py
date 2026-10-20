"""Allowlisted single-DLL in-game review package; never uploads or installs anything."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import re
import zipfile

from tools.package_nexus_release import ROOT, sha, verify_archive
from tools.melon_compat import _version_strings


def verified_loader(build: Path):
    player = build.resolve(strict=True) / 'player'
    info = json.loads((player / 'build-info.json').read_text(encoding='utf-8'))
    loader, core = player / 'version.dll', player / 'EpochPact.Core.dll'
    if info.get('flavor') != 'player':
        raise ValueError('Only the player flavor may be packaged; no renderer/test fixtures.')
    if sha(loader.read_bytes()) != info.get('loaderSha256') or sha(core.read_bytes()) != info.get('sha256'):
        raise ValueError('Player build hashes do not match; rebuild before packaging.')
    version = _version_strings(loader).get('FileVersion', '')
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', version):
        raise ValueError('No valid version resource in player DLL.')
    return loader, version


def package(build: Path, output: Path):
    loader, version = verified_loader(build)
    notice = ('EpochPact native in-game interface\n\n'
              'Inter fonts — SIL Open Font License 1.1; see INTER-OFL.txt.\n'
              'miniz 3.1.2 — MIT License; statically linked for loot-filter ZIP files.\n'
              'https://github.com/richgel999/miniz/tree/3.1.2\n\n')
    notice += (ROOT / 'native/third_party/miniz/LICENSE').read_text(encoding='utf-8')
    files = {'version.dll': loader,
             'README.md': ROOT / 'docs/ingame-player-readme.md',
             'INTER-OFL.txt': ROOT / 'ui/assets/inter-OFL.txt',
             'DISTRIBUTION-PERMISSIONS.txt': ROOT / 'docs/nexus/DISTRIBUTION-PERMISSIONS.txt',
             'supported-game-builds.json': ROOT / 'ui/supported-game-builds.json'}
    payload = {name: path.read_bytes() for name, path in files.items()}
    payload['README.md'] = payload['README.md'].replace(b'{{VERSION}}', version.encode('ascii'))
    payload['THIRD-PARTY-NOTICES.txt'] = notice.encode('utf-8')
    manifest = dict(version=version, kind='In-game UI review', runtime=['version.dll'],
                    files=[dict(path=n, bytes=len(d), sha256=sha(d)) for n, d in sorted(payload.items())])
    payload['runtime-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    payload['SHA256SUMS.txt'] = ''.join(f'{sha(d)}  {n}\n' for n, d in sorted(payload.items())).encode()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'EpochPact-{version}-InGame.zip'
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026, 10, 20, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    result = verify_archive(destination)
    with zipfile.ZipFile(destination) as archive:
        if any(archive.read(n) != d for n, d in payload.items()):
            raise ValueError('Archived bytes differ from the verified payload.')
    (output / (destination.name + '.sha256')).write_text(result['sha256'] + '  ' + destination.name + '\n')
    (output / (destination.name + '.verification.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(package(args.build, args.output), indent=2))
