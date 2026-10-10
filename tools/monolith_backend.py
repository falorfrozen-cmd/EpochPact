"""Monolith IPC API for the user's UI. Every mutation includes the loaded save id."""
from __future__ import annotations
import math
import re
import time
try:
    from . import progression_backend as progression
except ImportError:
    import progression_backend as progression


def read() -> dict:
    result = progression.request("monolithread")
    if not result.get("ok"):
        raise RuntimeError(result.get("error", "Monolith bilgisi okunamadı."))
    return result


def _id(save_id: str) -> str:
    if not isinstance(save_id, str) or not re.fullmatch(r"\d+", save_id):
        raise ValueError("Geçersiz karakter kimliği.")
    return save_id


def unlock(save_id: str) -> dict:
    return progression.request(f"monolithunlock {_id(save_id)}")


def _ensure_rest(save_id: str) -> None:
    live = read()
    if live.get("player", {}).get("id") != save_id:
        raise RuntimeError("The loaded character changed. Refresh live data.")
    if live.get("player", {}).get("scene") == "M_Rest" and live.get("restContextReady"):
        return
    if live.get("editable") is not True:
        raise RuntimeError("Open timelines from End of Time, Monolith Hub or Traveler's Rest.")
    # Only explicit Open/Show-in-game actions call this. Refreshing the panel
    # never travels or starts an Echo. Use the actual unlocked waypoint.
    if live.get("player", {}).get("scene") != "M_Rest":
        reply = progression.le_session.send(f"monolithrest {save_id}", timeout=90)
        if not reply or "normal waypoint transition requested for M_Rest" not in reply:
            raise RuntimeError(reply or "Traveler's Rest did not respond.")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        identity = progression.request("identityread")
        player = identity.get("player", {})
        if player and player.get("id") != save_id:
            raise RuntimeError("The loaded character changed during travel.")
        if identity.get("ok") and player.get("scene") == "M_Rest":
            ready = read()
            if ready.get("player", {}).get("id") != save_id:
                raise RuntimeError("The loaded character changed during travel.")
            if ready.get("restContextReady"):
                return
        time.sleep(0.2)
    raise RuntimeError("Traveler's Rest has not finished loading. Resume the game and try again.")


def _target(action: str, save_id: str, timeline: int, empowered: bool, value: int | None = None) -> dict:
    if type(timeline) is not int or not 1 <= timeline <= 254 or type(empowered) is not bool:
        raise ValueError("Geçersiz timeline veya zorluk.")
    command = f"{action} {_id(save_id)} {timeline} {'empowered' if empowered else 'normal'}"
    if value is not None:
        if type(value) is not int or not 0 <= value <= 2147483647:
            raise ValueError("Değer pozitif bir tam sayı veya sıfır olmalı.")
        command += f" {value}"
    if action in ("monolithselect", "echofocus"):
        with progression._ipc_lock:
            _ensure_rest(save_id)
            target = f"{save_id} {timeline} {'empowered' if empowered else 'normal'}"
            if action == "echofocus":
                panel = progression.request(f"monolithpanelready {target}")
                if not panel.get("panelReady"):
                    result = progression.request(f"monolithselect {target}")
                    if not result.get("ok"):
                        return result
                    _wait_panel(target, save_id)
                return progression.request(command)
            result = progression.request(command)
            if result.get("ok"):
                _wait_panel(target, save_id)
            return result
    return progression.request(command)


def _wait_panel(target: str, save_id: str) -> None:
    """Wait for asynchronous scene/panel work; never repeat the mutation."""
    deadline = time.monotonic() + 60
    stable = 0
    while time.monotonic() < deadline:
        result = progression.request(f"monolithpanelready {target}")
        player = result.get("player", {})
        if player and player.get("id") != save_id:
            raise RuntimeError("The loaded character changed while opening the timeline.")
        stable = stable + 1 if result.get("ok") and result.get("panelReady") else 0
        if stable >= 2:
            return
        time.sleep(0.2)
    raise RuntimeError("The timeline is still loading. The opening request was not repeated.")


def select(save_id: str, timeline: int, empowered: bool) -> dict:
    return _target("monolithselect", save_id, timeline, empowered)


def echoes(save_id: str, timeline: int, empowered: bool) -> dict:
    return _target("echoread", save_id, timeline, empowered)


def focus_echo(save_id: str, timeline: int, empowered: bool, index: int) -> dict:
    return _target("echofocus", save_id, timeline, empowered, index)


def corruption(save_id: str, timeline: int, empowered: bool, value: int) -> dict:
    return _target("corruption", save_id, timeline, empowered, value)


def stability(save_id: str, timeline: int, empowered: bool, value: int) -> dict:
    return _target("stability", save_id, timeline, empowered, value)


def multiplier(value: float) -> dict:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 1 <= value <= 100:
        raise ValueError("Stability çarpanı 1–100 arasında olmalı.")
    return progression.request(f"stabilitymult {value:.9g}")


undo = progression.undo
