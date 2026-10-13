"""List the interface's English text for translation (ui/i18n-source.json).

Collects human-readable text from the page scripts and HTML: string literals,
the static parts of template literals and text between tags, plus the control
labels and descriptions in ui/locale-en.json. Dynamic parts (${...}) become
{0}, {1}, ... so "${n} settings applied" is one entry. The result is the key
list for ui/locale-<language>.json; nothing here runs in the shipped app.

    py -3 tools/extract_ui_text.py            # write ui/i18n-source.json
    py -3 tools/extract_ui_text.py --check    # fail when the list is stale
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['app.js', 'launcher.js', 'collection-ui.js', 'feed.js', 'damage.js', 'damage-meter.js', 'loot-log.js',
           'affix-picker.js', 'crafting-model.js', 'session.js', 'stat-model.js', 'preferences.js', 'release-notes.js',
           'index.html', 'feed.html', 'damage.html']
OUTPUT = ROOT / 'ui/i18n-source.json'
# Player-facing messages raised by the desktop backend.
PYTHON_SOURCES = ['tools/ui_bridge.py', 'tools/player_setup.py', 'tools/ui_language.py', 'tools/app_update.py', 'epochpact_ui.py']
CODE_TOKENS = ('${', '=>', '();', '){', '==', '!=', '&&', '||', '.map(', 'esc(', 'cache.',
               'document.', '`', 'use strict', '="', '.test(', 'Number(', '+(', '];', '?.', 'catch{', 'await ', '$(', 'querySelector')
# Keywords count as code only in code shape: "let x=" is code, "the panels let clicks pass" is text.
CODE_SHAPES = re.compile(r"\b(?:let|const|var)\s+[\w$]+\s*=|\bfunction\b\s*[\w$]*\s*\(|\breturn\s*['\"(]|\breturn\s+[^\s.]+[;)?]|\bif\s*\(|[\w$]\.[a-z]\w*\(")

WORD = re.compile(r"[A-Za-z][A-Za-z'’]+")
# A count and lowercase words: "1 tab created", "{0} stats".
COUNT_PHRASE = re.compile(r"\(?(?:\{\d+\}|\d+) [a-z][a-z’' ]*[a-z.!…]\)?")
CODEY = re.compile(r"^[a-z][A-Za-z0-9]*$|^[a-z0-9_.-]+$|^[#.\[][\w-]|=>|\bfunction\b|^[A-Z_0-9]+$|^/|\.(js|css|png|svg|json|html|ttf|exe|dll|zip)\b|^https?:|^data:|^\w+\(|[{};]$")


def text_like(s: str) -> bool:
    s = s.strip()
    if COUNT_PHRASE.fullmatch(s):
        return True  # "1 tab created", "{0} stats"
    if len(s) < 2 or len(s) > 2000 or not WORD.search(s):
        return False
    if any(token in s for token in CODE_TOKENS) or CODE_SHAPES.search(s):
        return False
    if CODEY.search(s) and ' ' not in s:
        return False
    words = s.split()
    if all(re.fullmatch(r"[a-z0-9_-]+", w) for w in words) and any(re.search(r"[-_0-9]", w) for w in words):
        return False  # css classes / identifiers (plain lowercase phrases are text)
    if re.search(r"[<>]", s) or s.startswith(('epochpact.', 'data-', 'aria-')):
        return False
    return any(c.isupper() for c in s[:1]) or ' ' in s or s[:1].isupper()


def normalise(s: str) -> str:
    return re.sub(r"\s+", " ", s).strip()


def unescape(text: str) -> str:
    # One pass, so "\\reports" stays a backslash and "reports"; line breaks
    # collapse like the page's own whitespace.
    return re.sub(r"\\([nrt\\'\"`])", lambda m: ' ' if m.group(1) in 'nrt' else m.group(1), text)


MARKUP_VALUE = re.compile(r"<|\.join\(|\w*(?:html|Html|HTML|button|Button|badge|Badge|icon|Icon|card|Card|chip|Chip|panel|Panel|pane|Pane|render|Render|options|Options|row|Row|list|List|table|Table|Section|section)\w*")


def markup_value(expression: str) -> bool:
    """A ${..} that builds markup rather than a value (a count is a value)."""
    return bool(MARKUP_VALUE.search(expression)) and not re.search(r"\.length\b|\.size\b|toLocaleString|toFixed|\bfmt\(|\bnumber\(", expression)


def template_parts(body: str):
    """Static text of a template literal between tags; ${..} -> {n}."""
    body = unescape(body)
    # Replace nested expressions with placeholders, then split at tags.
    out, i, buf, expressions = [], 0, '', []
    while i < len(body):
        if body.startswith('${', i):
            j, level = i + 2, 1
            while j < len(body) and level:
                level += body[j] == '{'
                level -= body[j] == '}'
                j += 1
            expressions.append(body[i + 2:j - 1])
            buf += '\x00'
            i = j
            continue
        buf += body[i]
        i += 1
    seen = 0
    for index, chunk in enumerate(re.split(r"(<[^>]*>)", buf)):
        if index % 2:  # a tag: its ${..} (attributes) are not text
            seen += chunk.count('\x00')
            continue
        pieces = chunk.split('\x00')
        values = expressions[seen:seen + len(pieces) - 1]
        seen += len(pieces) - 1
        if not any(text_like(p) for p in pieces) and not COUNT_PHRASE.fullmatch(normalise('{0}'.join(pieces))):
            continue
        # Beside markup, a ${..} at either end that builds markup (an icon, a
        # badge, a list) is not part of the text; a value ("Rank ${n}") is.
        while len(pieces) > 1 and not pieces[0].strip() and markup_value(values[0]):
            pieces.pop(0)
            values.pop(0)
        while len(pieces) > 1 and not pieces[-1].strip() and markup_value(values[-1]):
            pieces.pop()
            values.pop()
        text = normalise('\x00'.join(pieces))
        text = renumber(text.replace('\x00', '{0}'))
        if (COUNT_PHRASE.fullmatch(text) or text_like(re.sub(r"\{\d+\}", '', text))) and len(text) >= 2:
            out.append(text)
    return out


def renumber(text: str) -> str:
    counter = iter(range(100))
    return re.sub(r"\{\d+\}", lambda m: '{%d}' % next(counter), text)


REGEX_BEFORE = set('(,=:[!&|?{};+-*%<>~^')


def js_literals(source: str):
    """Template literal bodies and quoted strings, in source order.

    Walks the code so quotes and backticks inside strings, comments, regular
    expressions and ${..} expressions (including nested templates) do not
    pair up with the wrong partner."""
    templates, strings, n = [], [], len(source)

    def string(i):
        quote, j = source[i], i + 1
        while j < n and source[j] != quote and source[j] != '\n':
            j += 2 if source[j] == '\\' else 1
        strings.append(source[i + 1:j])
        return j + 1

    def regex(i):
        j, in_class = i + 1, False
        while j < n and source[j] != '\n':
            c = source[j]
            if c == '\\':
                j += 2
                continue
            if c == '[':
                in_class = True
            elif c == ']':
                in_class = False
            elif c == '/' and not in_class:
                j += 1
                break
            j += 1
        while j < n and source[j].isalpha():
            j += 1
        return j

    def template(i):
        j = i + 1
        while j < n and source[j] != '`':
            if source[j] == '\\':
                j += 2
            elif source.startswith('${', j):
                j = code(j + 2, closing=True)
            else:
                j += 1
        templates.append(source[i + 1:j])
        return j + 1

    def code(i, closing=False):
        depth, previous = 0, ';'
        while i < n:
            c = source[i]
            if c in '\'"':
                i, previous = string(i), 'a'
            elif c == '`':
                i, previous = template(i), 'a'
            elif source.startswith('//', i):
                end = source.find('\n', i)
                i = n if end < 0 else end
            elif source.startswith('/*', i):
                end = source.find('*/', i + 2)
                i = n if end < 0 else end + 2
            elif c == '/' and (previous in REGEX_BEFORE or re.search(r"\b(?:return|typeof|case)\s*$", source[max(0, i - 12):i])):
                i, previous = regex(i), 'a'
            else:
                if c == '{':
                    depth += 1
                elif c == '}':
                    if closing and depth == 0:
                        return i + 1
                    depth -= 1
                if not c.isspace():
                    previous = c
                i += 1
        return i

    code(0)
    return templates, strings


def attribute_texts(source: str):
    return [normalise(m.group(1)) for m in re.finditer(r'(?:title|placeholder|aria-label|alt)="([^"]+)"', source) if text_like(m.group(1))]


def scan_js(source: str):
    found = []
    templates, strings = js_literals(source)
    for body in templates:
        found += template_parts(body) + attribute_texts(unescape(body))
    for raw in strings:
        s = unescape(raw)
        if re.search(r"<[a-z/][^>]*>", s):
            found += template_parts(raw) + attribute_texts(s)  # markup in a quoted string
        # Short capitalised words are labels too ("On", "Off", "All").
        elif text_like(s) and (' ' in s.strip() or s[:1].isupper() and len(s) > 3 or re.fullmatch(r"[A-Z][a-z]{1,2}", s)):
            found.append(normalise(s))
    return found


def scan_html(source: str):
    found = []
    for m in re.finditer(r">([^<>]+)<", source):
        if text_like(m.group(1)):
            found.append(normalise(m.group(1)))
    for m in re.finditer(r'(?:title|placeholder|aria-label|alt)="([^"]+)"', source):
        if text_like(m.group(1)):
            found.append(normalise(m.group(1)))
    return found


def scan_python(source: str):
    found = []
    literal = r"(f?)(['\"])((?:\\.|(?!\2).)*)\2"
    for m in re.finditer(r"raise \w+\(((?:\s*f?(['\"])(?:\\.|(?!\2).)*\2)+)", source):
        parts = re.findall(literal, m.group(1))  # adjacent literals on several lines are one message
        text = ''.join(re.sub(r"\{[^{}]*\}", '{0}', body) if f else body for f, _, body in parts)
        if any(f for f, _, _ in parts):
            text = renumber(text)
        if text_like(re.sub(r"\{\d+\}", '', text)):
            found.append(normalise(text))
    for m in re.finditer(r'":\s*"([^"]+)",?\n', source):  # English side of message maps
        if text_like(m.group(1)) and m.group(1)[:1].isupper():
            found.append(normalise(m.group(1)))
    return found


def collect() -> list[str]:
    texts = []
    for name in PYTHON_SOURCES:
        texts += scan_python((ROOT / name).read_text(encoding='utf-8'))
    for name in SOURCES:
        source = (ROOT / 'ui' / name).read_text(encoding='utf-8')
        texts += scan_html(source) if name.endswith('.html') else scan_js(source)
    locale = json.loads((ROOT / 'ui/locale-en.json').read_text(encoding='utf-8'))
    texts += list(locale.get('groups', {}).values())
    for label, description in locale.get('controls', {}).values():
        texts += [label, description]
    catalog = json.loads((ROOT / 'ui/catalog.json').read_text(encoding='utf-8'))
    for control in catalog.get('controls', []):
        for parameter in control.get('parameters') or []:
            texts += parameter.get('choiceLabels') or []
    seen, out = set(), []
    for t in texts:
        # The page translates the parts around " · " one by one, so a text never
        # starts or ends with that separator.
        t = re.sub(r"^·\s*|\s*·$", '', normalise(t))
        if t and t not in seen and (COUNT_PHRASE.fullmatch(t) or text_like(re.sub(r"\{\d+\}", '', t))):
            seen.add(t)
            out.append(t)
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    texts = collect()
    data = json.dumps(texts, ensure_ascii=False, indent=0) + '\n'
    if args.check:
        current = OUTPUT.read_text(encoding='utf-8') if OUTPUT.exists() else ''
        if current != data:
            print('ui/i18n-source.json is stale: run py -3 tools/extract_ui_text.py')
            return 1
        print(f'{len(texts)} texts, up to date')
        return 0
    OUTPUT.write_text(data, encoding='utf-8', newline='\n')
    print(f'{len(texts)} texts written to {OUTPUT.relative_to(ROOT)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
