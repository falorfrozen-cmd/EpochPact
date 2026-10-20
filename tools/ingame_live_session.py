"""Prepare/restore an isolated in-game UI test, without mouse/keyboard automation.

Uses the existing monolith test clone/restore safety checks. The process stays open for manual
or Computer Use checks; finish closes only the process started by this manifest and restores
the exact runtime/save bytes. Never start while the owner's game is running.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import shutil
import time
from pathlib import Path
from tools import le_session as le
from tools import monolith_live_check as saves

PROCESS = None


class UnsafeTestState(RuntimeError):
    pass

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def write(path, data):
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def wait(fn, seconds, message):
    deadline = time.monotonic() + seconds
    last = None
    while time.monotonic() < deadline:
        try:
            value = fn()
            if value:
                return value
        except UnsafeTestState:
            raise
        except Exception as exc:
            last = str(exc)
        time.sleep(1)
    raise RuntimeError(f"{message}: {last}")


def reply(command):
    text = le.send(command, timeout=8, expected_process=PROCESS)
    if not text:
        raise RuntimeError(f"No reply: {command}")
    return json.loads(text)


def start(folder, loader):
    global PROCESS
    if le.game_pids():
        raise RuntimeError("Last Epoch is already running; the owner's process is not a test session.")
    path = folder / "session.json"
    if path.exists() and json.loads(path.read_text())["phase"] != "restored":
        raise RuntimeError("Restore the existing test session first.")
    folder.mkdir(parents=True, exist_ok=True)
    loader = loader.resolve(strict=True)
    game = le.GAME.resolve(strict=True)
    backup = folder / "runtime-before"
    backup.mkdir(exist_ok=True)
    real = Path(os.environ["LOCALAPPDATA"]) / "EpochPact/preferences.json"
    records = []
    for relative in ("version.dll", "EpochPact/EpochPact.Core.dll", "EpochPact/installed-build.json"):
        target = game / relative
        if target.is_symlink():
            raise RuntimeError("Runtime links are not supported by this test helper.")
        record = {"relative": relative, "sha256": digest(target)}
        if record["sha256"]:
            dest = backup / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(target, dest)
        records.append(record)
    state = {"phase": "backed-up", "game": str(game), "records": records, "realPreferencesHash": digest(real),
             "loaderHash": digest(loader), "pid": None}
    write(path, state)
    saves.MANIFEST = le.OUT / "ingame-chatgpt-test-manifest.json"
    manifest = saves.prepare("EpFeedbackTest")
    state.update(phase="prepared", cloneId=manifest["id"], cloneName="EpFeedbackTest")
    write(path, state)
    # The embedded player loader replaces the standalone core for this test only.
    core = game / "EpochPact/EpochPact.Core.dll"
    if core.exists():
        le._make_install_writable(core)
        core.unlink()
    target = game / "version.dll"
    le._make_install_writable(target)
    shutil.copyfile(loader, target)
    assert digest(target) == state["loaderHash"]
    prefs = folder / "preferences.json"
    if real.exists():
        shutil.copyfile(real, prefs)
    else:
        prefs.write_text('{"version":1,"values":{}}', encoding="utf-8")
    os.environ["EPOCHPACT_PREFERENCES"] = str(prefs)
    state["phase"] = "installed"
    write(path, state)
    if le.cmd_launch(argparse.Namespace(offline=True)):
        raise RuntimeError("Offline launch failed; use finish to restore the saved files.")
    pids = le.game_pids()
    if len(pids) != 1:
        raise RuntimeError("Expected one test process.")
    state["pid"] = pids[0]
    PROCESS = le.process_identity(pids[0])
    if not PROCESS:
        raise RuntimeError("Test process identity could not be verified.")
    state["process"] = PROCESS
    state["phase"] = "running"
    write(path, state)
    print(json.dumps({"phase": "running", "pid": state["pid"]}), flush=True)
    wait(lambda: reply("connectionping").get("ok"), 180, "Core startup")
    def character_select():
        current = reply("sessionread")
        if current.get("state") == "Login" and not current.get("transitioning"):
            le.send("playoffline", timeout=8, expected_process=PROCESS)
        return current.get("state") == "CharacterSelect" and not current.get("transitioning")
    wait(character_select, 240, "Character selection")
    le.send(f"loadname EpFeedbackTest {manifest['originalCharacter'].get('level') or 1}", timeout=90, expected_process=PROCESS)
    def loaded():
        identity = reply("identityread")
        player = identity.get("player") or {}
        if identity.get("offline") and player.get("name") != "EpFeedbackTest":
            raise UnsafeTestState("Unexpected loaded character; do not run UI tests.")
        return identity if identity.get("offline") and str(player.get("id")) == state["cloneId"] else None
    identity = wait(loaded, 300, "Isolated character")
    state["identity"] = identity
    write(path, state)
    panel = wait(lambda: (lambda value: value if value.get("built") and value.get("jobs") == 0 else None)(reply("panel read")), 180, "Background UI build")
    state["panelBuilt"] = {k: panel.get(k) for k in ("built", "page", "faults", "error", "timing")}
    write(path, state)
    print(json.dumps(state["panelBuilt"]), flush=True)


def finish(folder):
    path = folder / "session.json"
    state = json.loads(path.read_text())
    if state["phase"] == "restored":
        if not state.get("normalShutdownConfirmed", state.get("closeCode") == 0):
            raise RuntimeError("Files were restored, but normal game shutdown was not confirmed; see the session report.")
        print("Already restored.")
        return
    pids = le.game_pids()
    if pids and pids != [state["pid"]]:
        raise RuntimeError("Another game process is running; refusing to close or restore it.")
    if pids and le.process_identity(pids[0]) != state.get("process"):
        raise RuntimeError("The process was restarted; refusing to close or restore it.")
    if pids:
        result = le.cmd_close(argparse.Namespace(timeout=120))
        state["closeCode"] = result
        write(path, state)
        if le.game_pids():
            raise RuntimeError("Test game is still running; saves and DLLs were not restored.")
    game = le.GAME.resolve(strict=True)
    if str(game) != state["game"]:
        raise RuntimeError("Game path changed; refusing restore.")
    for record in state["records"]:
        if record["sha256"]:
            assert digest(folder / "runtime-before" / record["relative"]) == record["sha256"]
    saves.MANIFEST = le.OUT / "ingame-chatgpt-test-manifest.json"
    if state.get("cloneId"):
        manifest = json.loads(saves.MANIFEST.read_text())
        if manifest["id"] != state["cloneId"] or manifest["testName"] != "EpFeedbackTest":
            raise RuntimeError("Another save test owns the manifest; refusing restore.")
        state["savesRestored"] = saves.restore()
    for record in state["records"]:
        target = game / record["relative"]
        if target.is_symlink():
            raise RuntimeError("Runtime became a link; refusing restore.")
        if record["sha256"]:
            le._make_install_writable(target)
            shutil.copy2(folder / "runtime-before" / record["relative"], target)
        elif target.exists():
            le._make_install_writable(target)
            target.unlink()
        assert digest(target) == record["sha256"]
    real = Path(os.environ["LOCALAPPDATA"]) / "EpochPact/preferences.json"
    state["realPreferencesUnchanged"] = digest(real) == state["realPreferencesHash"]
    state["normalShutdownConfirmed"] = state.get("closeCode") == 0
    state["phase"] = "restored"
    write(path, state)
    assert state["realPreferencesUnchanged"], "Real preferences changed; preserve evidence."
    print(json.dumps({k: state.get(k) for k in ("phase", "closeCode", "normalShutdownConfirmed", "savesRestored", "realPreferencesUnchanged")}), flush=True)
    if not state["normalShutdownConfirmed"]:
        raise RuntimeError("All files were restored, but the game did not confirm normal shutdown. This live test failed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("start", "finish"))
    parser.add_argument("folder", type=Path)
    parser.add_argument("--loader", type=Path)
    args = parser.parse_args()
    if args.action == "start":
        if args.loader is None:
            parser.error("start requires --loader")
        start(args.folder.resolve(), args.loader)
    else:
        finish(args.folder.resolve())
