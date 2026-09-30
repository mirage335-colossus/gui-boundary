#!/usr/bin/env python3
"""Loopback host for independent browser sessions of the C++ example.

No third-party packages. Each tab owns a subprocess and random protocol epoch;
its unguessable token and exact Origin check protect all mutating endpoints.
Sessions expire after 15 idle minutes and shutdown kills every owned child.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import select
import subprocess
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

MAX_INPUT = 1024 * 1024
MAX_OUTPUT = 96 * 1024 * 1024
ASSETS = Path(__file__).resolve().parent


class Session:
    def __init__(self, executable):
        self.epoch = secrets.token_urlsafe(24)
        self.lock = threading.Lock()
        self.used = time.monotonic()
        self.process = subprocess.Popen([str(executable), self.epoch], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        self.buffer = bytearray()
        try:
            self.state = self.read()
        except Exception:
            self.close()
            raise

    def read(self):
        deadline = time.monotonic() + 15
        while b"\n" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
                raise TimeoutError("Backend response timed out")
            chunk = os.read(self.process.stdout.fileno(), 65536)
            if not chunk:
                raise RuntimeError("Backend exited")
            self.buffer.extend(chunk)
            if len(self.buffer) > MAX_OUTPUT:
                raise ValueError("Backend response exceeds output limit")
        line, _, remaining = self.buffer.partition(b"\n")
        self.buffer = bytearray(remaining)
        json.loads(line)  # Validate the subprocess protocol before forwarding it.
        return bytes(line)

    def exchange(self, data):
        with self.lock:
            self.used = time.monotonic()
            payload = memoryview(data + b"\n")
            while payload:
                written = self.process.stdin.write(payload)
                if not written:
                    raise RuntimeError("Backend input pipe closed")
                payload = payload[written:]
            self.state = self.read()
            return self.state

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process.stdin.close()
        self.process.stdout.close()


class Host(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, executable, wasm_dir=None):
        self.executable = Path(executable).resolve() if executable else None
        self.wasm_dir = Path(wasm_dir).resolve() if wasm_dir else None
        self.sessions = {}
        self.sessions_lock = threading.Lock()
        super().__init__(address, Handler)

    @property
    def authority(self):
        return f"127.0.0.1:{self.server_port}"

    def server_close(self):
        super().server_close()
        with self.sessions_lock:
            sessions, self.sessions = self.sessions, {}
        for session in sessions.values():
            session.close()

    def service_actions(self):
        with self.sessions_lock:
            expired = [token for token, session in self.sessions.items() if time.monotonic() - session.used > 900]
            sessions = [self.sessions.pop(token) for token in expired]
        for session in sessions:
            session.close()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def reply(self, code, data, kind="application/json"):
        if isinstance(data, str):
            data = data.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self' 'wasm-unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(data)

    def check_host(self):
        if self.headers.get("Host") != self.server.authority:
            self.reply(403, '{"error":"Unexpected Host"}')
            return False
        return True

    def do_GET(self):
        if not self.check_host():
            return
        path = urlsplit(self.path).path
        known = {"/": ("index.html", "text/html; charset=utf-8"),
                 "/index.html": ("index.html", "text/html; charset=utf-8"),
                 "/renderer.mjs": ("renderer.mjs", "text/javascript; charset=utf-8"),
                 "/style.css": ("style.css", "text/css; charset=utf-8"),
                 "/boot.mjs": ("boot.mjs", "text/javascript; charset=utf-8")}
        if path in known:
            name, kind = known[path]
            return self.reply(200, (ASSETS / name).read_bytes(), kind)
        if self.server.wasm_dir and path in ("/gui_web_wasm.js", "/gui_web_wasm.wasm"):
            target = self.server.wasm_dir / path[1:]
            if target.is_file():
                return self.reply(200, target.read_bytes(), "application/wasm" if path.endswith(".wasm") else "text/javascript")
        self.reply(404, '{"error":"Not found"}')

    def do_POST(self):
        if not self.check_host():
            return
        if self.headers.get("Origin") != "http://" + self.server.authority:
            return self.reply(403, '{"error":"Unexpected Origin"}')
        if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
            return self.reply(415, '{"error":"JSON required"}')
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_INPUT:
                return self.reply(413, '{"error":"Message exceeds input limit"}')
            self.connection.settimeout(15)
            data = self.rfile.read(length)
            def object_fields(pairs):
                result = {}
                for key, value in pairs:
                    if key in result:
                        raise ValueError("Duplicate JSON object key")
                    result[key] = value
                return result
            payload = json.loads(data, object_pairs_hook=object_fields)
            if not isinstance(payload, dict):
                raise ValueError("Object required")
            # Bridge framing is one JSON document per line, regardless of how
            # the HTTP client formatted the submitted JSON document.
            data = json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
            path = urlsplit(self.path).path
            if path == "/api/session":
                if not self.server.executable:
                    return self.reply(404, '{"error":"Only Wasm hosting is configured"}')
                with self.server.sessions_lock:
                    expired = [token for token, session in self.server.sessions.items() if time.monotonic() - session.used > 900]
                    for token in expired:
                        self.server.sessions.pop(token).close()
                    if len(self.server.sessions) >= 16:
                        return self.reply(429, '{"error":"Session limit reached; restart host to clear inactive tabs"}')
                    session = Session(self.server.executable)
                    token = secrets.token_urlsafe(32)
                    self.server.sessions[token] = session
                return self.reply(200, json.dumps({"token": token, "state": json.loads(session.state)}))
            if path in ("/api/event", "/api/release"):
                token = self.headers.get("X-Gui-Token", "")
                with self.server.sessions_lock:
                    session = self.server.sessions.get(token)
                if session is None:
                    return self.reply(403, '{"error":"Unknown session token"}')
                if path == "/api/release":
                    with self.server.sessions_lock:
                        self.server.sessions.pop(token, None)
                    with session.lock:
                        session.close()
                    return self.reply(200, '{"released":true}')
                return self.reply(200, session.exchange(data))
            self.reply(404, '{"error":"Not found"}')
        except (ValueError, OSError, RuntimeError, TimeoutError, RecursionError) as error:
            self.reply(400, json.dumps({"error": str(error)}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", help="Path to gui_web_demo")
    parser.add_argument("--wasm-dir", help="Directory containing gui_web_wasm.js and gui_web_wasm.wasm")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not args.executable and not args.wasm_dir:
        parser.error("Provide --executable, --wasm-dir, or both")
    host = Host(("127.0.0.1", args.port), args.executable, args.wasm_dir)
    print(f"Browser example: http://{host.authority}/" + ("?mode=wasm" if not args.executable else ""), flush=True)
    try:
        host.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        host.server_close()


if __name__ == "__main__":
    main()
