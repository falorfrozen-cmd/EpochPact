"""EpochPact desktop/web UI. One worker serializes every request to the native IPC."""
from __future__ import annotations

import argparse
import contextlib
import io
import json
import logging
from logging.handlers import RotatingFileHandler
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
import secrets
import socket
import threading
from urllib.parse import urlparse
import webbrowser
import sys

from flask import Flask, jsonify, request, send_from_directory
from tools.ui_bridge import ROOT, UiBridge
from tools.ui_language import english_exception, english_result
from tools.app_paths import runtime_root, user_root
from tools.player_setup import PlayerSetup, DesktopDialogs


def create_app(bridge=None, *, developer=False, operation_log=None, setup=None):
    bridge = bridge or UiBridge()
    app = Flask(__name__, static_folder=None)
    close_state = {"closing": False, "closed": False, "future": None}
    app.config["MAX_CONTENT_LENGTH"] = 128 * 1024
    token = secrets.token_urlsafe(32)
    worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="epochpact-ipc")
    jobs = OrderedDict()
    jobs_lock = threading.Lock()
    app.extensions.update(epoch_bridge=bridge, epoch_worker=worker, epoch_jobs=jobs, epoch_token=token)
    journal = None
    if operation_log is not None:
        operation_log.parent.mkdir(parents=True, exist_ok=True)
        journal = logging.getLogger(f"epochpact.operations.{id(app)}")
        journal.setLevel(logging.INFO)
        journal.propagate = False
        handler = RotatingFileHandler(operation_log, maxBytes=2 * 1024 * 1024, backupCount=2, encoding="utf-8")
        handler.setFormatter(logging.Formatter("%(asctime)s %(message)s"))
        journal.addHandler(handler)
        app.extensions['epoch_journal_handler'] = handler

    @app.before_request
    def guard():
        if request.host.split(":")[0] not in ("127.0.0.1", "localhost"):
            return jsonify(ok=False, error="Only local connections are accepted."), 403
        if request.method == "POST":
            origin = request.headers.get("Origin")
            if origin and urlparse(origin).netloc != request.host:
                return jsonify(ok=False, error="Cross-origin requests are not allowed."), 403
            if request.headers.get("X-Epoch-Token") != token:
                return jsonify(ok=False, error="Invalid interface session."), 403

    @app.after_request
    def headers(response):
        response.headers["Cache-Control"] = "no-store" if request.path.startswith("/api/") else "no-cache"
        response.headers["X-Content-Type-Options"] = "nosniff"
        response.headers["Content-Security-Policy"] = "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; script-src 'self'; font-src 'self'; connect-src 'self'; frame-ancestors 'none'"
        return response

    @app.get("/")
    def index():
        return send_from_directory(ROOT / "ui", "index.html")

    @app.get("/ui/<path:path>")
    def assets(path):
        if path in ("app.js", "style.css", "stat-model.js", "session.js", "collection-ui.js", "collection.css", "launcher.js"):
            return send_from_directory(ROOT / "ui", path)
        if path.startswith("assets/"):
            return send_from_directory(ROOT / "ui/assets", path.removeprefix("assets/"))
        return jsonify(ok=False), 404

    @app.get("/api/bootstrap")
    def bootstrap():
        return jsonify(app_id="epochpact-ui-v1", setup_version=1 if setup else 0, preview=bridge.preview, developer=developer,
                       token=token, catalog=bridge.public_catalog(), launcher=setup.status() if setup else None)

    def perform(payload, revision):
        try:
            if setup and revision != setup.revision:
                raise RuntimeError('The game location changed. Refresh before trying this action again.')
            if payload["type"] == "launcher":
                result = setup.execute(payload['action'], payload.get('args', {}))
            elif payload["type"] == "connection":
                launcher = setup.status() if setup else None
                if setup and not launcher['installed']:
                    result = {'ok': True, 'connected': False, 'offline': False, 'launcher': launcher,
                              'error': 'Select the game and install the mod in Game setup.', 'preview': False, 'history': []}
                else:
                    result = bridge.connection()
                    # Recovery is useful player information. A Settings-first
                    # startup must not wait for a save action to show this history.
                    result = {**result, "history": bridge.available_history()}
                    if setup:
                        result['launcher'] = launcher
            elif payload["type"] == "reconcile":
                if setup:
                    setup.require_ready()
                result = bridge.reconcile()
            else:
                if setup:
                    setup.require_ready()
                result = bridge.execute(payload["id"], payload.get("args", {}), payload.get("operation", "set"))
            result = english_result(result)
        except Exception as exc:
            app.logger.info("UI operation failed: %s", exc)
            result = {"ok": False, "error": english_exception(exc)}
        if journal is not None:
            # Technical replies stay in local bounded logs, away from the
            # player interface. Never log HTTP headers or the session token.
            journal.info("%s", json.dumps({"request": payload, "result": result}, ensure_ascii=False, default=str))
        return result

    @app.post("/api/jobs")
    def submit():
        if close_state["closing"]:
            return jsonify(ok=False, error="EpochPact is restoring temporary settings before closing."), 503
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict) or payload.get("type") not in ("connection", "control", "reconcile", "launcher"):
            return jsonify(ok=False, error="Invalid request."), 400
        if payload['type'] == 'launcher' and (setup is None or payload.get('action') not in ('status', 'select', 'install', 'elevated_install', 'launch') or
                                              not isinstance(payload.get('args', {}), dict)):
            return jsonify(ok=False, error="Invalid game setup action."), 400
        if payload["type"] == "control" and payload.get("id") not in bridge.controls:
            return jsonify(ok=False, error="Invalid control."), 400
        with jobs_lock:
            if close_state["closing"]:
                return jsonify(ok=False, error="EpochPact is restoring temporary settings before closing."), 503
            if sum(not f.done() for f in jobs.values()) >= 16:
                return jsonify(ok=False, error="The operation queue is full."), 429
            while len(jobs) >= 128:
                oldest = next(iter(jobs))
                if not jobs[oldest].done():
                    return jsonify(ok=False, error="The operation queue is full."), 429
                jobs.pop(oldest)
            job_id = secrets.token_hex(12)
            jobs[job_id] = worker.submit(perform, payload, setup.revision if setup else 0)
        return jsonify(ok=True, job=job_id), 202

    @app.get("/api/jobs/<job_id>")
    def poll(job_id):
        with jobs_lock:
            future = jobs.get(job_id)
        if future is None:
            return jsonify(ok=False, error="Operation not found."), 404
        return jsonify(ok=True, done=future.done(), result=future.result() if future.done() else None)

    @app.get("/favicon.ico")
    def favicon():
        return "", 204

    def close_session():
        with jobs_lock:
            if close_state["closed"]:
                return {"ok": True, "restored": 0}
            if not close_state["closing"]:
                close_state["closing"] = True
                # Accepted actions complete before cleanup; no abandoned writes.
                close_state["future"] = worker.submit(bridge.restore_session)
            future = close_state["future"]
        try:
            result = future.result()
            if not result.get("ok"):
                raise RuntimeError(result.get("error", "Temporary settings could not be restored."))
            close_state["closed"] = True
            if journal:
                journal.info(json.dumps({"request": {"type": "close"}, "result": result}, ensure_ascii=False))
            return result
        except Exception:
            close_state["closing"] = False
            close_state["future"] = None
            raise

    app.extensions["epoch_close_session"] = close_session
    return app


