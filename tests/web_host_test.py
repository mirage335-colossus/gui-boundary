#!/usr/bin/env python3
"""Exercise real HTTP and real independent C++ subprocesses on loopback."""
import importlib.util
import json
from pathlib import Path
import sys
import threading
import urllib.error
import urllib.request

spec = importlib.util.spec_from_file_location("boundary_web_host", Path(__file__).parents[1] / "backends/web/host.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
host = module.Host(("127.0.0.1", 0), sys.argv[1])
thread = threading.Thread(target=host.serve_forever, daemon=True)
thread.start()
url = f"http://{host.authority}"


def request(path, payload=None, token=None, origin=None):
    headers = {"Content-Type": "application/json", "Origin": origin or url}
    if token:
        headers["X-Gui-Token"] = token
    data = payload if isinstance(payload, bytes) else None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(url + path, data, headers)
    try:
        with urllib.request.urlopen(req, timeout=20) as response:
            return response.status, response.read()
    except urllib.error.HTTPError as error:
        return error.code, error.read()


try:
    assert request("/")[0] == 200
    assert request("/../include/gui/contract.hpp")[0] == 404
    assert request("/api/session", {}, origin="https://other.example")[0] == 403
    assert request("/api/event", {"operation": {}}, token="invalid")[0] == 403
    status, first = request("/api/session", {})
    assert status == 200
    first = json.loads(first)
    second = json.loads(request("/api/session", {})[1])
    assert first["token"] != second["token"]
    assert first["state"]["epoch"] != second["state"]["epoch"]
    envelope = {"epoch": first["state"]["epoch"], "seq": "1", "operation": {"type": "resize", "width": 700, "height": 500, "scale": 1}}
    reply = json.loads(request("/api/event", envelope, first["token"])[1])
    assert reply["ack"] == "1" and reply["snapshot"]["width"] == 700
    replay = json.loads(request("/api/event", envelope, first["token"])[1])
    assert replay["ack"] == "1" and "Duplicate" in replay["error"]
    pretty = json.dumps({"epoch": first["state"]["epoch"], "seq": "2", "operation": {"type": "poll"}}, indent=2).encode()
    framed = json.loads(request("/api/event", pretty, first["token"])[1])
    assert framed["ack"] == "2"
    assert request("/api/event", b'{"epoch":"x","epoch":"y"}', first["token"])[0] == 400
    wrong = json.loads(request("/api/event", envelope, second["token"])[1])
    assert wrong["ack"] == "0" and "epoch" in wrong["error"]
    children = [session.process for session in host.sessions.values()]
    assert request("/api/release", {}, second["token"])[0] == 200
    assert request("/api/event", envelope, second["token"])[0] == 403
finally:
    host.shutdown()
    host.server_close()
    thread.join(timeout=5)
assert all(child.poll() is not None for child in children)
print("web loopback origin/token guard, session isolation, replay and child cleanup passed")
