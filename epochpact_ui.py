"""EpochPact desktop/web UI. One worker serializes every request to the native IPC."""
from __future__ import annotations

import argparse
import json
import logging
from logging.handlers import RotatingFileHandler
from collections import OrderedDict
from concurrent.futures import ThreadPoolExecutor
import secrets
import threading
from urllib.parse import urlparse
import webbrowser

from flask import Flask, jsonify, request, send_from_directory
from tools.ui_bridge import ROOT, UiBridge
from tools.ui_language import english_exception, english_result


def create_app(bridge=None, *, developer=False, operation_log=None):
    bridge = bridge or UiBridge()
    app = Flask(__name__, static_folder=None)
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
        if path in ("app.js", "style.css", "stat-model.js", "session.js", "collection-ui.js", "collection.css"):
            return send_from_directory(ROOT / "ui", path)
        if path.startswith("assets/"):
            return send_from_directory(ROOT / "ui/assets", path.removeprefix("assets/"))
        return jsonify(ok=False), 404

    @app.get("/api/bootstrap")
    def bootstrap():
        return jsonify(app_id="epochpact-ui-v1", preview=bridge.preview, developer=developer, token=token, catalog=bridge.public_catalog())

    def perform(payload):
        try:
            if payload["type"] == "connection":
                result = bridge.connection()
                # Recovery is useful player information. A Settings-first
                # startup must not wait for a save action to show this history.
                result = {**result, "history": [dict(item) for item in bridge.history]}
            elif payload["type"] == "reconcile":
                result = bridge.reconcile()
            else:
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
        payload = request.get_json(silent=True)
        if not isinstance(payload, dict) or payload.get("type") not in ("connection", "control", "reconcile"):
            return jsonify(ok=False, error="Invalid request."), 400
        if payload["type"] == "control" and payload.get("id") not in bridge.controls:
            return jsonify(ok=False, error="Invalid control."), 400
        with jobs_lock:
            if sum(not f.done() for f in jobs.values()) >= 16:
                return jsonify(ok=False, error="The operation queue is full."), 429
            while len(jobs) >= 128:
                oldest = next(iter(jobs))
                if not jobs[oldest].done():
                    return jsonify(ok=False, error="The operation queue is full."), 429
                jobs.pop(oldest)
            job_id = secrets.token_hex(12)
            jobs[job_id] = worker.submit(perform, payload)
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

    return app


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true", help="No game access; catalog snapshot is clearly marked.")
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--browser", action="store_true")
    parser.add_argument("--developer", action="store_true", help="Show technical diagnostics; hidden in the normal player interface.")
    parser.add_argument("--port", type=int, default=17884)
    args = parser.parse_args()
    url = f"http://127.0.0.1:{args.port}"
    app = create_app(UiBridge(preview=args.preview), developer=args.developer,
                     operation_log=None if args.preview else ROOT / "live/ui/operations.log")
    from werkzeug.serving import make_server
    try:
        server = make_server("127.0.0.1", args.port, app, threaded=True)
    except (OSError, SystemExit):
        app.extensions['epoch_worker'].shutdown(wait=False, cancel_futures=True)
        if handler := app.extensions.get('epoch_journal_handler'):
            handler.close()
            logging.getLogger(f"epochpact.operations.{id(app)}").removeHandler(handler)
        # Only reuse this application's matching mode, never another local service.
        import json
        from urllib.request import urlopen
        try:
            with urlopen(url + "/api/bootstrap", timeout=3) as reply:
                existing = json.load(reply)
            if existing.get("app_id") != "epochpact-ui-v1" or existing.get("preview") != args.preview or existing.get("developer", False) != args.developer:
                raise RuntimeError("The port is in use by another application or a different preview mode.")
        except Exception as exc:
            raise RuntimeError("The interface port is in use. Choose another --port.") from exc
        if args.no_open:
            return
        if args.browser:
            webbrowser.open(url)
        else:
            import webview
            webview.create_window("EpochPact", url, width=1500, height=1000, min_size=(860, 650), maximized=True, background_color="#091310")
            webview.start()
        return
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
            webview.create_window("EpochPact", url, width=1500, height=1000, min_size=(860, 650), maximized=True, background_color="#091310")
            webview.start()
        finally:
            server.shutdown()
            app.extensions["epoch_worker"].shutdown(wait=False, cancel_futures=True)


if __name__ == "__main__":
    main()
