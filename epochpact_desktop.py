"""Standalone player entry point, with a narrowly scoped administrator installer."""
from __future__ import annotations

import argparse
import ctypes
import json
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
import re
import sys

from tools.app_paths import user_root, validate_installer_result


def installer():
    from tools.player_setup import validate_executable
    from tools import le_session as le
    parser = argparse.ArgumentParser()
    parser.add_argument('--install-plugin', action='store_true')
    parser.add_argument('--game-exe', required=True)
    parser.add_argument('--install-result', required=True)
    args = parser.parse_args()
    output = validate_installer_result(args.install_result)
    try:
        le.GAME = validate_executable(args.game_exe).parent
        import contextlib
        import io
        text = io.StringIO()
        with contextlib.redirect_stdout(text):
            code = le.cmd_install(argparse.Namespace(flavor='player'))
        result = {'ok': code == 0, 'error': text.getvalue().strip() if code else None}
    except Exception as exc:
        result = {'ok': False, 'error': str(exc)}
    # Recheck after installation; exclusive creation refuses replacement of a
    # pre-existing file, including one created while the helper was running.
    validate_installer_result(args.install_result)
    with output.open('x', encoding='utf-8') as stream:
        stream.write(json.dumps(result) + '\n')
    return 0 if result['ok'] else 1


def main():
    if '--install-plugin' in sys.argv:
        return installer()
    user_root().mkdir(parents=True, exist_ok=True)
    handler = RotatingFileHandler(user_root() / 'startup.log', maxBytes=1024*1024, backupCount=2, encoding='utf-8')
    logging.basicConfig(handlers=[handler], level=logging.INFO, format='%(asctime)s %(levelname)s %(message)s')
    try:
        from epochpact_ui import main as ui_main
        ui_main()
        return 0
    except Exception:
        logging.exception('EpochPact could not start')
        if '--no-open' not in sys.argv:
            message = 'EpochPact could not open. Check startup.log in your LocalAppData/EpochPact folder.\n\nIf WebView2 is missing, install Microsoft Edge WebView2 Runtime, then reopen EpochPact.'
            ctypes.windll.user32.MessageBoxW(None, message, 'EpochPact', 0x10)
        return 1
    finally:
        handler.close()


if __name__ == '__main__':
    sys.exit(main())
