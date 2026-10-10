"""Validated session controls for smart pickup and the normal offline forge."""
from __future__ import annotations
import math
import re
try:
    from . import progression_backend as ipc
except ImportError:
    import progression_backend as ipc


def loot_read() -> dict:
    return ipc.request("lootread")


def craft_read() -> dict:
    return ipc.request("craftread")


def loot_mode(mode: str) -> dict:
    if mode not in ("all", "filter", "quality", "materials"):
        raise ValueError("Toplama modu all/filter/quality/materials olmalı.")
    return ipc.request(f"lootmode {mode}")


def minimum_lp(value: int) -> dict:
    if type(value) is not int or not 0 <= value <= 4:
        raise ValueError("LP alt sınırı 0–4 tam sayı olmalı.")
    return ipc.request(f"lootlp {value}")


def _toggle(value) -> int:
    if type(value) not in (bool, int) or value not in (0, 1):
        raise ValueError("0 veya 1 bekleniyor.")
    return int(value)


def t7(value: bool) -> dict:
    return ipc.request(f"loott7 {_toggle(value)}")


def respect_filter(value: bool) -> dict:
    return ipc.request(f"lootfilter {_toggle(value)}")


def category(category: str, value: bool) -> dict:
    if category not in ("materials", "gold", "potions", "xp", "favor", "bones"):
        raise ValueError("Geçersiz toplama kategorisi.")
    return ipc.request(f"lootcategory {category} {_toggle(value)}")


def affixes(ids: list[int]) -> dict:
    if not isinstance(ids, list) or len(ids) > 64 or any(type(i) is not int or not 0 <= i <= 65535 for i in ids):
        raise ValueError("En fazla 64 geçerli affix ID bekleniyor.")
    unique = list(dict.fromkeys(ids))
    value = ",".join(map(str, unique)) if unique else "none"
    return ipc.request(f"lootaffixes {value}")


def affixes_csv(value: str) -> dict:
    if value == "none":
        return affixes([])
    if not isinstance(value, str) or not re.fullmatch(r"\d+(?:,\d+)*", value):
        raise ValueError("Virgülle ayrılmış affix ID veya none bekleniyor.")
    return affixes([int(i) for i in value.split(",")])


def fp_factor(value: float) -> dict:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 1:
        raise ValueError("FP maliyet katsayısı 0–1 arasında olmalı.")
    return ipc.request(f"craftfp {value:.9g}")


def glyph_chance(glyph: str, value: float | None) -> dict:
    if glyph not in ("hope", "despair"):
        raise ValueError("Glyph hope veya despair olmalı.")
    if value is None:
        token = "reset"
    else:
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 0 <= value <= 100:
            raise ValueError("Glyph ihtimali yüzde 0–100 olmalı.")
        token = f"{value:.9g}"
    return ipc.request(f"craft{glyph} {token}")


def preserve_shards(value: bool) -> dict:
    return ipc.request(f"craftshards {_toggle(value)}")


def preserve_runes(value: bool) -> dict:
    return ipc.request(f"craftrunes {_toggle(value)}")


def preserve_glyphs(value: bool) -> dict:
    return ipc.request(f"craftglyphs {_toggle(value)}")


def bypass_level(value: bool) -> dict:
    return ipc.request(f"craftlevel {_toggle(value)}")


def reveal_map(value: bool) -> dict:
    return ipc.request(f"mapreveal {_toggle(value)}")


def map_read() -> dict:
    return ipc.request("mapread")


def forge(save_id: str) -> dict:
    if not isinstance(save_id, str) or not re.fullmatch(r"[0-9]+", save_id):
        raise ValueError("A loaded offline save ID is required.")
    return ipc.request(f"craftforge {save_id}")


def reset_loot() -> dict:
    return ipc.request("lootreset")


def reset_craft() -> dict:
    return ipc.request("craftreset")
