#include "wood/process.hpp"
#include "wood/support.hpp"
#include "wood_runtime.hpp"
#include <atomic>
#include <thread>

namespace {
constexpr wd::Site test_site{"network-app-test", 1, 1};
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::string read_all(const wd::Stream& value) {
    std::string result;
    for (;;) {
        auto part = wd::stream_read(value, 65536, test_site);
        if (part.empty())
            return result;
        result += part;
        require(result.size() < 1048576, "application output budget");
    }
}
std::string exchange(wd::Int port, const std::string& request, int expected) {
    wd::StreamScope connection(wd::tcp_connect("127.0.0.1", port, 5000, test_site), test_site);
    wd::stream_write(connection.stream, request, test_site);
    wd::tcp_shutdown_write(connection.stream, test_site);
    auto result = read_all(connection.stream);
    require(result.starts_with("HTTP/1.1 " + std::to_string(expected) + " "),
            "application response status");
    const auto separator = result.find("\r\n\r\n");
    require(separator != std::string::npos, "application response framing");
    return result.substr(separator + 4);
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--resolver") {
        try {
            wd::stream_detail::initialize(test_site);
            addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_protocol = IPPROTO_TCP;
            hints.ai_flags = AI_NUMERICSERV;
            addrinfo* addresses = nullptr;
            const int status = getaddrinfo("localhost", "12345", &hints, &addresses);
            if (status != 0)
                wd::stream_detail::network_error(test_site, "fixture resolver", status);
            const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
            bool v4 = false, v6 = false;
            for (auto* address = addresses; address; address = address->ai_next) {
                v4 |= address->ai_family == AF_INET;
                v6 |= address->ai_family == AF_INET6;
            }
            std::cout << "{\"ipv4\":" << (v4 ? "true" : "false")
                      << ",\"ipv6\":" << (v6 ? "true" : "false") << "}\n";
            return 0;
        } catch (const std::exception& failure) {
            std::cerr << failure.what() << '\n';
            return 1;
        }
    }
    if (argc == 4 && std::string_view(argv[1]) == "--pressure") {
        try {
            wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 10000, test_site), test_site);
            const int bytes = 1024;
            require(setsockopt(listener.stream.state->socket, SOL_SOCKET, SO_RCVBUF,
                               reinterpret_cast<const char*>(&bytes), sizeof(bytes)) == 0,
                    "bounded peer receive buffer");
            const auto port = wd::tcp_port(listener.stream, test_site);
            const auto log = wood::path_from(argv[3]) / "pressure.log";
            int status = -1;
            std::exception_ptr failure;
            std::jthread child([&] {
                try {
                    status = wood::run_process({argv[2], std::to_string(port)}, log,
                                              {std::chrono::seconds(20), 1048576});
                } catch (...) {
                    failure = std::current_exception();
                }
            });
            wd::StreamScope peer(wd::tcp_accept(listener.stream, test_site), test_site);
            // Keep the socket open without consuming data until the child ends.
            child.join();
            if (failure)
                std::rethrow_exception(failure);
#ifdef _WIN32
            constexpr auto expected = "native write deadline passed\r\n";
#else
            constexpr auto expected = "native write deadline passed\n";
#endif
            require(status == 0 && wood::read_file(log) == expected,
                    "installed application write deadline under real backpressure");
            std::cout << "independent backpressure deadline passed\n";
            return 0;
        } catch (const std::exception& failure) {
            std::cerr << failure.what() << '\n';
            return 1;
        }
    }
    if (argc != 4)
        return 2;
    try {
        const auto root = wood::path_from(argv[3]);
        const auto ready = root / "server.ready";
        require(!std::filesystem::exists(ready), "unique fixture directory required");
        int server_status = -1;
        std::exception_ptr server_error;
        std::atomic<bool> finished = false;
        // Only one driver process call runs at a time: its cancellation handlers
        // are process-wide. The parent talks TCP directly while the child serves.
        std::jthread server([&] {
            try {
                server_status =
                    wood::run_process({argv[1], "127.0.0.1", "0", "8", wood::path_text(ready)},
                                      root / "server.log", {std::chrono::seconds(20), 1048576});
            } catch (...) {
                server_error = std::current_exception();
            }
            finished.store(true);
        });
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        std::string ready_text;
        while (!ready_text.ends_with('\n') && !finished.load()) {
            require(std::chrono::steady_clock::now() < until, "HTTP server startup deadline");
            if (std::filesystem::exists(ready))
                ready_text = wood::read_file(ready);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(ready_text.ends_with('\n'), "HTTP server did not publish ready port");
        const auto port = static_cast<wd::Int>(std::stoll(ready_text));
        const std::string host = "\r\nHost: localhost\r\n";
        require(exchange(port, "GET /health HTTP/1.1" + host + "\r\n", 200) ==
                    "{\"status\":\"ok\"}",
                "health JSON");
        require(exchange(port, "POST /sum HTTP/1.1" + host + "Content-Length: 9\r\n\r\n[1,2,3,4]",
                         200) == "{\"sum\":10}",
                "POST JSON calculation");
        require(exchange(port, "POST /sum HTTP/1.1" + host + "Content-Length: 1\r\n\r\nx", 400) ==
                    "{\"error\":\"invalid request\"}",
                "bad JSON handled");
        (void)exchange(port, "GET /missing HTTP/1.1" + host + "\r\n", 404);
        require(exchange(port, "HEAD /health HTTP/1.1" + host + "\r\n", 200).empty(),
                "HEAD sends no body");
        require(exchange(port,
                         "POST /sum HTTP/1.1" + host +
                             "Transfer-Encoding: chunked\r\n\r\n2\r\n[5\r\n3\r\n,7]\r\n0\r\n\r\n",
                         200) == "{\"sum\":12}",
                "chunked JSON calculation");
        (void)exchange(port,
                       "POST /sum HTTP/1.1" + host +
                           "Content-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                       400);
        (void)exchange(port, "GET /health HTTP/1.1" + host + "\r\n", 200);
        server.join();
        if (server_error)
            std::rethrow_exception(server_error);
        require(server_status == 0, "Wood server exit status");

        // Independently construct wire bytes rather than using Wood's serializer.
        wd::StreamScope listener(wd::tcp_listen("127.0.0.1", 0, 5000, test_site), test_site);
        const auto peer_port = wd::tcp_port(listener.stream, test_site);
        std::exception_ptr peer_error;
        std::jthread peer([&] {
            try {
                wd::StreamScope accepted(wd::tcp_accept(listener.stream, test_site), test_site);
                std::string request;
                while (!request.ends_with("\r\n\r\n")) {
                    request += wd::stream_read_line(accepted.stream, 16384, test_site);
                    require(request.size() <= 16384, "client request budget");
                }
                require(request.starts_with("GET /from-cpp HTTP/1.1\r\n"),
                        "Wood client request line");
                require(request.find("Connection: close\r\n") != std::string::npos,
                        "Wood client connection policy");
                wd::stream_write(accepted.stream,
                                 "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\na",
                                 test_site);
                wd::stream_write(accepted.stream, std::string("\0b\r\n0\r\n\r\n", 9), test_site);
            } catch (...) {
                peer_error = std::current_exception();
            }
        });
        const auto client_log = root / "client.log";
        const int client_status =
            wood::run_process({argv[2], "127.0.0.1", std::to_string(peer_port), "/from-cpp"},
                              client_log, {std::chrono::seconds(15), 1048576});
        peer.join();
        if (peer_error)
            std::rethrow_exception(peer_error);
        require(client_status == 0, "Wood HTTP client exit status");
        const auto report = wood::Json::parse(wood::read_file(client_log));
        require(report.at("status").integer() == 200 &&
                    report.at("body").string() == std::string("a\0b", 3),
                "client decodes binary chunked response");
        std::cout << "standalone HTTP server/client applications passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL: " << failure.what() << '\n';
        return 1;
    }
}
