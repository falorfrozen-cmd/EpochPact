"""Explicit checks on an isolated EpCraftTest clone; never targets an owner save.

Prepare/restore use the existing fresh-backup clone harness. Verification seeds
two test uniques through normal stash APIs and requires a research build.
"""
import argparse
import json
import hashlib
from pathlib import Path
from . import monolith_live_check as isolated, progression_backend as ipc, collection_backend as collection

isolated.MANIFEST = isolated.le_session.OUT / "collection-test-manifest.json"


def verify():
    manifest=json.loads(isolated.MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("restored"):
        raise RuntimeError("Prepare an isolated clone first.")
    sid=manifest["id"]
    initial=collection.stash()
    if initial["player"]!={"id":sid,"name":"EpCraftTest"}:
        raise RuntimeError("The loaded character is not the isolated EpCraftTest clone.")
    transcript=[]
    checks=[]

    def command(text,expected=True):
        result=ipc.request(text)
        transcript.append(dict(command=text,response=result))
        if result.get("ok") is not expected:raise AssertionError(f"{text}: {result}")
        return result

    def check(condition,label):
        if not condition:raise AssertionError(label)
        checks.append(label)
        print("PASS",label,flush=True)

    for action in ("stashunique0","stashunique2"):
        command(f"crafttest {sid} {action}")
    stash=collection.stash()
    check(stash["total"]==initial["total"]+2,"real stash insertion accounted for")
    copies=[i for i in stash["items"] if i.get("uniqueId")==1]
    check({i["lp"] for i in copies if i["source"]=="Stash"}=={0,2},"LP0/LP2 read from real stash containers")
    atlas=collection.atlas()
    crown=next(i for i in atlas["items"] if i["id"]==1)
    check(crown["count"]==len(copies) and crown["bestLP"]==2,"atlas counts inventory + stash copies and best LP")
    check(atlas["total"]>400 and atlas["owned"]>=1,"runtime Unique/Set catalog and owned/missing")
    check(any(a["tier"]==7 for i in stash["items"] for a in i["affixes"]),"real T7 tier and roll read correctly")
    check(all(any(m["name"]=="Damage Dealt to Mana Before Health" and m["percent"] and 20<=m["value"]<=50 for m in i["uniqueMods"]) for i in copies),"unique modifiers use actual names, values and percentage conversion")
    command(f"crafttest {sid} setup")
    staged=collection.stash()
    check(any(i["source"]=="Forge" for i in staged["items"]),"items staged in Forge remain owned")
    command(f"crafttest {sid} clear")

    empty=command(f"echoread {sid} 2 normal")
    check(not empty["generated"] and empty["echoes"]==[],"reading an unopened web does not create it")
    mono=command("monolithread")
    run=next(t for t in mono["timelines"] if t["id"]==2)["difficulties"][0]["run"]
    check(run is None,"unopened timeline still has no run after read")
    echoes=command(f"echoread {sid} 1 normal")
    check(echoes["generated"] and len(echoes["echoes"])>=7,"existing Echo web yields actual rewards and coordinates")
    runnable=next(e for e in echoes["echoes"] if e["runnable"])
    before=collection.stash()["revision"]
    for _ in range(2):
        focus=command(f"echofocus {sid} 1 normal {runnable['index']}")
        check(focus.get("selectionVerified") and focus["focused"]==runnable["index"],"normal map selection readback verifies focus")
    command(f"echofocus 0 1 normal {runnable['index']}",False)
    command(f"echofocus {sid} 1 normal 999999",False)
    after=command(f"echoread {sid} 1 normal")
    check(after["echoes"]==echoes["echoes"],"focus neither starts nor completes Echoes")
    check(collection.stash()["revision"]==before,"navigator does not change items or their locations")
    report=dict(ok=True,gameVersion="1.5.2",player=stash["player"],catalogTypes=atlas["total"],
                equipment=stash["total"],ownedCrownCopies=crown["count"],bestLP=crown["bestLP"],
                echoes=len(echoes["echoes"]),pages=stash["pages"],maxPageMs=stash["maxPageMs"],checks=checks,transcript=transcript)
    target=Path(__file__).resolve().parents[1]/"research/live/collection-live-verification.json"
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf-8")
    return {k:v for k,v in report.items() if k!='transcript'}


def verify_player():
    """Read/focus the already seeded clone using the actual non-research DLL."""
    manifest=json.loads(isolated.MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("restored"):
        raise RuntimeError("Prepare and seed the isolated clone before player verification.")
    sid=manifest["id"]
    atlas=collection.atlas()
    if atlas["player"]!={"id":sid,"name":"EpCraftTest"}:
        raise RuntimeError("The loaded character is not the isolated EpCraftTest clone.")
    checks=[]
    def check(condition,label):
        if not condition:raise AssertionError(label)
        checks.append(label)
        print("PASS",label,flush=True)
    status=isolated.le_session.send("status")
    check(status is not None and status.startswith("EpochPact 0.1.0;") and "research:" not in status,
          "actual player build has no research trackers")
    fixture=isolated.le_session.send(f"crafttest {sid} stashunique0")
    check(fixture is not None and "unknown" in fixture.lower(),"research fixture command is not shipped")
    crown=next(i for i in atlas["items"] if i["id"]==1)
    stash=collection.stash()
    check(atlas["total"]==486 and crown["count"]==4 and crown["bestLP"]==2,
          "player DLL reads real catalog, all four copies and usable LP")
    check(any(i["source"]=="Stash" and i["lp"]==2 for i in stash["items"]),
          "player DLL reads real stash item locations")
    web=ipc.request(f"echoread {sid} 1 normal")
    check(web.get("ok") and web["generated"] and len(web["echoes"])==7,
          "player DLL reads existing rewards and Echo graph")
    target=next(e["index"] for e in web["echoes"] if e["runnable"])
    focus=ipc.request(f"echofocus {sid} 1 normal {target}")
    check(focus.get("selectionVerified") and focus["focused"]==target,
          "player DLL verifies normal in-game selection")
    check(ipc.request(f"echoread {sid} 1 normal")["echoes"]==web["echoes"] and
          collection.stash()["revision"]==stash["revision"],
          "player focus does not advance Echoes or change items")
    empty=ipc.request(f"echoread {sid} 2 normal")
    check(empty.get("ok") and not empty["generated"] and not empty["echoes"],
          "player read leaves an unopened timeline unchanged")
    built=isolated.le_session.BUILD/"EpochPact.Core.dll"
    installed=isolated.le_session.GAME/"EpochPact/EpochPact.Core.dll"
    digest=hashlib.sha256(built.read_bytes()).hexdigest()
    check(hashlib.sha256(installed.read_bytes()).hexdigest()==digest,"installed DLL equals tested player build")
    report=dict(ok=True,gameVersion="1.5.2",profile="player",coreSHA256=digest,checks=checks,
                catalogTypes=atlas["total"],equipment=stash["total"],echoes=len(web["echoes"]),focus=focus)
    (isolated.le_session.OUT/"collection-player-verification.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    return report


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action",choices=("prepare","verify","verify-player","restore"))
    action=parser.parse_args().action
    result=isolated.prepare("EpCraftTest") if action=="prepare" else verify() if action=="verify" else verify_player() if action=="verify-player" else isolated.restore()
    print(json.dumps(result,indent=2))
