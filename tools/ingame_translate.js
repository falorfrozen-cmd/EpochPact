// Translates the in-game window's texts with the desktop interface's own translator (ui/i18n.js),
// so both show the same words. Used by tools/build_ingame_assets.py (.js, not .cjs: node --test
// runs tools/*.cjs, and this one reads its request from stdin).
// stdin: {"tr": ["English text", ...], "ru": [...]}  stdout: {"tr": {"English text": "...", ...}, ...}
'use strict';
const fs = require('fs');
const path = require('path');
const i18n = require(path.join(__dirname, '..', 'ui', 'i18n.js'));

const request = JSON.parse(fs.readFileSync(0, 'utf8'));
const out = {};
for (const [language, texts] of Object.entries(request)) {
  const dictionary = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'ui', `i18n-${language}.json`), 'utf8'));
  const translate = i18n.translator(dictionary, {decimalComma: language === 'tr' || language === 'ru'});
  const table = {};
  for (const text of texts) {
    const result = translate(text);
    if (result !== null && result !== text) table[text] = result;
  }
  out[language] = table;
}
process.stdout.write(JSON.stringify(out));
