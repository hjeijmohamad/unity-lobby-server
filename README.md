# Unity lobby server

A small C++17 HTTP backend for pre-match lobbies. Players create a room, share its code, join, and mark themselves ready. The host can start once at least two players are present and everyone is ready.

The server owns the lobby state. A [Unity client example](examples/unity) uses `UnityWebRequest` to call the API.

## Build and run

Requires CMake 3.24+, a C++17 compiler, and Python 3 for the HTTP tests. The first configuration downloads [cpp-httplib](https://github.com/yhirose/cpp-httplib) and [nlohmann/json](https://github.com/nlohmann/json); both archives are pinned and checked with SHA-256.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

Run on Linux or macOS:

```sh
./build/lobby-server
```

Run with Visual Studio on Windows:

```powershell
.\build\Release\lobby-server.exe
```

The default address is `127.0.0.1:8080`. Change it with `--host` and `--port`. Use `-DBUILD_TESTING=OFF` at configuration time to build without Python.

## Try the API

```sh
curl http://127.0.0.1:8080/healthz
curl -X POST http://127.0.0.1:8080/rooms \
  -H 'Content-Type: application/json' \
  -d '{"name":"Alex","capacity":4}'
```

Creating or joining a room returns a membership:

```json
{
  "room": {
    "code": "a1b2c3",
    "host_id": "0123456789abcdef",
    "capacity": 4,
    "started": false,
    "players": [{"id":"0123456789abcdef","name":"Alex","ready":false}]
  },
  "player_id": "0123456789abcdef",
  "token": "<member token>"
}
```

Use the returned room code and token in later calls:

```sh
curl -X POST http://127.0.0.1:8080/rooms/a1b2c3/players \
  -H 'Content-Type: application/json' -d '{"name":"Sam"}'

curl -X PUT http://127.0.0.1:8080/rooms/a1b2c3/ready \
  -H 'Content-Type: application/json' \
  -H 'Authorization: Bearer <member token>' -d '{"ready":true}'
```

Each player uses their own token to become ready, including the host. The host then calls `POST /rooms/a1b2c3/start` with their bearer token. On Windows, use `curl.exe` and your shell's JSON quoting rules.

| Method | Path | Body | Result |
| --- | --- | --- | --- |
| GET | `/healthz` | — | `200`, health status |
| POST | `/rooms` | `name`, `capacity` | `201`, membership |
| POST | `/rooms/{code}/players` | `name` | `201`, membership |
| GET | `/rooms/{code}` | — | `200`, `{ "room": ... }` |
| PUT | `/rooms/{code}/ready` | `ready` | `200`, room |
| POST | `/rooms/{code}/start` | — | `200`, room |
| DELETE | `/rooms/{code}/players/me` | — | `204`, no body |

All room operations except create and join require `Authorization: Bearer <token>`. JSON bodies require `Content-Type: application/json`. Errors use `{ "error": "message" }`: invalid input is `400`, invalid membership `401`, host-only access `403`, missing or expired room `404`, and state conflicts `409`. Unsupported content types return `415`; bodies above 4 KiB return `413`.

## Rules and scope

- Rooms hold 2–8 players, with up to 128 rooms per process. A full server returns `503` on create.
- Names contain 1–24 printable ASCII characters, without surrounding spaces. Duplicate display names are allowed; player IDs identify members.
- Room codes are six lowercase hexadecimal characters. They act as invitations: anyone with a code can join a waiting room.
- Member tokens use 32 random bytes from the operating system and are returned only when a player creates or joins. Room snapshots contain no tokens.
- Joining and changing readiness are blocked after start. Players can still leave. If the host leaves, the room is removed.
- Rooms expire after 30 minutes without a successful join, ready, start, or leave operation. Polling does not extend their lifetime. Expiry is checked on the next store operation.

State lives in memory and resets when the process stops. The store uses a mutex so simultaneous joins cannot exceed capacity. HTTP handling uses eight workers and a bounded queue.

This handles the lobby before a match; it does not launch game servers, relay gameplay, or provide accounts. Keep the default loopback binding for local use. An internet deployment needs TLS, request rate limits, and an identity layer appropriate to the game. The Unity sample targets desktop and Editor use; it has no WebGL CORS setup.

## Tests

The C++ suite covers membership, start rules, departure, expiry with a fake clock, room limits, and concurrent joins. The Python suite starts the real HTTP server and checks the full two-player flow, validation, response shapes, and concurrent capacity enforcement. GitHub Actions runs both suites on Linux and Windows.

## License

[MIT](LICENSE). Third-party libraries retain their own MIT licenses in their downloaded source trees.
