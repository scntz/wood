#include "wood/process.hpp"
#include "wood/support.hpp"
#include "wood_runtime.hpp"
#include <barrier>
#include <functional>

namespace {
constexpr wd::Site test_site{"backend-test", 1, 1};
std::atomic<int> checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void fails(F action, const char* code) {
    try {
        action();
    } catch (const wd::Error& failure) {
        require(failure.code == code, "unexpected native error code");
        return;
    }
    throw std::runtime_error(std::string("expected error ") + code);
}
void execute(const wd::Database& db, const std::string& sql) {
    wd::ResourceScope query(wd::db_prepare(db, sql, test_site), test_site);
    while (wd::stmt_step(query.value, test_site)) {
    }
}
std::barrier<>* task_barrier = nullptr;
wd::Json barrier_work(wd::Json value) {
    task_barrier->arrive_and_wait();
    return value;
}
wd::Json cancel_work(wd::Json value) {
    while (!wd::task_cancelled(test_site))
        std::this_thread::yield();
    return value;
}
wd::Json error_work(wd::Json) {
    wd::error(test_site, "R4010", "preserved task failure");
}
void core(const std::filesystem::path& parent) {
    const auto root =
        parent / std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    const auto path = wood::path_text(root / wood::path_from("database á.db"));
    wd::ResourceScope first(wd::db_open(path, 20, test_site), test_site);
    execute(first.value, "CREATE TABLE t(id INTEGER PRIMARY KEY, text TEXT UNIQUE)");
    wd::ResourceScope second(wd::db_open(path, 20, test_site), test_site);
    {
        wd::ResourceScope tx(wd::db_begin(first.value, test_site), test_site);
        execute(first.value, "INSERT INTO t(text) VALUES('locked')");
        fails([&] { execute(second.value, "INSERT INTO t(text) VALUES('blocked')"); }, "R4020");
    }
    execute(second.value, "INSERT INTO t(text) VALUES('committed')");
    {
        wd::ResourceScope outer(wd::db_begin(first.value, test_site), test_site);
        wd::ResourceScope inner(wd::db_begin(first.value, test_site), test_site);
        fails([&] { wd::db_commit(outer.value, test_site); }, "R4020");
        wd::db_commit(inner.value, test_site);
        wd::db_commit(outer.value, test_site);
        fails([&] { wd::db_commit(outer.value, test_site); }, "R4020");
    }
    wd::ResourceScope query(wd::db_prepare(first.value, "SELECT ?,?", test_site), test_site);
    fails([&] { wd::stmt_int(query.value, 0, test_site); }, "R4020");
    fails([&] { wd::stmt_bind_int(query.value, 0, 1, test_site); }, "R4020");
    fails([&] { wd::stmt_bind_int(query.value, 3, 1, test_site); }, "R4020");
    fails(
        [&] {
            wd::stmt_bind_float(query.value, 1, std::numeric_limits<double>::infinity(), test_site);
        },
        "R4020");
    fails([&] { wd::stmt_bind_text(query.value, 1, std::string("\xff", 1), test_site); }, "R4020");
    wd::stmt_bind_int(query.value, 1, wd::max_int, test_site);
    wd::stmt_bind_blob(query.value, 2, "", test_site);
    require(wd::stmt_step(query.value, test_site), "bound row");
    require(wd::stmt_int(query.value, 0, test_site) == wd::max_int, "exact SQLite int64");
    require(wd::stmt_kind(query.value, 1, test_site) == "blob" &&
                wd::stmt_blob(query.value, 1, test_site).empty(),
            "empty blob is not NULL");
    fails([&] { wd::stmt_kind(query.value, -1, test_site); }, "R4020");
    fails([&] { wd::stmt_bind_int(query.value, 1, 1, test_site); }, "R4020");
    wd::stmt_close(query.value, test_site);
    fails([&] { wd::stmt_step(query.value, test_site); }, "R4020");
    // Deadline interrupts a genuinely long recursive SQL computation.
    wd::ResourceScope long_query(
        wd::db_prepare(first.value,
                       "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE "
                       "x<1000000000) SELECT sum(x) FROM n",
                       test_site),
        test_site);
    const auto begin = std::chrono::steady_clock::now();
    fails([&] { wd::stmt_step(long_query.value, test_site); }, "R4020");
    require(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5),
            "SQL execution deadline");
    execute(first.value, "SELECT count(*) FROM t"); // interrupted connection remains usable
    std::exception_ptr thread_error;
    std::jthread foreign([&] {
        try {
            fails([&] { wd::db_changes(first.value, test_site); }, "R4020");
        } catch (...) {
            thread_error = std::current_exception();
        }
    });
    foreign.join();
    if (thread_error)
        std::rethrow_exception(thread_error);
    // Immutable JSON messages are processed by four simultaneously live workers.
    std::barrier gate(4);
    task_barrier = &gate;
    std::vector<wd::Task> tasks;
    for (int i = 0; i < 4; ++i)
        tasks.push_back(wd::task_spawn(barrier_work, wd::Json(i), test_site));
    for (int i = 0; i < 4; ++i) {
        require(wd::json_int(wd::task_wait(tasks[static_cast<std::size_t>(i)], 5000, test_site),
                             test_site) == i,
                "concurrent task message");
        wd::task_close(tasks[static_cast<std::size_t>(i)], test_site);
    }
    wd::ResourceScope worker(wd::task_spawn(cancel_work, wd::Json("stopped"), test_site),
                             test_site);
    fails([&] { wd::task_wait(worker.value, 1, test_site); }, "R4022");
    wd::task_cancel(worker.value, test_site);
    require(wd::json_str(wd::task_wait(worker.value, 5000, test_site), test_site) == "stopped",
            "cooperative task cancellation");
    wd::ResourceScope failure(wd::task_spawn(error_work, wd::Json{}, test_site), test_site);
    fails([&] { wd::task_wait(failure.value, 5000, test_site); }, "R4010");
    wd::task_close(failure.value, test_site);
    wd::task_close(worker.value, test_site);
    wd::db_close(first.value, test_site);
    fails([&] { wd::stmt_step(long_query.value, test_site); }, "R4020");
    require(wd::task_detail::active.load() == 0, "all worker threads completed");
}
std::string fixture(const std::filesystem::path& root, const char* name) {
    return wood::path_text(root / name);
}
bool tls_available() {
    try {
        std::cout << wd::tls_version(test_site) << '\n';
        return true;
    } catch (const wd::Error& failure) {
        if (failure.message.starts_with("required native library is unavailable:")) {
#ifdef _WIN32
            if (GetEnvironmentVariableW(L"WOOD_REQUIRE_TLS_TESTS", nullptr, 0))
#else
            if (std::getenv("WOOD_REQUIRE_TLS_TESTS"))
#endif
                throw;
            std::cout << "SKIP: optional OpenSSL 3 runtime is unavailable\n";
            return false;
        }
        throw;
    }
}
void tls_pair(const std::filesystem::path& root, const std::string& host, const char* ca,
              const char* cert, const char* key, bool success, bool unclean = false) {
    wd::ResourceScope context(
        wd::tls_context_new(fixture(root, cert), fixture(root, key), test_site), test_site);
    wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 3000, test_site), test_site);
    const auto port = wd::tcp_port(listener.stream, test_site);
    std::exception_ptr server_error;
    std::jthread server([&] {
        try {
            auto raw = wd::tcp_accept(listener.stream, test_site);
            wd::StreamScope secure(wd::tls_accept(raw, context.value, test_site), test_site);
            require(wd::stream_closed(raw), "TLS upgrade invalidates plain aliases");
            if (unclean) {
                secure.stream.state->close();
                return;
            }
            std::string bytes;
            while (bytes.size() < 262144)
                bytes += wd::stream_read(secure.stream, 65536, test_site);
            require(bytes == std::string(262144, '\0'), "TLS binary request payload");
            wd::stream_write(secure.stream, bytes + "end", test_site);
        } catch (...) {
            server_error = std::current_exception();
        }
    });
    std::exception_ptr client_error;
    try {
        wd::StreamScope connection(wd::tls_connect(host, port, fixture(root, ca), 3000, test_site),
                                   test_site);
        if (unclean) {
            fails([&] { wd::stream_read(connection.stream, 1, test_site); }, "R4021");
            connection.stream.state->close();
        } else {
            wd::stream_write(connection.stream, std::string(262144, '\0'), test_site);
            std::string bytes;
            for (;;) {
                auto part = wd::stream_read(connection.stream, 65536, test_site);
                if (part.empty())
                    break;
                bytes += part;
            }
            require(bytes == std::string(262144, '\0') + "end",
                    "TLS binary response and clean EOF");
        }
    } catch (...) {
        client_error = std::current_exception();
    }
    server.join();
    if (success) {
        if (client_error)
            std::rethrow_exception(client_error);
        if (server_error)
            std::rethrow_exception(server_error);
    } else {
        require(client_error != nullptr, "invalid TLS peer rejected");
        try {
            std::rethrow_exception(client_error);
        } catch (const wd::Error& failure) {
            require(failure.code == "R4021", "certificate validation error");
        }
    }
}
wd::Json tls_transferred_work(wd::Stream connection, wd::Json payload) {
    const auto bytes = wd::stream_read(connection, 3, test_site);
    require(bytes == std::string("a\0b", 3), "TLS buffered bytes survive task transfer");
    wd::stream_write(connection, bytes + wd::json_str(payload, test_site), test_site);
    return wd::Json(static_cast<wd::Int>(bytes.size()));
}
void tls_transfer(const std::filesystem::path& root) {
    wd::ResourceScope context(wd::tls_context_new(fixture(root, "server.pem"),
                                                  fixture(root, "server-key.pem"), test_site),
                              test_site);
    wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 3000, test_site), test_site);
    std::exception_ptr server_error;
    std::jthread server([&] {
        try {
            auto raw = wd::tcp_accept(listener.stream, test_site);
            wd::StreamScope secure(wd::tls_accept(raw, context.value, test_site), test_site);
            require(wd::stream_read_line(secure.stream, 100, test_site) == "prefix\n",
                    "TLS prefix before task transfer");
            require(secure.stream.state->input.size() - secure.stream.state->offset == 3,
                    "TLS transfer fixture has buffered input");
            const auto alias = secure.stream;
            wd::ResourceScope worker(
                wd::task_spawn_io(tls_transferred_work, secure.stream, wd::Json("done"), test_site),
                test_site);
            require(wd::stream_closed(alias) && wd::stream_closed(secure.stream),
                    "TLS task transfer consumes all original aliases");
            require(wd::json_int(wd::task_wait(worker.value, 5000, test_site), test_site) == 3,
                    "TLS task result after scoped socket close");
        } catch (...) {
            server_error = std::current_exception();
        }
    });
    std::exception_ptr client_error;
    try {
        wd::StreamScope client(wd::tls_connect("localhost",
                                               wd::tcp_port(listener.stream, test_site),
                                               fixture(root, "ca.pem"), 3000, test_site),
                               test_site);
        wd::stream_write(client.stream, std::string("prefix\na\0b", 10), test_site);
        std::string response;
        for (;;) {
            auto part = wd::stream_read(client.stream, 100, test_site);
            if (part.empty())
                break;
            response += part;
        }
        require(response == std::string("a\0bdone", 7), "transferred TLS response and clean EOF");
        // The task's normal close waits for this side's write half-close.
    } catch (...) {
        client_error = std::current_exception();
    }
    server.join();
    if (client_error)
        std::rethrow_exception(client_error);
    if (server_error)
        std::rethrow_exception(server_error);
}
void tls(const std::filesystem::path& root) {
    tls_transfer(root);
    tls_pair(root, "localhost", "ca.pem", "server-chain.pem", "server-key.pem", true);
    tls_pair(root, "localhost", "wrong-ca.pem", "server.pem", "server-key.pem", false);
    tls_pair(root, "127.0.0.1", "ca.pem", "server.pem", "server-key.pem", false);
    tls_pair(root, "localhost", "ca.pem", "expired.pem", "expired-key.pem", false);
    tls_pair(root, "localhost", "ca.pem", "server.pem", "server-key.pem", true, true);
    fails(
        [&] {
            wd::tls_context_new(fixture(root, "malformed.pem"), fixture(root, "server-key.pem"),
                                test_site);
        },
        "R4021");
    fails(
        [&] {
            wd::tls_context_new(fixture(root, "server.pem"), fixture(root, "wrong-key.pem"),
                                test_site);
        },
        "R4021");
    wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 1000, test_site), test_site);
    // A TCP peer that never sends TLS records cannot hold a handshake forever.
    fails(
        [&] {
            wd::tls_connect("127.0.0.1", wd::tcp_port(listener.stream, test_site),
                            fixture(root, "ca.pem"), 20, test_site);
        },
        "R4018");
    wd::ResourceScope context(wd::tls_context_new(fixture(root, "server.pem"),
                                                  fixture(root, "server-key.pem"), test_site),
                              test_site);
    std::exception_ptr close_error;
    std::jthread closer([&] {
        try {
            auto raw = wd::tcp_accept(listener.stream, test_site);
            // The previous timed-out handshake left one plain accepted socket.
            wd::stream_close(raw, test_site);
            raw = wd::tcp_accept(listener.stream, test_site);
            auto secure = wd::tls_accept(raw, context.value, test_site);
            wd::stream_timeout(secure, 20, test_site);
            fails([&] { wd::stream_close(secure, test_site); }, "R4018");
            require(wd::stream_closed(secure), "timed-out TLS close still releases its socket");
        } catch (...) {
            close_error = std::current_exception();
        }
    });
    auto held = wd::tls_connect("localhost", wd::tcp_port(listener.stream, test_site),
                                fixture(root, "ca.pem"), 1000, test_site);
    closer.join();
    held.state->close();
    if (close_error)
        std::rethrow_exception(close_error);
}
std::string http_read(const wd::Stream& connection, int status) {
    auto head = wd::stream_read_line(connection, 16384, test_site);
    require(head.starts_with("HTTP/1.1 " + std::to_string(status) + " "), "HTTPS status");
    std::size_t length = 0;
    for (;;) {
        auto line = wd::stream_read_line(connection, 16384, test_site);
        if (line == "\r\n")
            break;
        require(!line.empty(), "HTTPS header termination");
        if (line.starts_with("Content-Length: "))
            length = static_cast<std::size_t>(std::stoull(line.substr(16)));
    }
    require(length <= 1048576, "HTTPS application response budget");
    std::string result;
    while (result.size() < length) {
        auto part =
            wd::stream_read(connection, static_cast<wd::Int>(length - result.size()), test_site);
        require(!part.empty(), "HTTPS response length");
        result += part;
    }
    return result;
}
std::string exchange(wd::Int port, const std::string& ca, const std::string& method,
                     const std::string& target, const std::string& body, int status) {
    wd::StreamScope connection(wd::tls_connect("localhost", port, ca, 5000, test_site), test_site);
    wd::stream_write(connection.stream,
                     method + " " + target + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " +
                         std::to_string(body.size()) + "\r\n\r\n" + body,
                     test_site);
    return http_read(connection.stream, status);
}
void run_server(const std::string& executable, const std::filesystem::path& fixtures,
                const std::filesystem::path& root, int requests,
                const std::function<void(wd::Int)>& work, const std::string& name,
                bool json_service = false) {
    const auto ready = root / (name + ".ready");
    std::atomic<bool> finished = false;
    int status = -1;
    std::exception_ptr failure;
    std::jthread server([&] {
        try {
            const std::vector<std::string> arguments = json_service
                ? std::vector<std::string>{executable, "127.0.0.1", "0",
                    fixture(fixtures, "server-chain.pem"), fixture(fixtures, "server-key.pem"),
                    std::to_string(requests), wood::path_text(ready)}
                : std::vector<std::string>{executable, wood::path_text(root / "items.db"),
                    fixture(fixtures, "server-chain.pem"), fixture(fixtures, "server-key.pem"),
                    "0", std::to_string(requests), wood::path_text(ready)};
            status = wood::run_process(arguments,
                                       root / (name + ".log"), {std::chrono::seconds(30), 1048576});
        } catch (...) {
            failure = std::current_exception();
        }
        finished.store(true);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    std::string text;
    while (!text.ends_with('\n') && !finished.load()) {
        require(std::chrono::steady_clock::now() < deadline, "HTTPS backend startup deadline");
        if (std::filesystem::exists(ready))
            text = wood::read_file(ready);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(text.ends_with('\n'), "HTTPS backend readiness");
    work(static_cast<wd::Int>(std::stoll(text)));
    server.join();
    if (failure)
        std::rethrow_exception(failure);
    require(status == 0, "standalone HTTPS backend exit");
}
void https_client(const std::filesystem::path& fixtures, const std::string& client,
                  const std::filesystem::path& root);
void json_apps(const std::filesystem::path& fixtures, const std::string& server,
               const std::string& client, const std::filesystem::path& root) {
    const auto ca = fixture(fixtures, "ca.pem");
    run_server(server, fixtures, root, 3, [&](wd::Int port) {
        require(wood::Json::parse(exchange(port, ca, "GET", "/health", "", 200))
                    .at("status").string() == "ok", "native HTTPS health response");
        require(wood::Json::parse(exchange(port, ca, "POST", "/sum", "[1,2,3]", 200))
                    .at("sum").integer() == 6, "native HTTPS JSON computation");
        (void)exchange(port, ca, "GET", "/absent", "", 404);
    }, "json", true);
    https_client(fixtures, client, root);
}
void apps(const std::filesystem::path& fixtures, const std::string& server,
          const std::string& client, const std::filesystem::path& root) {
    const auto ca = fixture(fixtures, "ca.pem");
    const std::string attack = "'); DROP TABLE items; --";
    run_server(
        server, fixtures, root, 8,
        [&](wd::Int port) {
            fails(
                [&] {
                    wd::tls_connect("localhost", port, fixture(fixtures, "wrong-ca.pem"), 5000,
                                    test_site);
                },
                "R4021");
            fails([&] { wd::tls_connect("127.0.0.1", port, ca, 5000, test_site); }, "R4021");
            // Leave one request incomplete. A second request must finish before it:
            // a sequential server would time out here. This verifies actual overlap.
            wd::StreamScope slow(wd::tls_connect("localhost", port, ca, 5000, test_site),
                                 test_site);
            wd::stream_write(slow.stream, "GET /health HTTP/1.1\r\nHost: localhost\r\n", test_site);
            require(wood::Json::parse(exchange(port, ca, "GET", "/health", "", 200))
                            .at("status")
                            .string() == "ok",
                    "overlapping HTTPS health request");
            wd::stream_write(slow.stream, "\r\n", test_site);
            require(wood::Json::parse(http_read(slow.stream, 200)).at("status").string() == "ok",
                    "slow request completes independently");
            const auto payload =
                wood::Json(wood::Json::Object{{"names", wood::Json::Array{wood::Json("alpha"),
                                                                          wood::Json(attack)}}})
                    .dump();
            require(wood::Json::parse(exchange(port, ca, "POST", "/items", payload, 201))
                            .at("created")
                            .integer() == 2,
                    "transactional HTTPS insertion");
            (void)exchange(port, ca, "POST", "/items", "{\"names\":[\"temporary\",\"alpha\"]}",
                           400);
            const auto rows =
                wood::Json::parse(exchange(port, ca, "GET", "/items", "", 200)).array();
            require(rows.size() == 2 && rows[1].at("name").string() == attack,
                    "bound SQL data and full batch rollback");
            wd::StreamScope invalid(wd::tls_connect("localhost", port, ca, 5000, test_site),
                                    test_site);
            wd::stream_write(invalid.stream,
                             "POST /items HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
                             "0\r\nTransfer-Encoding: chunked\r\n\r\n",
                             test_site);
            (void)http_read(invalid.stream, 400);
        },
        "initial");
    run_server(
        server, fixtures, root, 1,
        [&](wd::Int port) {
            require(
                wood::Json::parse(exchange(port, ca, "GET", "/items", "", 200)).array().size() == 2,
                "database persists across process restart");
        },
        "restart");
    https_client(fixtures, client, root);
}
void https_client(const std::filesystem::path& fixtures, const std::string& client,
                  const std::filesystem::path& root) {
    const auto ca = fixture(fixtures, "ca.pem");
    // Independently supplied TLS wire response exercises Wood's HTTPS client.
    wd::ResourceScope context(wd::tls_context_new(fixture(fixtures, "server.pem"),
                                                  fixture(fixtures, "server-key.pem"), test_site),
                              test_site);
    require(wd::tls_detail::api(test_site).SSL_CTX_ctrl(context.value.state->handle, 124,
                0x0303, nullptr) == 1, "independent HTTPS peer requires TLS 1.2");
    wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 5000, test_site), test_site);
    const auto port = wd::tcp_port(listener.stream, test_site);
    std::exception_ptr peer_error;
    std::jthread peer([&] {
        try {
            auto raw = wd::tcp_accept(listener.stream, test_site);
            wd::StreamScope secure(wd::tls_accept(raw, context.value, test_site), test_site);
            using ServerName = const char* (*)(const wd::tls_detail::SSL*, int);
            auto name = wd::tls_detail::api(test_site).ssl.symbol<ServerName>(
                "SSL_get_servername", "R4021", test_site)(
                    static_cast<wd::tls_detail::Transport*>(secure.stream.state->secure.get())->ssl, 0);
            require(name && std::string_view(name) == "localhost", "HTTPS client supplies SNI");
            std::string header;
            while (!header.ends_with("\r\n\r\n"))
                header += wd::stream_read_line(secure.stream, 16384, test_site);
            require(header.starts_with("GET /from-native HTTP/1.1\r\n"),
                    "Wood HTTPS client request");
            wd::stream_write(secure.stream,
                             "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\na",
                             test_site);
            wd::stream_write(secure.stream, std::string("\0b\r\n0\r\n\r\n", 9), test_site);
        } catch (...) {
            peer_error = std::current_exception();
        }
    });
    const auto log = root / "client.log";
    const auto status =
        wood::run_process({client, "localhost", std::to_string(port), "/from-native", ca}, log,
                          {std::chrono::seconds(15), 1048576});
    peer.join();
    if (peer_error)
        std::rethrow_exception(peer_error);
    require(status == 0, "standalone Wood HTTPS client exit");
    const auto result = wood::Json::parse(wood::read_file(log));
    require(result.at("status").integer() == 200 &&
                result.at("body").string() == std::string("a\0b", 3),
            "HTTPS chunked binary response");
}
} // namespace
int run(int argc, char** argv) {
    try {
        if (argc < 3)
            return 2;
        const std::string mode = argv[1];
        if (mode == "core")
            core(wood::path_from(argv[2]));
        else {
            if (!tls_available())
                return 77;
            if (mode == "tls")
                tls(wood::path_from(argv[2]));
            else if (mode == "apps" && argc == 6)
                apps(wood::path_from(argv[2]), argv[3], argv[4], wood::path_from(argv[5]));
            else if (mode == "json-apps" && argc == 6)
                json_apps(wood::path_from(argv[2]), argv[3], argv[4], wood::path_from(argv[5]));
            else
                return 2;
        }
        std::cout << checks.load() << " native backend checks passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> arguments;
    for (int i = 0; i < argc; ++i)
        arguments.push_back(wood::path_text(std::filesystem::path(argv[i])));
    std::vector<char*> pointers;
    for (auto& argument : arguments)
        pointers.push_back(argument.data());
    return run(argc, pointers.data());
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
