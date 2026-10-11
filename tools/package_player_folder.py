"""Create a standard player ZIP from a hash-verified folder build.

No build, installation, upload, game binaries, developer files or private saves.
The existing artifact is never overwritten.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
import zipfile

from tools.package_nexus_release import ROOT, safe_archive_name, sha, verify_archive
from tools.package_review_bundle import read_bundle


def package(bundle: Path, output: Path):
    info, files = read_bundle(bundle)
    version = info['version']
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', version):
        raise ValueError('Invalid player version')
    resources = bundle / '_internal'
    payload = {'EpochPact/' + name: path.read_bytes() for name, path in files.items()}
    additions = {
        'CopyToGame/version.dll': resources / 'native/build/version.dll',
        'CopyToGame/EpochPact/EpochPact.Core.dll': resources / 'native/build/player/EpochPact.Core.dll',
        'README.md': ROOT / 'docs/player-folder-readme.md',
        'DISTRIBUTION-PERMISSIONS.txt': ROOT / 'docs/nexus/DISTRIBUTION-PERMISSIONS.txt',
        'THIRD-PARTY-NOTICES.txt': resources / 'THIRD-PARTY-NOTICES.txt',
        'INTER-OFL.txt': resources / 'ui/assets/inter-OFL.txt',
        'supported-game-builds.json': resources / 'ui/supported-game-builds.json',
    }
    payload.update({name: path.read_bytes() for name, path in additions.items()})
    payload['README.md'] = payload['README.md'].replace(b'{{VERSION}}', version.encode('ascii'))
    manifest = dict(version=version, game='Last Epoch', author='Falor', kind='Player',
                    github='https://github.com/falorfrozen-cmd/EpochPact',
                    discord='https://discord.gg/q6aexZZSAf',
                    runtime=[n for n in payload if n.lower().endswith(('.exe', '.dll', '.pyd', '.pyc'))],
                    files=[dict(path=n, bytes=len(d), sha256=sha(d)) for n, d in sorted(payload.items())])
    payload['runtime-manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode()
    payload['SHA256SUMS.txt'] = ''.join(f'{sha(d)}  {n}\n' for n, d in sorted(payload.items())).encode()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'EpochPact-{version}.zip'
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(payload.items()):
            entry = zipfile.ZipInfo(safe_archive_name(name), date_time=(2026, 10, 12, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(entry, data)
    result = verify_archive(destination)
    (output / (destination.name + '.sha256')).write_text(result['sha256'] + '  ' + destination.name + '\n')
    (output / (destination.name + '.verification.json')).write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = package(args.bundle, args.output)
    print(json.dumps({k: v for k, v in result.items() if k != 'runtime'} | {
        'runtimeFiles': len(result['runtime'])}, indent=2))
