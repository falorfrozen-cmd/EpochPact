"""Prepare/restore EpCoFTest using the validated isolated-save workflow."""
import argparse
import json
try:
    from . import monolith_live_check as isolated
except ImportError:
    import monolith_live_check as isolated

isolated.MANIFEST = isolated.le_session.OUT / "cof-test-manifest.json"

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("prepare", "restore"))
    action = parser.parse_args().action
    result = isolated.prepare("EpCoFTest") if action == "prepare" else isolated.restore()
    print(json.dumps(result if action == "restore" else {k: result[k] for k in ("id", "backup", "testName")}, indent=2))
