import io
import json
import unittest

from tools import update_check as uc


class FakeResponse(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class UpdateCheckTests(unittest.TestCase):
    def test_versions_order_by_number_and_stage(self):
        self.assertLess(uc.parse('0.1.2-dev.9'), uc.parse('v0.1.2-dev.19'))
        self.assertLess(uc.parse('0.1.1-alpha.5'), uc.parse('0.1.2-dev.1'))
        self.assertLess(uc.parse('0.1.2-dev.30'), uc.parse('0.1.2'))
        self.assertIsNone(uc.parse('latest'))

    def test_compare_reports_newer_release_and_keeps_links_on_the_repository(self):
        r = uc.compare('0.1.2-dev.19', 'v0.1.2-dev.20', 'https://github.com/falorfrozen-cmd/EpochPact/releases/tag/v0.1.2-dev.20')
        self.assertTrue(r['updateAvailable'])
        self.assertEqual(r['latest'], '0.1.2-dev.20')
        self.assertTrue(r['url'].endswith('/tag/v0.1.2-dev.20'))
        self.assertFalse(uc.compare('0.1.2-dev.19', 'v0.1.2-dev.19')['updateAvailable'])
        self.assertFalse(uc.compare('0.1.2-dev.19', 'v0.1.2-dev.18')['updateAvailable'])
        self.assertEqual(uc.compare('0.1.2-dev.19', 'v0.1.2-dev.20', 'https://evil.example/x')['url'], uc.RELEASES)

    def test_check_reads_one_release_and_fails_quietly(self):
        seen = []

        def opener(request, timeout):
            seen.append((request.full_url, timeout, request.get_header('User-agent')))
            return FakeResponse(json.dumps({'tag_name': 'v0.1.2-dev.21', 'html_url': uc.RELEASES + '/tag/v0.1.2-dev.21'}).encode())
        r = uc.check('0.1.2-dev.19', opener=opener)
        self.assertTrue(r['ok'] and r['updateAvailable'])
        self.assertEqual(seen[0][0], uc.LATEST)
        self.assertEqual(seen[0][2], 'EpochPact/0.1.2-dev.19')

        def offline(request, timeout):
            raise OSError('no network')
        self.assertFalse(uc.check('0.1.2-dev.19', opener=offline)['ok'])
        self.assertFalse(uc.check('0.1.2-dev.19', opener=lambda r, timeout: FakeResponse(b'[]'))['ok'])

    def test_update_endpoint_starts_one_background_check(self):
        import epochpact_ui
        from unittest.mock import patch
        from tools import app_update
        from tools.ui_bridge import UiBridge
        calls = []
        with patch.object(app_update, 'check', side_effect=lambda version: calls.append(version) or {'ok': True, 'updateAvailable': False}):
            app = epochpact_ui.create_app(UiBridge(preview=True))
            client = app.test_client()
            first = client.get('/api/update').get_json()
            for _ in range(50):
                if calls:
                    break
                import time
                time.sleep(0.02)
            client.get('/api/update')
        self.assertEqual(len(calls), 1)
        self.assertTrue(first.get('pending') or first.get('ok'))


if __name__ == '__main__':
    unittest.main()
