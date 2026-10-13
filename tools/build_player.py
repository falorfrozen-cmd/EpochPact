"""Build a Python-independent Windows player EXE from verified player artifacts."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def version_info(version):
    """PyInstaller VSVersionInfo text: the same publisher fields as native/version_info.h."""
    numbers = [int(n) for n in re.findall(r'\d+', version)][:4]
    numbers += [0] * (4 - len(numbers))
    fields = {'CompanyName': 'Falor', 'FileDescription': 'EpochPact app for Last Epoch (offline mod setup and controls)',
              'FileVersion': version, 'InternalName': 'EpochPact', 'OriginalFilename': 'EpochPact.exe',
              'ProductName': 'EpochPact', 'ProductVersion': version, 'LegalCopyright': 'Copyright (C) 2026 Falor',
              'Comments': 'Unofficial offline mod for Last Epoch. Source: https://github.com/falorfrozen-cmd/EpochPact'}
    strings = ',\n'.join(f'          StringStruct({k!r}, {v!r})' for k, v in fields.items())
    return f'''VSVersionInfo(
  ffi=FixedFileInfo(filevers={tuple(numbers)}, prodvers={tuple(numbers)}, mask=0x3f, flags=0x2,
                    OS=0x40004, fileType=0x1, subtype=0x0, date=(0, 0)),
  kids=[
    StringFileInfo([StringTable('040904B0', [
{strings}])]),
    VarFileInfo([VarStruct('Translation', [0x0409, 1200])])
  ]
)
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--layout', choices=('onefile', 'onedir'), default='onefile')
    parser.add_argument('--output', type=Path, default=ROOT / 'dist')
    parser.add_argument('--work', type=Path, default=ROOT / 'build/pyinstaller')
    parser.add_argument('--stage', type=Path, default=ROOT / 'build/player-resources')
    parser.add_argument('--loader', type=Path)
    parser.add_argument('--native-build', type=Path, help='Verified isolated native build root.')
    parser.add_argument('--version', help='Explicit candidate version; leaves published release metadata unchanged.')
    args = parser.parse_args()
    release = json.loads((ROOT / 'docs/nexus/release.json').read_text(encoding='utf-8'))
    version = args.version or release['version']
    if not re.fullmatch(r'[a-zA-Z0-9.-]+', version):
        raise ValueError('Invalid version')
    output = args.output.resolve()
    # Review builds are immutable evidence and never replace an uploaded build.
    if args.layout == 'onedir' and (output / 'EpochPact').exists():
        raise ValueError('Choose an empty output folder; this bundle already exists.')
    from tools import le_session as le
    if args.native_build:
        le.BUILD = args.native_build.resolve(strict=True)
    core, digest = le.build_artifact('player')
    loader = args.loader.resolve(strict=True) if args.loader else le.BUILD / 'version.dll'
    if not le.is_ours(loader):
        raise ValueError('Missing or foreign loader. Build native/build.bat player first.')
    stage = args.stage.resolve()
    (stage / 'ui/assets').mkdir(parents=True, exist_ok=True)
    for name in ('index.html', 'app.js', 'style.css', 'stat-model.js', 'session.js',
                 'collection-ui.js', 'collection.css', 'launcher.js', 'preferences.js', 'crafting-model.js', 'affix-picker.js', 'loot-log.js', 'feed.js', 'feed.html', 'damage-meter.js', 'release-notes.js', 'storage-sync.js', 'i18n.js', 'i18n-tr.json', 'i18n-ru.json', 'damage.js', 'damage.html', 'locale-en.json', 'supported-game-builds.json'):
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
    bridge = le.BUILD / 'melon/EpochPact.MelonBridge.dll'
    from tools import melon_compat
    if not melon_compat.owned_bridge(bridge):
        raise ValueError('Build the MelonLoader bridge before packaging this version.')
    (stage / 'native/build/melon').mkdir(parents=True, exist_ok=True)
    shutil.copy2(bridge, stage / 'native/build/melon' / bridge.name)
    notices = ['EpochPact third-party notices\nPython runtime: https://docs.python.org/3/license.html\n']
    notices.append('\nOptional MelonLoader integration: https://github.com/LavaGang/MelonLoader\n'
                   'MelonLoader is a separate third-party loader; its SDK/runtime is not included.\n'
                   'EpochPact.MelonBridge is built against the official MelonLoader 0.7.3 API.\n')
    notices.append('Optional bridge also references the loader-provided AsmResolver libraries (MIT).\n'
                   'https://github.com/Washi1337/AsmResolver/blob/master/LICENSE.md\n'
                   'These SDK libraries are not redistributed with EpochPact.\n')
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
    version_file = stage / 'version-info.txt'
    version_file.write_text(version_info(version), encoding='utf-8')
    environment = os.environ.copy()
    environment.update(EPOCHPACT_BUILD_STAGE=str(stage), EPOCHPACT_FREEZE_LAYOUT=args.layout,
                       EPOCHPACT_VERSION_FILE=str(version_file))
    subprocess.run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
                    '--distpath', str(output), '--workpath', str(args.work.resolve()),
                    str(ROOT / 'EpochPact.spec')], cwd=ROOT, env=environment, check=True)
    bundle = output / 'EpochPact' if args.layout == 'onedir' else output
    exe = bundle / 'EpochPact.exe'
    # CLR reads the host EXE's adjacent configuration before Python.NET can
    # load. Putting this in _internal or inside a onefile EXE does not work.
    from tools.desktop_config import NAME, validate
    (bundle / NAME).write_bytes(validate(ROOT / NAME))
    info = {'file': exe.name, 'bytes': exe.stat().st_size, 'version': version, 'layout': args.layout,
            'sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'nativePlayerSHA256': digest,
            'loaderSHA256': hashlib.sha256(loader.read_bytes()).hexdigest(),
            'python': sys.version, 'gameVersion': catalog.get('gameVersion'),
            'containsResearchBuild': False, 'containsHistoricalSnapshots': False}
    if args.layout == 'onedir':
        shutil.copy2(ROOT / 'docs/player-quickstart.txt', bundle / 'START-HERE.txt')
        shutil.copy2(stage / 'THIRD-PARTY-NOTICES.txt', bundle / 'THIRD-PARTY-NOTICES.txt')
        info['files'] = [{'path': file.relative_to(bundle).as_posix(), 'bytes': file.stat().st_size,
                          'sha256': hashlib.sha256(file.read_bytes()).hexdigest()}
                         for file in sorted(bundle.rglob('*')) if file.is_file()]
    (bundle / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n', encoding='utf-8')
    # The onedir review package has its own README explaining the different layout.
    if args.layout == 'onefile':
        shutil.copy2(ROOT / 'docs/player-quickstart.txt', output / 'START-HERE.txt')
        shutil.copy2(stage / 'THIRD-PARTY-NOTICES.txt', output / 'THIRD-PARTY-NOTICES.txt')
    print(json.dumps({k: v for k, v in info.items() if k != 'files'} | {
        'bundleFiles': len(info.get('files', []))}, indent=2))


if __name__ == '__main__':
    main()
