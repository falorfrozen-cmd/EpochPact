"""Loot filter files: list, back up, export, import and duplicate.

Last Epoch keeps loot filters as XML files in one folder per Windows user,
shared by every character and season. EpochPact never edits or deletes an
existing filter file: imports and copies are written as new files, exports and
backups go to EpochPact's own folder.
"""
from __future__ import annotations

import datetime as _dt
import os
import re
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from .app_paths import user_root

SIZE_LIMIT = 2 * 1024 * 1024
NAME_LIMIT = 60
BACKUPS_KEPT = 20
_RESERVED = {'CON', 'PRN', 'AUX', 'NUL', *(f'COM{i}' for i in range(1, 10)), *(f'LPT{i}' for i in range(1, 10))}


def filters_root() -> Path:
    return Path(os.environ.get('USERPROFILE', Path.home())) / 'AppData' / 'LocalLow' / 'Eleventh Hour Games' / 'Last Epoch' / 'Filters'


def _parse(data: bytes) -> ET.Element:
    """Strict parse: no DTD/entities, an ItemFilter root with a name and rules."""
    if len(data) > SIZE_LIMIT:
        raise ValueError('This file is larger than 2 MB and is not a Last Epoch loot filter.')
    head = data[:4096].lstrip(b'\xef\xbb\xbf').lower()
    if b'<!doctype' in data.lower() or b'<!entity' in data.lower():
        raise ValueError('This file contains a document type declaration and was refused.')
    if not head.lstrip().startswith((b'<?xml', b'<itemfilter')):
        raise ValueError('This file is not a Last Epoch loot filter.')
    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        raise ValueError(f'This loot filter file is damaged ({exc}).') from exc
    if root.tag != 'ItemFilter' or root.find('rules') is None or root.find('name') is None:
        raise ValueError('This file is not a Last Epoch loot filter.')
    return root


def _summary(path: Path) -> dict:
    stat = path.stat()
    entry = {'file': path.name, 'size': stat.st_size, 'modified': int(stat.st_mtime)}
    try:
        root = _parse(path.read_bytes())
        rules = root.find('rules')
        entry.update(name=(root.findtext('name') or '').strip() or path.stem,
                     rules=len(rules.findall('Rule')),
                     enabled=sum((r.findtext('isEnabled') or '').strip().lower() == 'true' for r in rules.findall('Rule')),
                     version=(root.findtext('lastModifiedInVersion') or '').strip(), valid=True)
    except (OSError, ValueError) as exc:
        entry.update(name=path.stem, rules=0, enabled=0, version='', valid=False, error=str(exc))
    return entry


def _files(root: Path) -> list[Path]:
    if not root.is_dir():
        return []
    return sorted((p for p in root.iterdir() if p.suffix.lower() == '.xml' and p.is_file() and not p.is_symlink()),
                  key=lambda p: p.name.lower())


def list_filters(root: Path | None = None) -> dict:
    root = root or filters_root()
    return {'ok': True, 'folder': str(root), 'exists': root.is_dir(), 'filters': [_summary(p) for p in _files(root)]}


def _existing(root: Path, file: str) -> Path:
    """A filter in the folder by exact file name; no paths or links."""
    if not isinstance(file, str) or not file or Path(file).name != file or not file.lower().endswith('.xml'):
        raise ValueError('Choose a loot filter from the list.')
    for path in _files(root):
        if path.name == file:
            return path
    raise ValueError('That loot filter no longer exists. Refresh the list.')


def safe_name(name: str) -> str:
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', '', name or '').strip().rstrip('. ')
    name = re.sub(r'\s+', ' ', name)[:NAME_LIMIT].rstrip('. ')
    if not name or name.split('.')[0].upper() in _RESERVED:
        name = 'Imported filter'
    return name


def _unique(root: Path, name: str) -> tuple[str, Path]:
    taken = {p.name.lower() for p in root.iterdir()} if root.is_dir() else set()
    candidate, n = name, 2
    while candidate.lower() + '.xml' in taken:
        candidate = f'{name} ({n})'
        n += 1
    return candidate, root / (candidate + '.xml')


def _rename(data: bytes, name: str) -> bytes:
    """Change only the filter's own <name> element so the game lists it separately."""
    text = data.decode('utf-8-sig')
    escaped = name.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
    renamed, count = re.subn(r'(<ItemFilter\b[^>]*>\s*)<name\s*(?:/>|>[^<]*</name>)', lambda m: m.group(1) + f'<name>{escaped}</name>', text, count=1)
    if count != 1:
        raise ValueError('This loot filter has an unexpected layout and was not copied.')
    _parse(renamed.encode('utf-8'))
    return renamed.encode('utf-8')


