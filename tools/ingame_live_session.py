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


def select_clone(name):
    """CharacterSelect state precedes tile loading: wait for the exact tile too."""
    offline_requested=False
    def selection():
        nonlocal offline_requested
        current=reply('sessionread')
        if current.get('state')=='Login' and not current.get('transitioning') and not offline_requested:
            response=le.send('playoffline',timeout=8,expected_process=PROCESS)
            offline_requested='OnPlayOfflineClicked called' in (response or '')
        return current.get('state')=='CharacterSelect' and not current.get('transitioning')
    wait(selection,240,'Character selection')
    def tile():
        response=le.send('characters',timeout=8,expected_process=PROCESS) or ''
        matches=[line for line in response.splitlines() if line.startswith(name+' level=')]
        if len(matches)>1:raise UnsafeTestState('Ambiguous disposable character tile.')
        return response if response.startswith('offline characters:') and len(matches)==1 else None
    wait(tile,180,'Disposable character tile loading')
    response=le.send('loadname '+name,timeout=90,expected_process=PROCESS) or ''
    if not response.startswith('loadname: normal tile double-click requested for '+name+' '):
        raise UnsafeTestState('Clone load was not acknowledged: '+response)


def start(folder, loader, *, hud_running=False, test_name="EpFeedbackTest", save_tests=False, hud_options=None):
    global PROCESS
    if test_name not in ("EpFeedbackTest", "EpCraftTest"):
        raise UnsafeTestState("Only the explicitly named disposable UI clones are supported.")
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
    for relative in ("version.dll", "EpochPact/EpochPact.Core.dll", "EpochPact/installed-build.json", "EpochPact/workshop-recipes.txt"):
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
    manifest = saves.prepare(test_name)
    state.update(phase="prepared", cloneId=manifest["id"], cloneName=test_name)
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
    data=json.loads(prefs.read_text(encoding='utf-8')); values=data.setdefault('values',{})
    # The owner's startup profile can enable travel/auto-pickup. Tests start neutral,
    # and never change the original preference file.
    values['epochpact.startup-profile.live'] = json.dumps('__off__')
    options=json.loads(values.get('epochpact.damage-window.v1','{}'))
    if hud_options:options.update(hud_options)
    options['running']=hud_running
    values['epochpact.damage-window.v1']=json.dumps(options)
    if hud_running:
        windows=json.loads(values.get('epochpact.windows.v1','{}'))
        for name in ('damage','feed'):
            entry=windows.setdefault(name,{}); entry['open']=True
        values['epochpact.windows.v1']=json.dumps(windows)
    prefs.write_text(json.dumps(data),encoding='utf-8')
    os.environ["EPOCHPACT_PREFERENCES"] = str(prefs)
    os.environ["EPOCHPACT_TEST_SAVE_ID"] = state["cloneId"]
    os.environ["EPOCHPACT_ALLOW_SAVE_TESTS"] = "1" if save_tests else "0"
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
    select_clone(test_name)
    def loaded():
        identity = reply("identityread")
        player = identity.get("player") or {}
        if identity.get("offline") and player.get("name") != test_name:
            raise UnsafeTestState("Unexpected loaded character; do not run UI tests.")
        return identity if identity.get("offline") and str(player.get("id")) == state["cloneId"] else None
    identity = wait(loaded, 300, "Isolated character")
    state["identity"] = identity
    write(path, state)
    panel = wait(lambda: (lambda value: value if value.get("built") and value.get("jobs") == 0 else None)(reply("panel read")), 180, "Background UI build")
    state["panelBuilt"] = {k: panel.get(k) for k in ("built", "page", "faults", "error", "timing")}
    write(path, state)
    print(json.dumps(state["panelBuilt"]), flush=True)


def restart(folder):
    """Reload the same disposable save from disk; keep the restoration anchor intact."""
    global PROCESS
    path=folder/'session.json'; state=json.loads(path.read_text())
    if state['phase']!='running' or le.game_pids()!=[state['pid']] or le.process_identity(state['pid'])!=state['process']:
        raise UnsafeTestState('Only the owned, unchanged test process can be restarted.')
    code=le.cmd_close(argparse.Namespace(timeout=120))
    state.setdefault('restartCloseCodes',[]).append(code); write(path,state)
    if le.game_pids() or code:
        raise UnsafeTestState('The test did not close normally; restore before any further test.')
    prefs=folder/'preferences.json'
    os.environ['EPOCHPACT_PREFERENCES']=str(prefs)
    os.environ['EPOCHPACT_TEST_SAVE_ID']=state['cloneId']
    if le.cmd_launch(argparse.Namespace(offline=True)):
        raise RuntimeError('Test restart launch failed.')
    pids=le.game_pids()
    if len(pids)!=1:raise UnsafeTestState('Expected one restarted test process.')
    state['pid']=pids[0]; PROCESS=le.process_identity(pids[0]);state['process']=PROCESS;write(path,state)
    if not PROCESS:raise UnsafeTestState('Cannot verify restarted test process.')
    wait(lambda:reply('connectionping').get('ok'),180,'Restart core startup')
    select_clone(state['cloneName'])
    def loaded():
        value=reply('identityread'); player=value.get('player') or {}
        if value.get('offline') and (player.get('name')!=state['cloneName'] or str(player.get('id'))!=state['cloneId']):
            raise UnsafeTestState('Unexpected character after restart.')
        return value if value.get('offline') else None
    state['identityAfterRestart']=wait(loaded,300,'Reload disposable character')
    wait(lambda:(lambda r:r if r.get('built') and not r.get('jobs') else None)(reply('panel read')),180,'Restart UI build')
    write(path,state)


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
        if manifest["id"] != state["cloneId"] or manifest["testName"] != state.get("cloneName", "EpFeedbackTest"):
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
