# Build via tools/build_player.py; only player resources enter this bundle.
from pathlib import Path
from PyInstaller.utils.hooks import collect_data_files

root = Path(SPECPATH)
stage = root / 'build/player-resources'
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
datas += collect_data_files('webview')
a = Analysis(
    [str(root / 'epochpact_desktop.py')], pathex=[str(root)],
    binaries=[], datas=datas,
    hiddenimports=['webview.platforms.winforms', 'webview.platforms.edgechromium', 'clr', 'pythonnet', 'clr_loader'],
    excludes=['PyQt5', 'PyQt6', 'PySide2', 'PySide6', 'qtpy', 'cefpython3', 'tkinter',
              'webview.platforms.qt', 'webview.platforms.gtk', 'webview.platforms.cef',
              'webview.platforms.android', 'webview.platforms.cocoa'],
    noarchive=False,
)
pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name='EpochPact',
          debug=False, strip=False, upx=False, console=False,
          disable_windowed_traceback=False, uac_admin=False)
