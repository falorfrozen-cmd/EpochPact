"""A transient Windows reply-file lock must never resend a game mutation."""
import builtins
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import datetime
import multiprocessing
import threading
import time
import subprocess

from tools import le_session as le


def concurrent_sender(game, command, queue):
    le.GAME = Path(game)
    try:
        queue.put((command, le.send(command, timeout=5)))
    except Exception as exc:
        queue.put((command, str(exc)))


class ProcessSerializationTests(unittest.TestCase):
    def test_two_real_processes_receive_only_their_own_replies(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc = Path(directory) / 'EpochPact/ipc'
            ipc.mkdir(parents=True)
            (ipc / 'protocol.json').write_text(json.dumps({'version': 2, 'pid': os.getpid()}))
            seen = []
            stopped = threading.Event()
            def server():
                while not stopped.is_set():
                    try:
                        packet = (ipc / 'cmd.txt').read_text()
                        (ipc / 'cmd.txt').unlink()
                    except OSError:
                        time.sleep(.002)
                        continue
                    nonce, command = packet.strip().split(' ', 1)
                    seen.append(command)
                    time.sleep(.08)
                    temp = ipc / 'reply.tmp'
                    temp.write_text(json.dumps({'nonce': nonce[1:], 'reply': 'result:' + command}))
                    temp.replace(ipc / 'reply.json')
            thread = threading.Thread(target=server, daemon=True)
            thread.start()
            queue = multiprocessing.Queue()
            children = [multiprocessing.Process(target=concurrent_sender, args=(directory, name, queue)) for name in ('first', 'second')]
            try:
                for child in children: child.start()
                actual = dict(queue.get(timeout=8) for _ in children)
                for child in children:
                    child.join(timeout=8)
                    self.assertEqual(child.exitcode, 0)
                self.assertEqual(actual, {'first': 'result:first', 'second': 'result:second'})
                self.assertCountEqual(seen, ['first', 'second'])
            finally:
                for child in children:
                    if child.is_alive(): child.terminate(); child.join()
                stopped.set(); thread.join(timeout=2)
                queue.close()


class BackupTests(unittest.TestCase):
    def test_retention_keeps_twenty_managed_and_preserves_legacy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'legacy').mkdir()
            for i in range(25):
                path = root / str(i); path.mkdir()
                marker = path / '.epochpact-retention'; marker.write_text('1\n')
                os.utime(marker, ns=((i + 1) * 1_000_000_000, (i + 1) * 1_000_000_000))
            le.prune_session_backups(root, keep=20, newest=root / '24')
            self.assertEqual(len(list(root.iterdir())), 21)
            self.assertTrue((root / 'legacy').is_dir())
            self.assertFalse((root / '4').exists())
            self.assertTrue((root / '5').exists())

    def test_junction_and_its_target_are_never_pruned(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / 'backups'; root.mkdir()
            outside = Path(directory) / 'owner'; outside.mkdir()
            (outside / '.epochpact-retention').write_text('1\n')
            (outside / 'save').write_text('protected')
            link = root / 'link'
            subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(outside)], check=True, capture_output=True)
            try:
                newest = root / 'new'; newest.mkdir()
                (newest / '.epochpact-retention').write_text('1\n')
                le.prune_session_backups(root, keep=1, newest=newest)
                self.assertTrue(link.exists())
                self.assertEqual((outside / 'save').read_text(), 'protected')
                with self.assertRaises(ValueError):
                    le.prune_session_backups(link, keep=1, newest=outside)
            finally:
                link.rmdir()

    def test_cleanup_failure_does_not_discard_successful_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); saves = root / 'saves'; saves.mkdir()
            (saves / 'character').write_text('save')
            with patch.object(le, 'SAVES', saves), patch.object(le, 'OUT', root / 'out'), \
                 patch.object(le, 'prune_session_backups', side_effect=PermissionError('indexer')):
                backup = le.backup_saves()
            self.assertEqual((backup / 'character').read_text(), 'save')

    def test_two_launch_backups_in_one_clock_tick_preserve_both_copies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            saves = root / 'saves'
            saves.mkdir()
            (saves / 'character').write_bytes(b'first')
            instant = datetime.datetime(2026, 10, 8, 12, 0, 0)
            with patch.object(le, 'SAVES', saves), patch.object(le, 'OUT', root / 'out'), \
                 patch.object(le._dt, 'datetime') as clock:
                clock.now.return_value = instant
                first = le.backup_saves()
                (saves / 'character').write_bytes(b'second')
                second = le.backup_saves()
            self.assertNotEqual(first, second)
            self.assertEqual((first / 'character').read_bytes(), b'first')
            self.assertEqual((second / 'character').read_bytes(), b'second')


