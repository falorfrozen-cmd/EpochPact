"""Read-only native page/collection checks and game-rendered screenshots in an isolated clone.

Uses reportnow / Unity ScreenCapture, never desktop capture, keyboard or mouse automation.
Always close/restore the owning ingame_live_session afterwards, including on failure.
"""
import argparse
import json
from pathlib import Path
import shutil
import time
from tools import le_session as le, collection_backend
from tools.ingame_capture import capture as save_capture

def run(folder):
    state = json.loads((folder / "session.json").read_text())
    process = state["process"]
    if state["phase"] != "running" or le.game_pids() != [state["pid"]] or le.process_identity(state["pid"]) != process:
        raise RuntimeError("This is not the running isolated test process.")
    def request(command):
        reply = json.loads(le.send(command, timeout=8, expected_process=process))
        if not reply.get("ok"): raise RuntimeError(f"{command}: {reply.get('error')}")
        return reply
    identity = request("identityread")
    if not identity.get("offline") or identity.get("player", {}).get("name") != "EpFeedbackTest" or str(identity["player"].get("id")) != state["cloneId"]:
        raise RuntimeError("Unexpected loaded character. No page actions were sent.")
    def ready(page, collection=False):
        until = time.monotonic()+120
        while time.monotonic()<until:
            value = request("panel read")
            if value.get("faults") or value.get("broken"): raise RuntimeError(value.get("error") or "Native UI fault")
            complete = not any(s.get("pendingWidgets") for s in value["sectionList"] if s["page"] == page)
            snapshot = value.get("collections", {}).get(page, {})
            if complete and (not collection or snapshot.get("snapshot") and not snapshot.get("busy")): return value
            if collection and not snapshot.get("busy") and not snapshot.get("snapshot"): raise RuntimeError("Collection scan failed without publishing a snapshot.")
            time.sleep(.2)
        raise RuntimeError("Page population did not finish.")
    reports=[]
    def capture(name):
        destination=folder/(name+".png")
        source=save_capture(request,state['game'],destination);report=source.parent
        reports.append({"name":name,"source":str(report),"image":str(destination)})
    request("panel 1")
    results={"inputAutomation":False,"gameWrites":False,"pages":[],"reports":reports}
    for page in ("stats","monolith","atlas","stash"):
        request("panel page:"+page)
        if page in ("atlas","stash"): request("panel collection:"+page)
        ui=ready(page,page in ("atlas","stash"))
        measure=request("panel measure")
        if measure.get("drift"): raise RuntimeError(f"Text measurement drift on {page}")
        results["pages"].append({"page":page,"faults":ui["faults"],"collection":ui.get("collections",{}).get(page),"measure":measure})
        capture("runtime-"+page)
        if page in ("atlas","stash"):
            with le.bound_process(process): data=collection_backend.atlas() if page=="atlas" else collection_backend.stash()
            if str(data["player"]["id"])!=state["cloneId"]: raise RuntimeError("Reference scan changed character.")
            native=ui['collections'][page]
            if native.get('total')!=data.get('total') or native.get('pages')!=data.get('pages') or str(native['player']['id'])!=str(data['player']['id']):
                raise RuntimeError('Native and desktop live collection summaries differ')
            (folder/(page+"-desktop-data.json")).write_text(json.dumps(data,ensure_ascii=False),encoding="utf-8")
    request("panel stat:sheet:0")
    time.sleep(.5)
    capture("runtime-stat-dexterity")
    (folder/"page-checks.json").write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps({"pages":len(results["pages"]),"screenshots":len(reports),"faults":0,"inputAutomation":False}),flush=True)

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("folder",type=Path)
    run(parser.parse_args().folder.resolve())
