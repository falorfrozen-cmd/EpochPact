"""On-demand, serialized inventory reads. Never writes items, containers or saves."""
from __future__ import annotations

from collections import defaultdict
try:
    from . import progression_backend as ipc
except ImportError:
    import progression_backend as ipc


def read_items() -> dict:
    with ipc._ipc_lock:
        rows, first, offset, timings = [], None, 0, []
        for _ in range(656):
            page = ipc.request(f"stashread {offset}")
            if not page.get("ok"):
                raise RuntimeError(page.get("error", "Could not read the loaded stash."))
            identity = (page.get("player", {}).get("id"), page.get("scope"), page.get("revision"), page.get("total"))
            if not all(identity[:3]) or type(identity[3]) is not int or not 0 <= identity[3] <= 65536:
                raise RuntimeError("Invalid collection snapshot identity.")
            if first is None:
                first = page
            elif identity != (first["player"]["id"], first["scope"], first["revision"], first["total"]):
                raise RuntimeError("Items or the loaded character changed during the scan. Refresh again.")
            items = page.get("items")
            if page.get("offset") != offset or not isinstance(items, list) or len(items) > 100:
                raise RuntimeError("Invalid collection page.")
            rows.extend(items)
            timings.append(page.get("readMs", 0))
            nxt = page.get("next")
            if nxt is None:
                if len(rows) != first["total"]:
                    raise RuntimeError("Incomplete collection scan; no partial result was displayed.")
                return {**first, "items": rows, "next": None, "offset": 0,
                        "pages": len(timings), "maxPageMs": max(timings, default=0)}
            if type(nxt) is not int or nxt != offset + len(items) or nxt <= offset or nxt >= first["total"]:
                raise RuntimeError("Invalid collection continuation.")
            offset = nxt
        raise RuntimeError("Collection scan exceeded its page limit.")


def atlas() -> dict:
    with ipc._ipc_lock:
        catalog = ipc.request("atlasread")
        if not catalog.get("ok"):
            raise RuntimeError(catalog.get("error", "Could not read the unique catalog."))
        owned = read_items()
        if catalog.get("player", {}).get("id") != owned["player"]["id"]:
            raise RuntimeError("The loaded character changed during the scan. Refresh again.")
        groups = defaultdict(list)
        for item in owned["items"]:
            if item.get("uniqueId") is not None:
                groups[item["uniqueId"]].append(item)
        entries = []
        seen = set()
        for entry in catalog["items"]:
            if entry["id"] in seen:
                raise RuntimeError("Duplicate unique catalog identity.")
            seen.add(entry["id"])
            copies = groups[entry["id"]]
            # A Legendary has already used its LP. Never present it as a craftable LP copy.
            potentials = [x["lp"] for x in copies if x["kind"] == "Unique" and not x.get("weaversWill")]
            entries.append({**entry, "count": sum(x["quantity"] for x in copies),
                            "bestLP": max(potentials, default=None), "copies": copies})
        return {"ok": True, "player": owned["player"], "scope": owned["scope"], "revision": owned["revision"],
                "items": entries, "owned": sum(x["count"] > 0 for x in entries), "total": len(entries),
                "pages": owned["pages"], "tabs": owned["tabs"], "maxPageMs": owned["maxPageMs"]}


def stash() -> dict:
    result = read_items()
    groups = defaultdict(list)
    for item in result["items"]:
        # Different uniques on the same base are not duplicates. Non-uniques are base comparisons.
        key = ("unique", item["uniqueId"]) if item["uniqueId"] is not None else ("base", item["type"], item["subType"])
        groups[key].append(item)
    result["groups"] = [{"key": ":".join(map(str, k)), "name": v[0]["name"] if k[0] == "unique" else v[0]["baseType"] + " · " + v[0]["name"],
                         "unique": k[0] == "unique", "count": len(v), "items": v}
                        for k, v in groups.items()]
    result["duplicateGroups"] = sum(x["unique"] and x["count"] > 1 for x in result["groups"])
    return result
