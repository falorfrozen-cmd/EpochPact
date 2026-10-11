"""Validate the application-scoped CLR configuration required beside the EXE."""
from pathlib import Path
import xml.etree.ElementTree as ET


NAME = 'EpochPact.exe.config'


def validate(path: Path) -> bytes:
    try:
        data = path.read_bytes()
        root = ET.fromstring(data)
    except (OSError, ET.ParseError) as exc:
        raise ValueError('Missing or invalid EpochPact.exe.config; extract the entire ZIP.') from exc
    if (root.tag != 'configuration' or root.attrib or len(root) != 1
            or root[0].tag != 'runtime' or root[0].attrib or len(root[0]) != 1
            or root[0][0].tag != 'loadFromRemoteSources'
            or root[0][0].attrib != {'enabled': 'true'} or len(root[0][0])):
        raise ValueError('Invalid EpochPact.exe.config; rebuild with the application CLR configuration.')
    return data
