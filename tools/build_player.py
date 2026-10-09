"""Build a Python-independent Windows player EXE from verified player artifacts."""
from pathlib import Path
import hashlib
import importlib.metadata
import json
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    from tools import le_session as le
    core, digest = le.build_artifact('player')
    loader = le.BUILD / 'version.dll'
    if not le.is_ours(loader):
        raise ValueError('Missing or foreign loader. Build native/build.bat player first.')
    stage = ROOT / 'build/player-resources'
    (stage / 'ui/assets').mkdir(parents=True, exist_ok=True)
    for name in ('index.html', 'app.js', 'style.css', 'stat-model.js', 'session.js',
                 'collection-ui.js', 'collection.css', 'launcher.js', 'locale-en.json', 'supported-game-builds.json'):
        shutil.copy2(ROOT / 'ui' / name, stage / 'ui' / name)
    for name in ('chronoforge-art.png', 'chronoforge-reference.png', 'void-atlas-art.png',
                 'void-atlas-reference.png', 'inter.ttf', 'inter-OFL.txt', 'monster-density.svg'):
        shutil.copy2(ROOT / 'ui/assets' / name, stage / 'ui/assets' / name)
    catalog = json.loads((ROOT / 'ui/catalog.json').read_text(encoding='utf-8'))
    # Developer snapshots contain player identities, local paths and old live
    # values. Runtime metadata and command contracts are the distributable data.
    catalog['liveState'] = {}
    catalog['currentValues'] = {}
    (stage / 'ui/catalog.json').write_text(json.dumps(catalog, ensure_ascii=False), encoding='utf-8')
    (stage / 'native/build/player').mkdir(parents=True, exist_ok=True)
    shutil.copy2(loader, stage / 'native/build/version.dll')
    shutil.copy2(core, stage / 'native/build/player/EpochPact.Core.dll')
    shutil.copy2(core.parent / 'build-info.json', stage / 'native/build/player/build-info.json')
    notices = ['EpochPact third-party notices\nPython runtime: https://docs.python.org/3/license.html\n']
    python_license = Path(sys.base_prefix) / 'LICENSE.txt'
    notices.append(python_license.read_text(encoding='utf-8'))
    for name in ('Flask', 'Werkzeug', 'Jinja2', 'MarkupSafe', 'click', 'itsdangerous', 'blinker',
                 'pywebview', 'pythonnet', 'clr_loader', 'cffi', 'pycparser', 'proxy_tools', 'bottle', 'typing_extensions'):
        dist = importlib.metadata.distribution(name)
        notices.append(f'\n{name} {dist.version}\n')
        for file in dist.files or []:
            if any(part.upper().startswith(('LICENSE', 'COPYING')) for part in file.parts):
                path = dist.locate_file(file)
                if path.is_file():
                    notices.append(path.read_text(encoding='utf-8', errors='replace'))
    (stage / 'THIRD-PARTY-NOTICES.txt').write_text('\n'.join(notices), encoding='utf-8')
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
                    '--distpath', str(ROOT / 'dist'), '--workpath', str(ROOT / 'build/pyinstaller'),
                    str(ROOT / 'EpochPact.spec')], cwd=ROOT, check=True)
    exe = ROOT / 'dist/EpochPact.exe'
    release = json.loads((ROOT / 'docs/nexus/release.json').read_text(encoding='utf-8'))
    info = {'file': exe.name, 'bytes': exe.stat().st_size, 'version': release['version'],
            'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'nativePlayerSHA256': digest,
            'python': sys.version, 'gameVersion': catalog.get('gameVersion'),
            'containsResearchBuild': False, 'containsHistoricalSnapshots': False}
    (ROOT / 'dist/build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    shutil.copy2(ROOT / 'docs/player-quickstart.txt', ROOT / 'dist/START-HERE.txt')
    shutil.copy2(stage / 'THIRD-PARTY-NOTICES.txt', ROOT / 'dist/THIRD-PARTY-NOTICES.txt')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