def _write_new(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(data)


def _add(root: Path, data: bytes, fallback: str) -> str:
    filter_root = _parse(data)
    original = (filter_root.findtext('name') or '').strip()
    name, target = _unique(root, safe_name(original or fallback))
    _write_new(target, data if name == original else _rename(data, name))
    return name


def import_filter(source: str, root: Path | None = None) -> dict:
    """Import one .xml filter, or every filter in an EpochPact backup .zip, as new files."""
    root = root or filters_root()
    path = Path(source) if isinstance(source, str) and source else None
    if path is None or not path.is_absolute() or path.suffix.lower() not in ('.xml', '.zip'):
        raise ValueError('Choose a loot filter .xml file or an EpochPact filter backup .zip.')
    if path.is_symlink() or not path.is_file():
        raise ValueError('That file cannot be read.')
    if path.suffix.lower() == '.xml':
        if root.is_dir() and path.resolve().parent == root.resolve():
            raise ValueError('That filter is already in your Last Epoch filter folder.')
        names = [_add(root, path.read_bytes(), path.stem)]
    else:
        if path.stat().st_size > 64 * SIZE_LIMIT:
            raise ValueError('This backup is too large.')
        with zipfile.ZipFile(path) as archive:
            members = [m for m in archive.infolist() if not m.is_dir() and m.filename.lower().endswith('.xml')
                       and '/' not in m.filename and '\\' not in m.filename]
            if not members or len(members) > 500:
                raise ValueError('This ZIP does not contain loot filter files.')
            if any(m.file_size > SIZE_LIMIT for m in members):
                raise ValueError('This ZIP contains a file larger than 2 MB and was refused.')
            payloads = [(Path(m.filename).stem, archive.read(m)) for m in members]
        for _, data in payloads:
            _parse(data)  # validate everything before writing anything
        names = [_add(root, data, stem) for stem, data in payloads]
    label = f'"{names[0]}"' if len(names) == 1 else f'{len(names)} loot filters'
    return {'ok': True, 'imported': names, 'filters': list_filters(root)['filters'],
            'message': f'Imported {label} as new filters; nothing was replaced. Reopen the game\'s loot filter window, '
                       'or restart Last Epoch if a filter is not listed yet.'}


def duplicate_filter(file: str, root: Path | None = None) -> dict:
    root = root or filters_root()
    source = _existing(root, file)
    data = source.read_bytes()
    original = (_parse(data).findtext('name') or '').strip() or source.stem
    name, target = _unique(root, safe_name(original + ' copy'))
    _write_new(target, _rename(data, name))
    return {'ok': True, 'file': target.name, 'name': name, 'filters': list_filters(root)['filters'],
            'message': f'Created "{name}". The original filter was not changed.'}


def export_filter(file: str, root: Path | None = None, output: Path | None = None) -> dict:
    root = root or filters_root()
    source = _existing(root, file)
    data = source.read_bytes()
    _parse(data)
    folder = output or (user_root() / 'filter-exports')
    name, target = _unique(folder, safe_name(source.stem))
    _write_new(target, data)
    return {'ok': True, 'path': str(target), 'folder': str(folder),
            'message': f'Exported as {target.name}. Its folder is open: share the file, and others can use Import filter.'}


def backup_filters(root: Path | None = None, output: Path | None = None) -> dict:
    root = root or filters_root()
    files = _files(root)
    if not files:
        raise ValueError('There are no loot filters to back up yet.')
    folder = output or (user_root() / 'filter-backups')
    folder.mkdir(parents=True, exist_ok=True)
    stamp = _dt.datetime.now().strftime('%Y%m%d-%H%M%S')
    target, n = folder / f'filters-{stamp}.zip', 2
    while target.exists():
        target, n = folder / f'filters-{stamp}-{n}.zip', n + 1
    with zipfile.ZipFile(target, 'x', zipfile.ZIP_DEFLATED) as archive:
        for path in files:
            archive.writestr(path.name, path.read_bytes())
    backups = sorted(folder.glob('filters-*.zip'), key=lambda p: p.stat().st_mtime, reverse=True)
    for old in backups[BACKUPS_KEPT:]:
        try:
            old.unlink()
        except OSError:
            pass
    return {'ok': True, 'path': str(target), 'folder': str(folder), 'count': len(files),
            'message': f'Backed up {len(files)} loot filter{"s" if len(files) != 1 else ""} to {target.name}. The newest {BACKUPS_KEPT} backups are kept.'}
