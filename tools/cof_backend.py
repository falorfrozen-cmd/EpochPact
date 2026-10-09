"""Circle of Fortune IPC API for the owner's UI; mutations retain recovery paths."""
from __future__ import annotations
import math
import re
try:
    from . import progression_backend as progression
except ImportError:
    import progression_backend as progression


def read() -> dict:
    result = progression.request("cofread")
    if not result.get("ok"):
        raise RuntimeError(result.get("error", "CoF bilgisi okunamadı."))
    return result


def _id(save_id: str) -> str:
    if not isinstance(save_id, str) or not re.fullmatch(r"[0-9]+", save_id):
        raise ValueError("Geçersiz karakter kimliği.")
    return save_id


def _integer(value: int, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f"Tam sayı {minimum}–{maximum} arasında olmalı.")
    return value


def join(save_id: str, *, switch_from_merchant: bool = False) -> dict:
    if type(switch_from_merchant) is not bool:
        raise ValueError("Faction değişimi için bool bekleniyor.")
    return progression.request(f"cofjoin {_id(save_id)}" + (" switch" if switch_from_merchant else ""))


def rank(save_id: str, value: int) -> dict:
    return progression.request(f"cofrank {_id(save_id)} {_integer(value, 1, 12)}")


def favor(save_id: str, value: int) -> dict:
    return progression.request(f"coffavor {_id(save_id)} {_integer(value, 0, 999999)}")


def reputation(save_id: str, amount: int) -> dict:
    return progression.request(f"cofreputation {_id(save_id)} {_integer(amount, 0, 1000000)}")


def unlock_lenses(save_id: str) -> dict:
    return progression.request(f"coflenses {_id(save_id)}")


def prophecy(save_id: str, slot: int, reward: int | None, lens: int | None, *, preview: bool = False) -> dict:
    if type(preview) is not bool:
        raise ValueError("Önizleme için bool bekleniyor.")
    selected_reward = "none" if reward is None else str(_integer(reward, 0, 65535))
    selected_lens = "none" if lens is None else str(_integer(lens, 0, 11))
    command = "cofpreview" if preview else "cofprophecy"
    return progression.request(f"{command} {_id(save_id)} {_integer(slot, 0, 3)} {selected_reward} {selected_lens}")


def charges(save_id: str, slot: int, value: int) -> dict:
    return progression.request(f"cofcharges {_id(save_id)} {_integer(slot, 0, 3)} {_integer(value, 0, 99)}")


def _multiplier(value: float) -> str:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not 1 <= value <= 100:
        raise ValueError("Kazanç çarpanı 1–100 arasında olmalı.")
    return f"{value:.9g}"


def favor_multiplier(value: float) -> dict:
    return progression.request(f"coffavormult {_multiplier(value)}")


def reputation_multiplier(value: float) -> dict:
    return progression.request(f"cofrepmult {_multiplier(value)}")


multiplier = favor_multiplier  # Existing UI callers retain their Favor control.


def charge_multiplier(value: float) -> dict:
    return progression.request(f"cofchargemult {_multiplier(value)}")


def reward_multiplier(value: float) -> dict:
    formatted = _multiplier(value)
    if value > 25:
        raise ValueError("Prophecy ödül çarpanı 1–25 arasında olmalı.")
    return progression.request(f"cofrewardmult {formatted}")


def double_drop_chance(source: str, percent: float | None) -> dict:
    if source not in ("enemy", "echo"):
        raise ValueError("Çift drop kaynağı enemy veya echo olmalı.")
    if percent is None:
        value = "reset"
    else:
        if isinstance(percent, bool) or not isinstance(percent, (int, float)) or not math.isfinite(percent) or not 0 <= percent <= 100:
            raise ValueError("Çift drop ihtimali yüzde 0–100 arasında olmalı.")
        value = f"{percent:.9g}"
    return progression.request(f"cofdouble {source} {value}")


def exalted_multiplier(value: float) -> dict:
    return progression.request(f"cofexaltedmult {_multiplier(value)}")


def t7_multiplier(value: float) -> dict:
    return progression.request(f"coft7mult {_multiplier(value)}")


def lp_multiplier(value: float) -> dict:
    return progression.request(f"coflpmult {_multiplier(value)}")


def lens_multiplier(lens: str, value: float) -> dict:
    if lens not in ("celerity", "charity", "duplication"):
        raise ValueError("Lens celerity, charity veya duplication olmalı.")
    return progression.request(f"coflensmult {lens} {_multiplier(value)}")


undo = progression.undo
