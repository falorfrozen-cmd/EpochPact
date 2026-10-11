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


def custom(save_id, template_id, affixes, perfect, modifiers="none"):
    # UI validation owns ranges and expected identity; native code independently
    # validates all inputs and snapshots the selected item before replacement.
    if modifiers != "none":
        return ipc.request(f"craftmix {save_id} {template_id} {affixes} {perfect} {modifiers}")
    return ipc.request(f"craftcustom {save_id} {template_id} {affixes} {perfect}")
