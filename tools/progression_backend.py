"""Campaign actions over EpochPact IPC, with recovery from native snapshots."""
from __future__ import annotations

import argparse
import json
import re
import shutil
import threading
from pathlib import Path

try:
    from . import le_session
except ImportError:
    import le_session

_ipc_lock = threading.RLock()
BACKUPS = le_session.GAME / "EpochPact" / "backups" / "progression"


def request(command: str) -> dict:
    with _ipc_lock:
        reply = le_session.send(command, timeout=90)
    if not reply:
        raise RuntimeError("Oyun komuta yanıt vermedi.")
    try:
        result = json.loads(reply)
    except json.JSONDecodeError as exc:
        raise RuntimeError(reply) from exc
    return result


def read() -> dict:
    result = request("progressread")
    if not result.get("ok"):
        raise RuntimeError(result.get("error", "Karakter bilgisi okunamadı."))
    return result


def apply(action: str, expected_id: str) -> dict:
    if action not in ("questscomplete", "waypointsunlock") or not re.fullmatch(r"\d+", expected_id):
        raise ValueError("Geçersiz işlem veya karakter kimliği.")
    return request(f"{action} {expected_id}")


def _json(path: Path) -> dict:
    text = path.read_text(encoding="utf-8-sig")
    start = text.find("{")
    if start < 0:
        raise ValueError(f"Geçersiz kayıt: {path.name}")
    data = json.loads(text[start:])
    if not isinstance(data, dict):
        raise ValueError("Kayıt nesnesi bekleniyor.")
    return data


def validate_backup(path: Path, *, backup_root: Path = BACKUPS, saves: Path = le_session.SAVES) -> dict:
    """Validate every recovery input before touching the active saves."""
    if path.is_symlink():
        raise ValueError("Yedek bir sembolik bağlantı olamaz.")
    path = path.resolve(strict=True)
    backup_root = backup_root.resolve(strict=True)
    if path.parent != backup_root or path.is_symlink():
        raise ValueError("EpochPact görev yedeği klasörü bekleniyor.")
    for filename in ("manifest.json", "live-character.json", "live-stash.json", "live-global.json"):
        file = path / filename
        if file.is_symlink() or not file.is_file():
            raise ValueError("Canlı yedek dosyası eksik veya bağlantı içeriyor.")
    manifest = _json(path / "manifest.json")
    if manifest.get("format") != 1 or manifest.get("operation") not in (
        "questscomplete", "waypointsunlock", "monolithunlock", "monolithselect", "corruption", "stability",
        "cofjoin", "cofrank", "coffavor", "cofreputation", "coflenses", "cofprophecy", "cofcharges", "forge-one-item"
    ):
        raise ValueError("Yedek biçimi tanınmıyor.")
    save_id, stash_id = manifest.get("id", ""), manifest.get("stashId", "")
    if not isinstance(save_id, str) or not re.fullmatch(r"\d+", save_id):
        raise ValueError("Yedekteki karakter kimliği geçersiz.")
    if not isinstance(stash_id, str) or not re.fullmatch(r"STASH_[A-Za-z0-9_-]+", stash_id):
        raise ValueError("Yedekteki kasa kimliği geçersiz.")
    if Path(manifest.get("saveDirectory", "")).resolve() != saves.resolve():
        raise ValueError("Yedek başka bir kayıt klasörüne ait.")
    character = _json(path / "live-character.json")
    stash = _json(path / "live-stash.json")
    global_data = _json(path / "live-global.json")
    if character.get("id") != save_id or character.get("characterName") != manifest.get("player"):
        raise ValueError("Canlı karakter yedeği manifest ile uyuşmuyor.")
    if stash.get("id") != stash_id:
        raise ValueError("Canlı kasa yedeği manifest ile uyuşmuyor.")
    directory = path / "saves"
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError("Kayıt yedeği eksik.")
    files = list(directory.iterdir())
    if not files or any(not file.is_file() or file.is_symlink() for file in files):
        raise ValueError("Yedekte beklenmeyen dosya bulundu.")
    if not (directory / f"1CHARACTERSLOT_BETA_{save_id}").is_file():
        raise ValueError("Asıl karakter kayıt dosyası yedekte yok.")
    return {"manifest": manifest, "character": character, "stash": stash, "global": global_data, "files": files}


def restore_files(path: Path, *, backup_root: Path = BACKUPS, saves: Path = le_session.SAVES) -> dict:
    """Only with the game closed. Keep a recovery backup of the state being replaced."""
    if le_session.game_pids():
        raise RuntimeError("Geri alma için oyun kapalı olmalı.")
    bundle = validate_backup(path, backup_root=backup_root, saves=saves)
    manifest = bundle["manifest"]
    recovery = le_session.backup_saves()
    if not recovery:
        raise RuntimeError("Mevcut kayıtların kurtarma yedeği alınamadı.")
    # Disk snapshots can lag live play. Overlay the live serialized data captured
    # before the action, including the shared wallet and global unlock flags.
    overlays = {
        f"1CHARACTERSLOT_BETA_{manifest['id']}": bundle["character"],
        manifest["stashId"]: bundle["stash"],
        "Epoch_Local_Global_Data_Beta": bundle["global"],
    }
    staged = []
    for filename, data in overlays.items():
        contents = "EPOCH" + json.dumps(data, ensure_ascii=False, separators=(",", ":"))
        for suffix in ("", ".bak", "_temp"):
            target = saves / (filename + suffix)
            if suffix and not target.exists():
                continue
            if target.is_symlink():
                raise ValueError("Aktif kayıt bir bağlantı olamaz.")
            staged.append((target, contents))
    # Other characters, unrelated stashes and stash tabs are never rolled back.
    # Stage every affected file before replacing any of them.
    temporaries = []
    try:
        for index, (target, contents) in enumerate(staged):
            temporary = Path(recovery) / f"progression-overlay-{index}.tmp"
            temporary.write_text(contents, encoding="utf-8")
            temporaries.append((temporary, target))
        for temporary, target in temporaries:
            shutil.copyfile(temporary, target.with_name(target.name + ".epochpact-restore"))
        for _, target in temporaries:
            target.with_name(target.name + ".epochpact-restore").replace(target)
    finally:
        for temporary, _ in temporaries:
            temporary.unlink(missing_ok=True)
    return {"ok": True, "player": manifest["player"], "level": bundle["character"].get("level"),
            "recoveryBackup": str(recovery)}


def undo(path: Path) -> dict:
    # Validate and match the live character before asking the normal game window to close.
    bundle = validate_backup(path)
    current = read()["player"]
    manifest = bundle["manifest"]
    if current["id"] != manifest["id"] or current["name"] != manifest["player"]:
        raise RuntimeError("Geri alınacak işlemin karakteri şu anda yüklü değil.")
    if le_session.cmd_close(argparse.Namespace(timeout=30)):
        raise RuntimeError("Oyun temizce kapatılamadı; kayıtlar değiştirilmedi.")
    return restore_files(path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("read")
    for name in ("complete", "waypoints"):
        command = sub.add_parser(name)
        command.add_argument("save_id")
    command = sub.add_parser("restore")
    command.add_argument("backup", type=Path)
    args = parser.parse_args()
    if args.command == "read":
        result = read()
    elif args.command == "restore":
        result = restore_files(args.backup)
    else:
        result = apply("questscomplete" if args.command == "complete" else "waypointsunlock", args.save_id)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result.get("ok") else 1


if __name__ == "__main__":
    raise SystemExit(main())
