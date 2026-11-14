"""Exercise the public API against a fresh server process."""
import concurrent.futures
import http.client
import json
import socket
import subprocess
import sys
import tempfile
import time


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def exercise(port):
    def request(method, path, expected, payload=None, token=None, content_type="application/json"):
        headers = {}
        if token is not None:
            headers["Authorization"] = "Bearer " + token
        if payload is not None:
            headers["Content-Type"] = content_type
            if not isinstance(payload, bytes):
                payload = json.dumps(payload).encode("utf-8")
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
        try:
            connection.request(method, path, body=payload, headers=headers)
            response = connection.getresponse()
            raw = response.read()
            check(response.status == expected,
                  f"{method} {path}: expected {expected}, got {response.status}: {raw!r}")
            if expected == 204:
                check(not raw, "204 response must be empty")
                return None
            check(response.getheader("Content-Type", "").startswith("application/json"),
                  "Response must use JSON")
            value = json.loads(raw)
            if expected >= 400:
                check(isinstance(value.get("error"), str), "Error response needs a message")
            return value
        finally:
            connection.close()

    check(request("GET", "/healthz", 200) == {"status": "ok"}, "Health response")
    request("GET", "/does-not-exist", 404)
    for malformed in (b"{", b"null", b"[]"):
        request("POST", "/rooms", 400, malformed)
    for capacity in (-1, 1, 9, 2.5, 18446744073709551615, True, "4", None):
        request("POST", "/rooms", 400, {"name": "Host", "capacity": capacity})
    for name in (None, True, "", " Host", "Host ", "Host\n", "x" * 25):
        request("POST", "/rooms", 400, {"name": name, "capacity": 2})
    request("POST", "/rooms", 400, {"capacity": 2})
    request("POST", "/rooms", 415, {"name": "Host", "capacity": 2}, content_type="text/plain")
    request("POST", "/rooms", 413, b" " * 5000)

    host = request("POST", "/rooms", 201, {"name": "Host", "capacity": 2})
    room = host["room"]
    code, token = room["code"], host["token"]
    path = "/rooms/" + code
    check(len(code) == 6 and len(token) == 64, "Session identifiers")
    check(host["player_id"] == room["host_id"], "Creator must be host")
    check("token" not in json.dumps(room), "Room views must not expose tokens")
    request("GET", path, 401)
    request("GET", path, 401, token="0" * 64)
    request("POST", path + "/start", 409, token=token)
    guest = request("POST", path + "/players", 201, {"name": "Guest"})
    check(guest["token"] != token, "Members need distinct tokens")
    check(len(guest["room"]["players"]) == 2, "Join returns latest room")
    check(request("GET", path, 200, token=token)["room"] == guest["room"], "Member room view")
    request("POST", path + "/players", 409, {"name": "Third"})
    request("POST", path + "/start", 403, token=guest["token"])
    request("PUT", path + "/ready", 401, {"ready": True})
    for value in ("true", 1, None):
        request("PUT", path + "/ready", 400, {"ready": value}, token=token)
    request("PUT", path + "/ready", 400, {}, token=token)
    request("PUT", path + "/ready", 200, {"ready": True}, token=token)
    request("POST", path + "/start", 409, token=token)
    request("PUT", path + "/ready", 200, {"ready": True}, token=guest["token"])
    started = request("POST", path + "/start", 200, token=token)
    check(started["room"]["started"] is True, "Start updates room state")
    check("token" not in json.dumps(started), "Room response must hide credentials")
    request("POST", path + "/players", 409, {"name": "Late"})
    request("PUT", path + "/ready", 409, {"ready": False}, token=guest["token"])
    request("DELETE", path + "/players/me", 204, token=guest["token"])
    request("GET", path, 401, token=guest["token"])
    check(len(request("GET", path, 200, token=token)["room"]["players"]) == 1,
          "Leaving removes the player")
    request("DELETE", path + "/players/me", 204, token=token)
    request("GET", path, 404, token=token)

    other = request("POST", "/rooms", 201, {"name": "Host", "capacity": 2})
    join_path = "/rooms/" + other["room"]["code"] + "/players"

    def join(_):
        connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
        try:
            connection.request("POST", join_path, body=b'{"name":"Guest"}',
                               headers={"Content-Type": "application/json"})
            response = connection.getresponse()
            response.read()
            return response.status
        finally:
            connection.close()

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        statuses = list(pool.map(join, range(8)))
    check(statuses.count(201) == 1 and statuses.count(409) == 7,
          f"Concurrent joins exceeded capacity: {statuses}")


def main():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
    with tempfile.TemporaryFile(mode="w+b") as log:
        process = subprocess.Popen([sys.argv[1], "--host", "127.0.0.1", "--port", str(port)],
                                   stdout=log, stderr=subprocess.STDOUT)
        failed = True
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                check(process.poll() is None, "Server exited during startup")
                connection = http.client.HTTPConnection("127.0.0.1", port, timeout=0.2)
                try:
                    connection.request("GET", "/healthz")
                    response = connection.getresponse()
                    response.read()
                    if response.status == 200:
                        break
                except OSError:
                    pass
                finally:
                    connection.close()
                time.sleep(0.05)
            else:
                raise AssertionError("Server did not become healthy within 10 seconds")
            exercise(port)
            failed = False
            print("HTTP API tests passed")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)
            if failed:
                log.seek(0)
                print("Server output:\n" + log.read().decode("utf-8", errors="replace"), file=sys.stderr)


if __name__ == "__main__":
    main()
