"""Keep player/research/test core artifacts separate, with verified SHA256."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def stamp(flavor, build_dir=None):
    if flavor not in ('player', 'research', 'test'):
        raise ValueError('Unknown build flavor.')
    build = Path(build_dir).resolve() if build_dir else Path(__file__).resolve().parents[1] / 'native/build'
    source = build / 'EpochPact.Core.dll'
    target = build / flavor / source.name
    target.parent.mkdir(exist_ok=True)
    shutil.copy2(source, target)
    metadata = {'flavor': flavor, 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    (target.parent / 'build-info.json').write_text(json.dumps(metadata) + '\n', encoding='utf-8')
    print(f'Build artifact: {flavor} | SHA256 {metadata["sha256"]} | {target}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('flavor', choices=('player', 'research', 'test'))
    parser.add_argument('--build-dir', type=Path)
    args = parser.parse_args()
    stamp(args.flavor, args.build_dir)
