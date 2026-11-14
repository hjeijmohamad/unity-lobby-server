#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace lobby {

struct Player {
    std::string id;
    std::string name;
    bool ready = false;
};

struct Room {
    std::string code;
    std::vector<Player> players;
    std::string host_id;
    int capacity;
    bool started = false;
};

struct Session {
    Room room;
    std::string player_id;
    std::string token;
};

class Error : public std::runtime_error {
public:
    Error(int status, const std::string& message);
    int status;
};

class Store {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    explicit Store(Clock clock = std::chrono::steady_clock::now);
    Session create(const std::string& name, int capacity);
    Session join(const std::string& code, const std::string& name);
    Room get(const std::string& code, const std::string& token);
    Room ready(const std::string& code, const std::string& token, bool value);
    Room start(const std::string& code, const std::string& token);
    void leave(const std::string& code, const std::string& token);

private:
    struct Entry {
        Room room;
        std::unordered_map<std::string, std::string> members;
        std::chrono::steady_clock::time_point touched;
    };

    void expire(std::chrono::steady_clock::time_point now);
    Entry& room(const std::string& code);
    static std::string member(const Entry& entry, const std::string& token);
    static Session add_player(Entry& entry, const std::string& name);

    Clock clock_;
    std::mutex mutex_;
    std::unordered_map<std::string, Entry> rooms_;
};

} // namespace lobby
