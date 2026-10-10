"""Fetch pinned official PyPI inputs, verify their hashes and audit installed files.

Does not install or execute downloaded code. Run in the existing build environment
on Windows x64 / Python 3.13; packaging selects compatible wheel tags. A matching
PyPI hash proves artifact identity, not that a dependency is free of vulnerabilities.
"""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import importlib.metadata
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tarfile
from urllib.parse import urlparse
from urllib.request import urlopen
import zipfile
from packaging.tags import sys_tags
from packaging.utils import parse_wheel_filename

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def fetch(url):
    if urlparse(url).scheme != 'https' or urlparse(url).hostname not in ('pypi.org', 'files.pythonhosted.org'):
        raise ValueError('Only official PyPI HTTPS sources are accepted')
    with urlopen(url, timeout=30) as response:
        if urlparse(response.url).hostname not in ('pypi.org', 'files.pythonhosted.org'):
            raise ValueError('Unexpected artifact redirect')
        return response.read()


def audit(name, version, output, tags):
    metadata_url = f'https://pypi.org/pypi/{name}/{version}/json'
    metadata = json.loads(fetch(metadata_url))
    choices = []
    for item in metadata['urls']:
        if item['filename'].endswith('.whl'):
            wheel_tags = parse_wheel_filename(item['filename'])[3]
            ranks = [tags[t] for t in wheel_tags if t in tags]
            if ranks:
                choices.append((min(ranks), item))
    if choices:
        item = min(choices, key=lambda x: (x[0], x[1]['filename']))[1]
    else:
        # This is intentionally limited to the one audited source-only dependency.
        source = [x for x in metadata['urls'] if x['packagetype'] == 'sdist']
        if name != 'proxy_tools' or len(source) != 1:
            raise ValueError(f'No compatible official wheel: {name} {version}')
        item = source[0]
    data = fetch(item['url'])
    digest = sha(data)
    if digest != item['digests']['sha256']:
        raise ValueError('PyPI artifact digest mismatch: ' + item['filename'])
    path = output / item['filename']
    if path.exists() and path.read_bytes() != data:
        raise ValueError('Refusing to replace a different existing artifact')
    path.write_bytes(data)
    compared, mismatches = 0, []
    try:
        installed = importlib.metadata.distribution(name)
    except importlib.metadata.PackageNotFoundError:
        installed = None
    if installed is not None and installed.version == version:
        if path.suffix == '.whl':
            with zipfile.ZipFile(path) as archive:
                for member in archive.namelist():
                    parts = PurePosixPath(member).parts
                    if not parts or '..' in parts or member.startswith('/') or any(
                            p.endswith(('.dist-info', '.data')) for p in parts):
                        continue
                    if member.endswith('/'):
                        continue
                    local = installed.locate_file(member)
                    compared += 1
                    if not local.is_file() or local.read_bytes() != archive.read(member):
                        mismatches.append(member)
        else:
            with tarfile.open(path) as archive:
                for member in archive.getmembers():
                    parts = PurePosixPath(member.name).parts
                    if not member.isfile() or len(parts) < 3 or parts[1] != 'proxy_tools' or '..' in parts:
                        continue
                    relative = '/'.join(parts[1:])
                    local = installed.locate_file(relative)
                    compared += 1
                    if not local.is_file() or local.read_bytes() != archive.extractfile(member).read():
                        mismatches.append(relative)
    return dict(name=name, version=version, filename=item['filename'], bytes=len(data), sha256=digest,
                source=item['url'], metadata=metadata_url, comparedInstalledFiles=compared,
                installedFileMismatches=mismatches, installed=installed is not None,
                installedVersion=installed.version if installed else None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/security-review/inputs')
    parser.add_argument('--lock', type=Path, default=ROOT / 'requirements-build-win64.lock')
    parser.add_argument('--report', type=Path, default=ROOT / 'docs/security/build-inputs-20261010.json')
    args = parser.parse_args()
    if sys.platform != 'win32' or sys.version_info[:2] != (3, 13) or sys.maxsize <= 2**32:
        raise ValueError('This audit lock targets Python 3.13 on Windows x64 only')
    pins = []
    for line in (ROOT / 'requirements-build-win64.pins').read_text().splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        match = re.fullmatch(r'([\w.-]+)==([\w.-]+)', line)
        if not match:
            raise ValueError('Invalid pin: ' + line)
        pins.append(match.groups())
    args.output.mkdir(parents=True, exist_ok=True)
    tags = {tag: i for i, tag in enumerate(sys_tags())}
    with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
        results = list(pool.map(lambda pair: audit(*pair, args.output, tags), pins))
    args.report.parent.mkdir(parents=True, exist_ok=True)
    report = dict(scope='Official artifact identity and comparison with installed package files; not a malware verdict.',
                  python=sys.version.split()[0], platform='Windows x64', packages=results)
    args.report.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if any(x['installedFileMismatches'] for x in results):
        raise ValueError('Installed dependency differs from official artifact; inspect report before building')
    args.lock.write_text('# Official PyPI artifact hashes; Python 3.13 / Windows x64.\n' + ''.join(
        f"{x['name']}=={x['version']} --hash=sha256:{x['sha256']}\n" for x in results), encoding='utf-8')
    print(json.dumps({'packages': len(results), 'comparedInstalledFiles': sum(x['comparedInstalledFiles'] for x in results),
                      'mismatches': 0, 'report': str(args.report)}, indent=2))


if __name__ == '__main__':
    main()
