#include "lobby.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

using nlohmann::json;

namespace {
json room_json(const lobby::Room& room) {
    json players = json::array();
    for (const auto& player : room.players) {
        players.push_back({{"id", player.id}, {"name", player.name}, {"ready", player.ready}});
    }
    return {{"code", room.code}, {"host_id", room.host_id}, {"capacity", room.capacity},
            {"started", room.started}, {"players", players}};
}

void reply(httplib::Response& res, int status, const json& value) {
    res.status = status;
    res.set_content(value.dump(), "application/json");
}

json body(const httplib::Request& req) {
    auto type = req.get_header_value("Content-Type");
    type = type.substr(0, type.find(';'));
    if (type != "application/json") {
        throw lobby::Error(415, "Content-Type must be application/json");
    }
    auto value = json::parse(req.body);
    if (!value.is_object()) throw lobby::Error(400, "Expected a JSON object");
    return value;
}

std::string name_from(const json& value) {
    if (!value.contains("name") || !value["name"].is_string()) {
        throw lobby::Error(400, "name must be a string");
    }
    return value["name"].get<std::string>();
}

std::string token_from(const httplib::Request& req) {
    const auto header = req.get_header_value("Authorization");
    if (header.compare(0, 7, "Bearer ") != 0 || header.size() != 71) {
        throw lobby::Error(401, "A member bearer token is required");
    }
    return header.substr(7);
}

template <typename Handler>
auto guarded(Handler handler) {
    return [handler](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Cache-Control", "no-store");
        try {
            handler(req, res);
        } catch (const lobby::Error& error) {
            reply(res, error.status, {{"error", error.what()}});
        } catch (const json::exception&) {
            reply(res, 400, {{"error", "Invalid JSON payload"}});
        } catch (const std::exception&) {
            reply(res, 500, {{"error", "Internal server error"}});
        }
    };
}

void session_reply(httplib::Response& res, const lobby::Session& session) {
    reply(res, 201, {{"room", room_json(session.room)},
                     {"player_id", session.player_id}, {"token", session.token}});
}
} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 8080;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout << "Usage: lobby-server [--host 127.0.0.1] [--port 8080]\n";
                return 0;
            }
            if (i + 1 >= argc) throw std::runtime_error("Missing option value");
            const std::string value = argv[++i];
            if (arg == "--host" && !value.empty()) host = value;
            else if (arg == "--port") {
                std::size_t used = 0;
                port = std::stoi(value, &used);
                if (used != value.size() || port < 1 || port > 65535) {
                    throw std::runtime_error("Port must be between 1 and 65535");
                }
            } else throw std::runtime_error("Unknown option: " + arg);
        }

        lobby::Store store;
        httplib::Server server;
        server.set_payload_max_length(4096);
        server.set_read_timeout(5, 0);
        server.set_write_timeout(5, 0);
        server.set_keep_alive_max_count(20);
        server.new_task_queue = [] { return new httplib::ThreadPool(8, 0, 64); };
        server.set_default_headers({{"Cache-Control", "no-store"}, {"X-Content-Type-Options", "nosniff"}});
        server.set_error_handler([](const auto&, auto& res) {
            if (res.body.empty()) reply(res, res.status, {{"error", "Request rejected"}});
        });

        server.Get("/healthz", guarded([](const auto&, auto& res) {
            reply(res, 200, {{"status", "ok"}});
        }));
        server.Post("/rooms", guarded([&](const auto& req, auto& res) {
            const auto value = body(req);
            if (!value.contains("capacity") || !value["capacity"].is_number_integer() ||
                value["capacity"] < 2 || value["capacity"] > 8) {
                throw lobby::Error(400, "capacity must be an integer from 2 to 8");
            }
            session_reply(res, store.create(name_from(value), value["capacity"].template get<int>()));
        }));
        server.Post(R"(/rooms/([a-f0-9]{6})/players)", guarded([&](const auto& req, auto& res) {
            session_reply(res, store.join(req.matches[1].str(), name_from(body(req))));
        }));
        server.Get(R"(/rooms/([a-f0-9]{6}))", guarded([&](const auto& req, auto& res) {
            reply(res, 200, {{"room", room_json(store.get(req.matches[1].str(), token_from(req)))}});
        }));
        server.Put(R"(/rooms/([a-f0-9]{6})/ready)", guarded([&](const auto& req, auto& res) {
            const auto token = token_from(req);
            const auto value = body(req);
            if (!value.contains("ready") || !value["ready"].is_boolean()) {
                throw lobby::Error(400, "ready must be a boolean");
            }
            reply(res, 200, {{"room", room_json(store.ready(req.matches[1].str(), token,
                                                         value["ready"].template get<bool>()))}});
        }));
        server.Post(R"(/rooms/([a-f0-9]{6})/start)", guarded([&](const auto& req, auto& res) {
            reply(res, 200, {{"room", room_json(store.start(req.matches[1].str(), token_from(req)))}});
        }));
        server.Delete(R"(/rooms/([a-f0-9]{6})/players/me)", guarded([&](const auto& req, auto& res) {
            store.leave(req.matches[1].str(), token_from(req));
            res.status = 204;
        }));

        if (!server.bind_to_port(host, port)) {
            std::cerr << "Cannot bind to " << host << ':' << port << '\n';
            return 1;
        }
        std::cout << "Lobby server listening on " << host << ':' << port << std::endl;
        return server.listen_after_bind() ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
