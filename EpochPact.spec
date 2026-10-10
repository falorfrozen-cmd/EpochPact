# Build via tools/build_player.py; only player resources enter this bundle.
from pathlib import Path
import os
from PyInstaller.utils.hooks import collect_data_files

root = Path(SPECPATH)
stage = Path(os.environ.get('EPOCHPACT_BUILD_STAGE', str(root / 'build/player-resources')))
onedir = os.environ.get('EPOCHPACT_FREEZE_LAYOUT', 'onefile') == 'onedir'
# Windows VERSIONINFO written by tools/build_player.py (publisher, product, version).
version_file = os.environ.get('EPOCHPACT_VERSION_FILE') or None
ui_files = ('index.html', 'app.js', 'style.css', 'stat-model.js', 'session.js',
            'collection-ui.js', 'collection.css', 'launcher.js', 'locale-en.json', 'catalog.json', 'supported-game-builds.json')
assets = ('chronoforge-art.png', 'chronoforge-reference.png', 'void-atlas-art.png',
          'void-atlas-reference.png', 'inter.ttf', 'inter-OFL.txt', 'monster-density.svg')
datas = [(str(stage / 'ui' / name), 'ui') for name in ui_files]
datas += [(str(stage / 'ui/assets' / name), 'ui/assets') for name in assets]
datas += [(str(stage / 'native/build/version.dll'), 'native/build'),
          (str(stage / 'native/build/player/EpochPact.Core.dll'), 'native/build/player'),
          (str(stage / 'native/build/player/build-info.json'), 'native/build/player'),
          (str(stage / 'THIRD-PARTY-NOTICES.txt'), '.')]
datas += collect_data_files('webview', excludes=['lib/pywebview-android.jar', 'lib/WebBrowserInterop.x86.dll'])
a = Analysis(
    [str(root / 'epochpact_desktop.py')], pathex=[str(root)],
    binaries=[], datas=datas,
    hiddenimports=['webview.platforms.winforms', 'webview.platforms.edgechromium', 'clr', 'pythonnet', 'clr_loader'],
    excludes=['PyQt5', 'PyQt6', 'PySide2', 'PySide6', 'qtpy', 'cefpython3', 'tkinter',
              'webview.platforms.qt', 'webview.platforms.gtk', 'webview.platforms.cef',
              'webview.platforms.android', 'webview.platforms.cocoa',
              'setuptools', '_distutils_hack', 'distutils', 'pkg_resources', 'wheel', 'pip', 'capstone',
              'cffi.setuptools_ext', 'cffi.recompiler', 'cffi.ffiplatform',
              'cffi.vengine_cpy', 'cffi.vengine_gen', 'cffi._shimmed_dist_utils'],
    # CFFI uses its prebuilt backend / dlopen here; runtime compilation is unused.
    # Build tools have no place in the distributed player application.
    noarchive=onedir,
)
# The upstream webview hook also collects lib/ without honoring our data
# exclusions. Remove only Android and unused 32-bit MSHTML assets here; keep
# WebView2's runtime directories because upstream code enumerates all of them.
unused_assets = {'webview/lib/pywebview-android.jar', 'webview/lib/WebBrowserInterop.x86.dll'}
a.datas = [item for item in a.datas if item[0].replace('\\', '/') not in unused_assets]
a.binaries = [item for item in a.binaries if item[0].replace('\\', '/') not in unused_assets]
pyz = PYZ(a.pure)
if onedir:
    exe = EXE(pyz, a.scripts, [], exclude_binaries=True, name='EpochPact',
              debug=False, strip=False, upx=False, console=False,
              disable_windowed_traceback=False, uac_admin=False, version=version_file)
    bundle = COLLECT(exe, a.binaries, a.datas, strip=False, upx=False, name='EpochPact')
else:
    exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='EpochPact',
              debug=False, strip=False, upx=False, console=False,
              disable_windowed_traceback=False, uac_admin=False, version=version_file)
