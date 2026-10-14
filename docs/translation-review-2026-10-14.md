# Translation review (2026-10-14)

A Russian player proposed English and Russian wording for the three auto
pickup settings and pointed out the untranslated seconds unit ("s") and the
semicolon in the interval text. Afterwards the whole Turkish and Russian
dictionaries were reviewed.

## Auto pickup texts

Taken from the proposal: no "nearby" (radius 0 collects every loaded drop),
"Longer intervals reduce system load", "equipment pickup pauses", no "as in
earlier versions", one Russian word for auto pickup (автоподбор). Not taken,
because they describe the game wrongly:

- "according to the loot filter settings": the default Pickup mode **All**
  collects items without the loot filter; the filter counts only in **Filter**
  mode or Quality mode with "Respect the loot filter". The text names Pickup
  mode and Pickup category instead.
- "all drops in the world" at radius 0: only drops the game has loaded in the
  current area.
- A colon before "gold, potions … continue": the second half contrasts with the
  first rather than explaining it; English keeps the semicolon, Russian joins
  with «а».

The Russian section title "Автоматический сбор" stays as the player wrote it.

## Units and decimals

- `tools/extract_ui_text.py` lists unit abbreviations (`UNITS`), so the seconds
  unit after a number field is translated: Turkish "sn", Russian "с".
- `ui/i18n.js`: in Turkish and Russian a template value that is a plain decimal
  number is written with a comma ("0.25–10 s · Default: 0.25" → "0,25–10 с ·
  По умолчанию: 0,25"). Numbers written in a translation itself were changed by
  hand (0,5 · ×1,5 · 1,5× · ×2,5). The number box itself is formatted by
  Windows' regional setting.

## Full review

All 1,538 texts were read in five parts against the English (meaning, grammar,
terms, placeholders, numbers). 95 proposals; 72 applied (Turkish 35, Russian
37), for example:

- Meaning: Turkish said "without restarting" where the English says "after
  restarting" (mixed items); Russian lost the `disabled` file name; Turkish
  "enter nothing" where the keyword `none` must be typed; Russian "Создать
  предмет" lost "custom"; the Russian forge counter counted changes instead of
  settings.
- Grammar: Turkish object suffixes after a name ("{0} öğesini favorilere ekle",
  "{0}: eşyadan kaldır"), Russian nouns missing after adjectives, "по {0} шт.".
- Terms: Russian «» quotes, сессия, снаряжение, Кузница, «Скрыть» rules,
  автоподбор; Turkish İtibar, değiştirici, tome'lar, Hide rules (the game's
  rule type), "Kullanılabilir affix'ler" (available, not current), DPS 10 sn.

Not applied (23): Turkish slogans keep their informal "sen" form, as every
slogan does; the Russian section title above; "son kullandığınız ayarlar" stays
capitalised because Turkish puts it first in its sentence.

## Checks

- Every text translated in both languages, placeholders unchanged
  (`tools/i18n_check.py`); a scan finds no decimal point, straight quotes in
  Russian or untranslated seconds unit left (Turkish "4.000" is a thousands
  separator).
- JavaScript 197 (new: decimal comma and unit), Python 324, native 20 tests pass.
- Compiled package and extracted ZIP: 24/24 and 4/4; Windows Defender found no
  threats (hashes in `docs/security/virustotal-20261014-dev29.json`).
- The Loot & Pickup page was opened in English, Turkish and Russian: "0.25–10 s
  · Default: 0.25", "0,25–10 sn · Varsayılan: 0,25", "0,25–10 с · По умолчанию:
  0,25" and the new descriptions.
