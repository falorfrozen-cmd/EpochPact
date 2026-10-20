// What the app's translator (ui/i18n.js) makes of each text, for the in-game translator's test
// (native/tests/ingame_i18n_test.cpp): null where it has no translation.
// stdin: {"tr": ["English text", ...], "ru": [...]}  stdout: {"tr": {"English text": "..." | null}, ...}
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
  for (const text of texts) table[text] = translate(text);
  out[language] = table;
}
process.stdout.write(JSON.stringify(out));
