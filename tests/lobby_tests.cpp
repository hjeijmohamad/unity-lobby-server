#include "lobby.hpp"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Action>
void rejects(int status, Action action) {
    try {
        action();
    } catch (const lobby::Error& error) {
        check(error.status == status, "Wrong error status");
        return;
    }
    throw std::runtime_error("Expected operation to fail");
}

void validation_and_auth() {
    lobby::Store store;
    rejects(400, [&] { store.create("", 4); });
    rejects(400, [&] { store.create(" player", 4); });
    rejects(400, [&] { store.create("player\n", 4); });
    rejects(400, [&] { store.create(std::string(25, 'a'), 4); });
    rejects(400, [&] { store.create("player", 1); });
    rejects(400, [&] { store.create("player", 9); });
    const auto host = store.create("host", 2);
    const auto guest = store.join(host.room.code, "guest");
    check(host.token.size() == 64 && guest.token != host.token, "Tokens must be opaque and distinct");
    check(host.room.host_id == host.player_id, "Creator must be host");
    check(guest.room.players.size() == 2, "Join must return updated room");
    rejects(409, [&] { store.join(host.room.code, "third"); });
    rejects(401, [&] { store.get(host.room.code, ""); });
    rejects(401, [&] { store.ready(host.room.code, guest.player_id, true); });
    const auto other = store.create("other", 2);
    rejects(401, [&] { store.get(host.room.code, other.token); });
    rejects(403, [&] { store.start(host.room.code, guest.token); });
}

void start_and_leave() {
    lobby::Store store;
    const auto host = store.create("host", 3);
    const auto code = host.room.code;
    store.ready(code, host.token, true);
    rejects(409, [&] { store.start(code, host.token); });
    const auto guest = store.join(code, "guest");
    rejects(409, [&] { store.start(code, host.token); });
    store.ready(code, guest.token, true);
    store.ready(code, guest.token, false);
    rejects(409, [&] { store.start(code, host.token); });
    store.ready(code, guest.token, true);
    check(store.start(code, host.token).started, "Start must update the room");
    rejects(409, [&] { store.start(code, host.token); });
    rejects(409, [&] { store.ready(code, guest.token, false); });
    rejects(409, [&] { store.join(code, "late"); });
    store.leave(code, guest.token);
    check(store.get(code, host.token).players.size() == 1, "Leave must remove guest");
    rejects(401, [&] { store.get(code, guest.token); });
    store.leave(code, host.token);
    rejects(404, [&] { store.get(code, host.token); });
}

void expiration_and_limit() {
    auto now = std::chrono::steady_clock::time_point{};
    lobby::Store store([&] { return now; });
    const auto host = store.create("host", 2);
    now += std::chrono::minutes(29);
    store.get(host.room.code, host.token);
    now += std::chrono::minutes(1);
    rejects(404, [&] { store.get(host.room.code, host.token); });
    const auto active = store.create("active", 2);
    now += std::chrono::minutes(29);
    store.ready(active.room.code, active.token, true);
    now += std::chrono::minutes(29);
    store.get(active.room.code, active.token);
    for (int i = 1; i < 128; ++i) {
        store.create("host", 2);
    }
    rejects(503, [&] { store.create("overflow", 2); });
    now += std::chrono::minutes(30);
    store.create("new", 2);
}

void concurrent_join() {
    lobby::Store store;
    const auto host = store.create("host", 4);
    std::atomic<int> joined{0};
    std::atomic<int> full{0};
    std::atomic<int> unexpected{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) {
        workers.emplace_back([&] {
            try {
                store.join(host.room.code, "guest");
                ++joined;
            } catch (const lobby::Error& error) {
                if (error.status == 409) {
                    ++full;
                } else {
                    ++unexpected;
                }
            } catch (...) {
                ++unexpected;
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    check(joined == 3 && full == 13 && unexpected == 0,
          "Concurrent joins must preserve capacity");
    check(store.get(host.room.code, host.token).players.size() == 4,
          "Concurrent joins must not lose players");
}

} // namespace

int main() {
    try {
        validation_and_auth();
        start_and_leave();
        expiration_and_limit();
        concurrent_join();
        std::cout << "Lobby tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Lobby test failed: " << error.what() << '\n';
        return 1;
    }
}
