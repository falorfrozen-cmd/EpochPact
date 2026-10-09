"""Isolated offline Monolith check. prepare/restore require the game to be closed.

Never loads or advances Falor. The manifest and original bytes are saved before
creating a character with a separate character-found stash.
"""
from __future__ import annotations
import argparse
import copy
import hashlib
import json
import re
import shutil
from pathlib import Path
try:
    from . import le_session
    from .progression_backend import _json
except ImportError:
    import le_session
    from progression_backend import _json

MANIFEST = le_session.OUT / "monolith-test-manifest.json"


def prepare(test_name: str = "EpMonolithTest") -> dict:
    if not re.fullmatch(r"Ep[A-Za-z]+Test", test_name):
        raise ValueError("Isolated test character name expected.")
    if le_session.game_pids():
        raise RuntimeError("Close the game normally before preparing the test.")
    if MANIFEST.exists() and not _json(MANIFEST).get("restored"):
        raise RuntimeError("An unrestored test already exists; restore it first.")
    saves = le_session.SAVES.resolve(strict=True)
    files = list(saves.iterdir())
    if any(not f.is_file() or f.is_symlink() for f in files):
        raise RuntimeError("Unexpected save directory entry.")
    backup = le_session.backup_saves()
    if backup is None:
        raise RuntimeError("Save backup unavailable.")
    source = _json(saves / "1CHARACTERSLOT_BETA_0")
    occupied = {int(m[1]) for f in files if (m := re.match(r"1CHARACTERSLOT_BETA_(\d+)", f.name))}
    index = max(occupied, default=0) + 1
    name = f"1CHARACTERSLOT_BETA_{index}"
    target = saves / name
    if target.exists():
        raise RuntimeError("Test save already exists.")
    clone = copy.deepcopy(source)
    clone.update(id=str(index), characterName=test_name, soloChallenge=True, soloCharacterChallenge=True)
    manifest = {
        "backup": str(backup), "saveDirectory": str(saves), "id": str(index),
        "testName": test_name, "testFile": name, "restored": False,
        "originalCharacter": {k: source.get(k) for k in ("id", "characterName", "level", "cycle")},
        "files": {f.name: hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
    }
    MANIFEST.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    original_text = (saves / "1CHARACTERSLOT_BETA_0").read_text(encoding="utf-8-sig")
    prefix = original_text[:original_text.index("{")]
    target.write_text(prefix + json.dumps(clone, separators=(",", ":")), encoding="utf-8")
    return manifest


def restore() -> dict:
    if le_session.game_pids():
        raise RuntimeError("Close the game normally before restoring the test.")
    manifest = _json(MANIFEST)
    saves = le_session.SAVES.resolve(strict=True)
    if saves != Path(manifest["saveDirectory"]).resolve(strict=True):
        raise RuntimeError("Save directory changed.")
    backup = Path(manifest["backup"]).resolve(strict=True)
    if backup.parent != (le_session.OUT / "saves-backups").resolve(strict=True):
        raise RuntimeError("Unexpected backup directory.")
    # Validate every source hash and target path before changing any file.
    for name, digest in manifest["files"].items():
        source, target = backup / name, saves / name
        if source.is_symlink() or target.is_symlink() or source.parent != backup or target.parent != saves:
            raise RuntimeError("Unsafe restore path.")
        if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Original backup bytes changed.")
    extra = [f for f in saves.iterdir() if f.name not in manifest["files"]]
    clone_stash = f"STASH_CYCLE_{manifest['originalCharacter']['cycle']}_2_{manifest['id']}"
    for file in extra:
        if (file.is_symlink() or not file.is_file() or file.parent != saves or
            not (file.name == manifest["testFile"] or file.name.startswith(manifest["testFile"] + "_") or
                 file.name.startswith(manifest["testFile"] + ".") or file.name == clone_stash or
                 file.name.startswith(clone_stash + "_") or file.name.startswith(clone_stash + "."))):
            raise RuntimeError(f"Unexpected new save file; preserve it: {file.name}")
    archive = le_session.OUT / (MANIFEST.stem.removesuffix("-manifest") + "-saves")
    archive.mkdir(exist_ok=True)
    for file in extra:
        shutil.copy2(file, archive / file.name)
        file.unlink()  # Only explicitly validated test files inside the known Saves directory.
    for name in manifest["files"]:
        shutil.copy2(backup / name, saves / name)
    if any(hashlib.sha256((saves / name).read_bytes()).hexdigest() != digest for name, digest in manifest["files"].items()):
        raise RuntimeError("Restored save verification failed.")
    manifest.update(restored=True, archivedTestFiles=[f.name for f in extra])
    MANIFEST.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return {"restoredFiles": len(manifest["files"]), "archivedTestFiles": len(extra), "originalBytesVerified": True}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=("prepare", "restore"))
    args = parser.parse_args()
    result = prepare() if args.action == "prepare" else restore()
    print(json.dumps(result if args.action == "restore" else {k: result[k] for k in ("backup", "id", "originalCharacter")}, indent=2))
