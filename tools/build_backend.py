"""Existing native build/workshop commands, using the shared serialized IPC."""
from __future__ import annotations
try:
    from . import progression_backend as ipc
except ImportError:
    import progression_backend as ipc


def workshop_catalog():
    with ipc._ipc_lock:
        templates = ipc.request("atlasread")
        affixes = ipc.request("lootread")
        identity = ipc.request("identityread")
        modifiers = ipc.request("craftmods")
        after = ipc.request("identityread")
        for result in (templates, affixes, identity, modifiers, after):
            if result.get("ok") is not True:
                raise RuntimeError(result.get("error", "Could not read workshop templates."))
        if any(templates.get("player", {}).get(k) != result.get("player", {}).get(k) for k in ("id", "name") for result in (identity, after)):
            raise RuntimeError("The loaded character changed. Load workshop templates again.")
        return {"ok": True, "player": templates["player"], "items": templates["items"], "affixes": affixes["affixes"], "modifiers": modifiers["modifiers"]}


def tiers(save_id, tiers):
    # New tiers for affixes already on the forge item: "ID:tier,..." (native code checks them again).
    return ipc.request(f"crafttiers {save_id} {tiers}")


def spawn_list():
    # The spawnable items and, for Spawn with chosen affixes, the affixes with the item types they roll on.
    with ipc._ipc_lock:
        items = ipc.request("spawnlist")
        if items.get("ok") is not True:
            return items
        affixes = ipc.request("lootread")
    if affixes.get("ok") is True:
        items["affixes"] = [{k: a[k] for k in ("id", "name", "tiers", "classes", "rollsOn", "groups") if k in a} for a in affixes.get("affixes", [])]
    return items


def custom(save_id, template_id, affixes, perfect, modifiers="none"):
    # UI validation owns ranges and expected identity; native code independently
    # validates all inputs and snapshots the selected item before replacement.
    if modifiers != "none":
        return ipc.request(f"craftmix {save_id} {template_id} {affixes} {perfect} {modifiers}")
    return ipc.request(f"craftcustom {save_id} {template_id} {affixes} {perfect}")
