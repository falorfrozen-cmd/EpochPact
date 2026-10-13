"""Check (and merge) the interface translations.

ui/i18n-source.json lists every English interface text (tools/extract_ui_text.py).
ui/i18n-<language>.json maps those texts to a translation. This reports texts
without a translation and translations whose {0}/{1} placeholders differ from
the English text.

    py -3 tools/i18n_check.py                       # report; exit 1 when incomplete
    py -3 tools/i18n_check.py --merge tr DIR        # merge DIR/src_NN.json + DIR/tr_NN.json
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = ('tr', 'ru')
PLACEHOLDER = re.compile(r"\{\d+\}")


def placeholders(text: str) -> list[str]:
    return sorted(PLACEHOLDER.findall(text))


def problems(source: list[str], dictionary: dict) -> dict:
    missing = [t for t in source if not isinstance(dictionary.get(t), str) or not dictionary[t].strip()]
    mismatched = [t for t in source if isinstance(dictionary.get(t), str) and placeholders(t) != placeholders(dictionary[t])]
    return {'missing': missing, 'placeholders': mismatched}


def load(language: str) -> dict:
    path = ROOT / f'ui/i18n-{language}.json'
    return json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}


def save(language: str, dictionary: dict) -> None:
    source = json.loads((ROOT / 'ui/i18n-source.json').read_text(encoding='utf-8'))
    ordered = {t: dictionary[t] for t in source if t in dictionary}
    ordered.update({k: v for k, v in sorted(dictionary.items()) if k not in ordered})
    (ROOT / f'ui/i18n-{language}.json').write_text(json.dumps(ordered, ensure_ascii=False, indent=0) + '\n', encoding='utf-8', newline='\n')


def merge(language: str, folder: Path) -> int:
    dictionary = load(language)
    added = 0
    for src in sorted(folder.glob('src_*.json')):
        target = folder / src.name.replace('src_', f'{language}_')
        if not target.exists():
            print(f'missing {target.name}')
            continue
        english = json.loads(src.read_text(encoding='utf-8'))
        translated = json.loads(target.read_text(encoding='utf-8'))
        if len(english) != len(translated):
            print(f'{target.name}: {len(translated)} entries for {len(english)} texts; skipped')
            continue
        for e, t in zip(english, translated):
            if isinstance(t, str) and t.strip():
                dictionary[e] = t
                added += 1
    save(language, dictionary)
    return added


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--merge', nargs=2, metavar=('LANGUAGE', 'FOLDER'))
    args = parser.parse_args()
    if args.merge:
        print(f'{merge(args.merge[0], Path(args.merge[1]))} translations merged')
    source = json.loads((ROOT / 'ui/i18n-source.json').read_text(encoding='utf-8'))
    failed = False
    for language in LANGUAGES:
        found = problems(source, load(language))
        print(f'{language}: {len(source) - len(found["missing"])}/{len(source)} translated, {len(found["placeholders"])} placeholder mismatches')
        for text in found['missing'][:10] + found['placeholders'][:10]:
            print('   ', text[:100])
        failed |= bool(found['missing'] or found['placeholders'])
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