def interface_server(app, port):
    """Keep this launch on its own backend, even if an older UI owns the port."""
    from werkzeug.serving import ThreadedWSGIServer
    class ExclusiveInterfaceServer(ThreadedWSGIServer):
        allow_reuse_address = False
        allow_reuse_port = False

        def server_bind(self):
            # SO_REUSEADDR on Windows can bind two listeners to the same port,
            # sending this UI's requests to the old backend nondeterministically.
            if sys.platform == 'win32':
                self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            super().server_bind()
    # A fixed adjacent fallback preserves the WebView origin across launches
    # while a legacy development server is still running. Port zero is last.
    candidates = [port] + ([p for p in range(port + 1, min(port + 5, 65536))] + [0] if port else [])
    for candidate in candidates:
        try:
            # Werkzeug prints bind errors before raising SystemExit. A windowed
            # EXE has no stderr, so capture that output rather than crashing.
            with contextlib.redirect_stderr(io.StringIO()):
                server = ExclusiveInterfaceServer('127.0.0.1', candidate, app)
        except (OSError, SystemExit):
            if candidate == candidates[-1]:
                raise RuntimeError('EpochPact could not open a local interface port.')
            continue
        if server.server_port != port and port:
            app.logger.info('Interface port %s is occupied; using %s with this launch\'s own backend.', port, server.server_port)
        return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true", help="No game access; catalog snapshot is clearly marked.")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--developer", action="store_true", help="Show technical diagnostics; hidden in the normal player interface.")
    parser.add_argument("--port", type=int, default=17884)
    args = parser.parse_args()
    if getattr(sys, 'frozen', False) and args.preview:
        parser.error('Preview snapshots are not included in the player package. Use the source preview launcher.')
    if getattr(sys, 'frozen', False) and args.developer:
        parser.error('Developer diagnostics are not available in the player package. Use a source checkout.')
    bridge = UiBridge(preview=args.preview)
    setup = None if args.preview else PlayerSetup(bridge)
    app = create_app(bridge, developer=args.developer, setup=setup,
                     operation_log=None if args.preview else runtime_root() / "live/ui/operations.log")
    server = interface_server(app, args.port)
    url = f"http://127.0.0.1:{server.server_port}"
    print(f"EpochPact {'PREVIEW' if args.preview else 'LIVE'}: {url}", flush=True)
    if args.no_open:
        server.serve_forever()
        return
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    if args.browser:
        webbrowser.open(url)
        thread.join()
    else:
        import webview
        try:
            dialogs = DesktopDialogs()
            dialogs._window = webview.create_window("EpochPact", url, js_api=dialogs, width=1500, height=1000, min_size=(860, 650), maximized=True, background_color="#091310")
            def closing():
                try:
                    if setup and (bridge.temporary_resets or bridge.actor):
                        setup.return_to_game()
                    app.extensions["epoch_close_session"]()
                    return True
                except Exception:
                    app.logger.exception("Temporary settings could not be restored on close")
                    import ctypes
                    ctypes.windll.user32.MessageBoxW(None,
                        "EpochPact could not restore temporary settings. Return to the game and let it finish loading, then try closing EpochPact again. The game has not been closed.",
                        "EpochPact", 0x10)
                    return False
            dialogs._window.events.closing += closing
            webview.start(gui='edgechromium', private_mode=False, storage_path=str(user_root() / 'webview'))
        finally:
            server.shutdown()
            app.extensions["epoch_worker"].shutdown(wait=True, cancel_futures=False)
            server.server_close()


if __name__ == "__main__":
    main()
