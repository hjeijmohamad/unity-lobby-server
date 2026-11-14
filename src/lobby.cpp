#include "lobby.hpp"

#include <algorithm>
#include <fstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace lobby {
namespace {

std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> data(bytes);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, data.data(), static_cast<ULONG>(data.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("Could not obtain random bytes");
    }
#else
    std::ifstream source("/dev/urandom", std::ios::binary);
    if (!source.read(reinterpret_cast<char*>(data.data()),
                     static_cast<std::streamsize>(data.size()))) {
        throw std::runtime_error("Could not obtain random bytes");
    }
#endif
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes * 2);
    for (auto byte : data) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 24 || name.front() == ' ' || name.back() == ' ' ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) {
            return c < 32 || c > 126;
        })) {
        throw Error(400, "Name must be 1-24 printable ASCII characters without surrounding spaces");
    }
}

} // namespace

Error::Error(int status, const std::string& message)
    : std::runtime_error(message), status(status) {}

Store::Store(Clock clock) : clock_(std::move(clock)) {}

void Store::expire(std::chrono::steady_clock::time_point now) {
    for (auto it = rooms_.begin(); it != rooms_.end();) {
        if (now - it->second.touched >= std::chrono::minutes(30)) {
            it = rooms_.erase(it);
        } else {
            ++it;
        }
    }
}

Store::Entry& Store::room(const std::string& code) {
    auto it = rooms_.find(code);
    if (it == rooms_.end()) {
        throw Error(404, "Room not found or expired");
    }
    return it->second;
}

std::string Store::member(const Entry& entry, const std::string& token) {
    auto it = entry.members.find(token);
    if (it == entry.members.end()) {
        throw Error(401, "Invalid player token");
    }
    return it->second;
}

Session Store::add_player(Entry& entry, const std::string& name) {
    std::string token;
    do {
        token = random_hex(32);
    } while (entry.members.count(token));
    std::string id;
    do {
        id = random_hex(8);
    } while (std::any_of(entry.room.players.begin(), entry.room.players.end(),
                         [&](const Player& player) { return player.id == id; }));
    entry.room.players.push_back({id, name, false});
    entry.members.emplace(token, id);
    return {entry.room, id, token};
}

Session Store::create(const std::string& name, int capacity) {
    validate_name(name);
    if (capacity < 2 || capacity > 8) {
        throw Error(400, "Capacity must be between 2 and 8");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    expire(now);
    if (rooms_.size() >= 128) {
        throw Error(503, "Room limit reached");
    }
    std::string code;
    do {
        code = random_hex(3);
    } while (rooms_.count(code));
    Entry entry{{code, {}, "", capacity, false}, {}, now};
    auto session = add_player(entry, name);
    entry.room.host_id = session.player_id;
    session.room.host_id = session.player_id;
    rooms_.emplace(code, std::move(entry));
    return session;
}

Session Store::join(const std::string& code, const std::string& name) {
    validate_name(name);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    expire(now);
    auto& entry = room(code);
    if (entry.room.started) {
        throw Error(409, "Room has already started");
    }
    if (entry.room.players.size() >= static_cast<std::size_t>(entry.room.capacity)) {
        throw Error(409, "Room is full");
    }
    auto session = add_player(entry, name);
    entry.touched = now;
    return session;
}

Room Store::get(const std::string& code, const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    expire(clock_());
    auto& entry = room(code);
    member(entry, token);
    return entry.room;
}

Room Store::ready(const std::string& code, const std::string& token, bool value) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    expire(now);
    auto& entry = room(code);
    const auto id = member(entry, token);
    if (entry.room.started) {
        throw Error(409, "Room has already started");
    }
    for (auto& player : entry.room.players) {
        if (player.id == id) {
            player.ready = value;
            break;
        }
    }
    entry.touched = now;
    return entry.room;
}

Room Store::start(const std::string& code, const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    expire(now);
    auto& entry = room(code);
    if (member(entry, token) != entry.room.host_id) {
        throw Error(403, "Only the host can start the room");
    }
    if (entry.room.started) {
        throw Error(409, "Room has already started");
    }
    if (entry.room.players.size() < 2 ||
        !std::all_of(entry.room.players.begin(), entry.room.players.end(),
                     [](const Player& player) { return player.ready; })) {
        throw Error(409, "At least two players must be ready");
    }
    entry.room.started = true;
    entry.touched = now;
    return entry.room;
}

void Store::leave(const std::string& code, const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = clock_();
    expire(now);
    auto& entry = room(code);
    const auto id = member(entry, token);
    if (id == entry.room.host_id) {
        rooms_.erase(code);
        return;
    }
    auto& players = entry.room.players;
    players.erase(std::remove_if(players.begin(), players.end(),
                                 [&](const Player& player) { return player.id == id; }),
                  players.end());
    entry.members.erase(token);
    entry.touched = now;
}

} // namespace lobby
