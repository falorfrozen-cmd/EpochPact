import json
import unittest

from tools import extract_ui_text, i18n_check


class SourceListTests(unittest.TestCase):
    def test_source_list_matches_the_interface(self):
        """A new interface text must be added to ui/i18n-source.json (and translated)."""
        stored = json.loads((extract_ui_text.OUTPUT).read_text(encoding='utf-8'))
        self.assertEqual(stored, extract_ui_text.collect(), 'run py -3 tools/extract_ui_text.py, then translate the new texts')

    def test_templates_number_their_parts(self):
        self.assertEqual(extract_ui_text.template_parts('${n} settings applied in order.'), ['{0} settings applied in order.'])
        self.assertEqual(extract_ui_text.template_parts('Move ${a} to ${b}'), ['Move {0} to {1}'])
        self.assertEqual(extract_ui_text.template_parts('<b>${x}</b>'), [])
        # Beside markup an outer ${..} may be an element (an icon, a badge): only
        # the text is kept; a count in front is matched by the page at run time.
        self.assertEqual(extract_ui_text.template_parts('<p>${icon} Apply ${n} settings</p>'), ['Apply {0} settings'])
        # Escapes are read in one pass: "\\reports" is a backslash and "reports".
        self.assertEqual(extract_ui_text.template_parts(r'Saved in Last Epoch\\EpochPact\\reports.'), ['Saved in Last Epoch\\EpochPact\\reports.'])

    def test_code_is_not_offered_for_translation(self):
        for code in ('cache.loot_read', 'const x=1', "x=>x+1", 'data-setting', 'epochpact.theme', 'button-primary', 'style.css',
                     ':""} step="{0}" {1}', ", ]/.test(s)?'"):
            self.assertFalse(extract_ui_text.text_like(code), code)
        for text in ('Apply changes', 'Pickup mode', 'Refresh'):
            self.assertTrue(extract_ui_text.text_like(text), text)


class CheckTests(unittest.TestCase):
    def test_missing_and_placeholder_mismatches_are_reported(self):
        found = i18n_check.problems(['A {0} b', 'C', 'D'], {'A {0} b': 'x {1}', 'C': ' '})
        self.assertEqual(found['missing'], ['C', 'D'])
        self.assertEqual(found['placeholders'], ['A {0} b'])

    def test_every_language_is_complete(self):
        source = json.loads(extract_ui_text.OUTPUT.read_text(encoding='utf-8'))
        for language in i18n_check.LANGUAGES:
            found = i18n_check.problems(source, i18n_check.load(language))
            self.assertEqual(found, {'missing': [], 'placeholders': []}, language)


if __name__ == '__main__':
    unittest.main()