class PacketTests(unittest.TestCase):
    def test_publication_lock_retries_failed_rename_only(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc = Path(directory)
            real_replace = os.replace
            attempts = 0
            def publish(src, dst):
                nonlocal attempts
                attempts += 1
                if attempts == 1:
                    raise PermissionError('transient reader lock; no packet submitted')
                real_replace(src, dst)
                nonce = (ipc / 'cmd.txt').read_text().split()[0][1:]
                (ipc / 'reply.json').write_text(json.dumps({'nonce':nonce,'reply':'actual'}))
            with patch.object(le.os, 'replace', side_effect=publish):
                self.assertEqual(le.send_packet(ipc, 'questscomplete 7', 1), 'actual')
                self.assertEqual(attempts, 2)

    def test_atomic_nonce_reply_ignores_old_reply_and_submits_once(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc = Path(directory)
            (ipc / 'reply.json').write_text(json.dumps({'nonce':'old', 'reply':'wrong'}))
            real_replace = os.replace
            def publish(src, dst):
                real_replace(src, dst)
                nonce = (ipc / 'cmd.txt').read_text().split()[0][1:]
                (ipc / 'reply.json').write_text(json.dumps({'nonce':nonce,'reply':'actual\nmultiline'}))
            with patch.object(le.os, 'replace', side_effect=publish) as submitted:
                self.assertEqual(le.send_packet(ipc, 'statraw 21 0 0 0', 1), 'actual\nmultiline')
                self.assertEqual(submitted.call_count, 1)

    def test_stale_reply_timeout_never_resends_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc = Path(directory)
            (ipc / 'reply.json').write_text('{"nonce":"old","reply":"success"}')
            with patch.object(le.os, 'replace', wraps=os.replace) as submitted:
                self.assertIsNone(le.send_packet(ipc, 'questscomplete 7', .03))
                self.assertEqual(submitted.call_count, 1)

    def test_dead_protocol_pid_falls_back_to_legacy(self):
        with tempfile.TemporaryDirectory() as directory:
            ipc = Path(directory)
            (ipc / 'protocol.json').write_text('{"version":2,"pid":123}')
            with patch.object(le.kernel32, 'OpenProcess', return_value=0):
                self.assertFalse(le.modern_ipc(ipc))


class GameLocationTests(unittest.TestCase):
    def test_explicit_path_takes_precedence_and_invalid_path_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory); (path / le.EXE).touch()
            self.assertEqual(le.resolve_game_dir({'EPOCHPACT_GAME_DIR': str(path)}), path.resolve())
            with self.assertRaises(FileNotFoundError):
                le.resolve_game_dir({'EPOCHPACT_GAME_DIR': str(path / 'missing')})

    def test_current_and_legacy_steam_library_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); steam = root / 'Steam'; (steam / 'steamapps').mkdir(parents=True)
            library = root / 'Other disk'; game = library / 'steamapps/common/Last Epoch'; game.mkdir(parents=True)
            (game / le.EXE).touch()
            escaped = str(library).replace('\\', '\\\\')
            for vdf in ('"libraryfolders" { "1" { "path" "' + escaped + '" } }',
                        '"libraryfolders" { "1" "' + escaped + '" }'):
                (steam / 'steamapps/libraryfolders.vdf').write_text(vdf)
                self.assertEqual(le.resolve_game_dir({}, steam), game.resolve())

    def test_default_library_works_without_vdf(self):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / 'steamapps/common/Last Epoch'; game.mkdir(parents=True)
            (game / le.EXE).touch()
            self.assertEqual(le.resolve_game_dir({}, directory), game.resolve())

    def test_malformed_library_file_does_not_break_default_library_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            steam = Path(directory); game = steam / 'steamapps/common/Last Epoch'; game.mkdir(parents=True)
            (game / le.EXE).touch()
            (steam / 'steamapps/libraryfolders.vdf').write_bytes(b'\xff\xfeinvalid')
            self.assertEqual(le.resolve_game_dir({}, steam), game.resolve())


class BuildArtifactTests(unittest.TestCase):
    def test_player_install_cannot_select_research_or_tampered_output(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory); player = build / 'player'; player.mkdir()
            core = player / 'EpochPact.Core.dll'; core.write_bytes(b'player')
            (build / 'EpochPact.Core.dll').write_bytes(b'research')
            metadata = player / 'build-info.json'
            metadata.write_text(json.dumps({'flavor': 'player', 'sha256': le.hashlib.sha256(b'player').hexdigest()}))
            with patch.object(le, 'BUILD', build):
                self.assertEqual(le.build_artifact()[0], core)
                core.write_bytes(b'tampered')
                with self.assertRaises(ValueError): le.build_artifact()
                core.write_bytes(b'player')
                metadata.write_text(json.dumps({'flavor': 'research', 'sha256': le.hashlib.sha256(b'player').hexdigest()}))
                with self.assertRaises(ValueError): le.build_artifact()


class ReplyFileLockTests(unittest.TestCase):
    def exercise(self, denied_reads, *, timeout=False):
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory)
            ipc = game / "EpochPact" / "ipc"
            ipc.mkdir(parents=True)
            out = ipc / "out.txt"
            out.write_bytes(b"")
            real_replace = os.replace
            attempts = 0

            def publish(source, target):
                real_replace(source, target)
                out.write_bytes(b"> cofreputation 6 10\r\n{\"ok\":true}\r\n")

            def limited_open(path, mode="r", *args, **kwargs):
                nonlocal attempts
                if Path(path) == out and mode == "rb":
                    attempts += 1
                    if attempts in denied_reads:
                        raise PermissionError("simulated native append lock")
                return builtins.open(path, mode, *args, **kwargs)

            with patch.object(le, "GAME", game), \
                 patch.object(le.os, "replace", side_effect=publish) as submitted, \
                 patch.object(le, "open", limited_open, create=True), \
                 patch.object(le.time, "sleep"):
                if timeout:
                    with patch.object(le.time, "time", side_effect=(0, 0, 0, 11)):
                        reply = le.send("cofreputation 6 10", timeout=10)
                else:
                    reply = le.send("cofreputation 6 10", timeout=10)
            self.assertEqual(submitted.call_count, 1)
            self.assertEqual((ipc / "cmd.txt").read_text(), "cofreputation 6 10\n")
            return reply

    def test_initial_read_lock_recovers_without_resending(self):
        self.assertEqual(self.exercise({1}), '{"ok":true}')

    def test_settled_read_lock_recovers_without_resending(self):
        self.assertEqual(self.exercise({2}), '{"ok":true}')

    def test_lock_keeps_original_deadline_and_submits_once(self):
        self.assertIsNone(self.exercise({1, 2}, timeout=True))


if __name__ == "__main__":
    unittest.main()
