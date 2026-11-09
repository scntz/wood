#include "wood_runtime.hpp"
#include <functional>
#include <thread>

namespace {
constexpr wd::Site test_site{"stream_test.wd", 7, 3};
int checks = 0;
const char* phase = "file streams";
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
void fails(const std::function<void()>& action, const char* code) {
    bool failed = false;
    try {
        action();
    } catch (const wd::Error& failure) {
        failed = true;
        check(failure.code == code && failure.site.line == 7, "error code and source location");
    }
    check(failed, "expected stream error");
}
} // namespace
int main() {
    try {
        using namespace wd;
        const auto root =
            std::filesystem::temp_directory_path() /
            ("wood-stream-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!std::filesystem::create_directory(root))
            throw std::runtime_error("fixture directory exists");
        struct Cleanup {
            std::filesystem::path root;
            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(root, ignored);
            }
        } cleanup{root};
        const auto path = path_string(
            root / std::filesystem::path(std::u8string(u8"bytes-árbol.bin")), test_site);
        Stream alias;
        {
            StreamScope scope(stream_open(path, "x", test_site), test_site);
            alias = scope.stream;
            stream_write(alias, std::string("a\0b\r\nlast", 9), test_site);
            stream_flush(alias, test_site);
            check(stream_tell(alias, test_site) == 9, "binary output position");
            fails([&] { (void)stream_read(alias, 1, test_site); }, "R4016");
            fails([&] { (void)stream_open(path, "x", test_site); }, "R4016");
        }
        check(stream_closed(alias), "scope closes surviving aliases");
        stream_close(alias, test_site);
        {
            StreamScope scope(stream_open(path, "r", test_site), test_site);
            auto file = scope.stream;
            check(stream_read_line(file, 5, test_site) == std::string("a\0b\r\n", 5),
                  "binary line");
            check(stream_tell(file, test_site) == 5, "tell accounts for buffered read ahead");
            check(stream_read(file, 2, test_site) == "l",
                  "mixed read consumes buffered data first");
            check(stream_read(file, 1, test_site) == "a", "read resumes after buffered short read");
            check(stream_tell(file, test_site) == 7, "tell after buffered read");
            stream_seek(file, 0, test_site);
            check(stream_read(file, 9, test_site) == std::string("a\0b\r\nlast", 9),
                  "seek clears read ahead");
            check(stream_read(file, 1, test_site).empty(), "EOF");
            stream_seek(file, 5, test_site);
            check(stream_read_line(file, 4, test_site) == "last", "exact line budget at EOF");
            fails([&] { stream_seek(file, -1, test_site); }, "R4016");
            fails([&] { stream_write(file, "bad", test_site); }, "R4016");
            fails([&] { stream_flush(file, test_site); }, "R4016");
            fails([&] { stream_timeout(file, 1, test_site); }, "R4016");
            fails([&] { (void)tcp_accept(file, test_site); }, "R4017");
            fails([&] { (void)stream_read(file, 16777217, test_site); }, "R4016");
            fails([&] { (void)stream_read(file, -1, test_site); }, "R4016");
        }
        fails([&] { (void)stream_open(std::string("a\0b", 3), "r", test_site); }, "R4016");
        fails([&] { (void)stream_open(path, "r+", test_site); }, "R4016");
        fails([&] { StreamScope scope(alias, test_site); }, "R4016");
        auto exceptional = stream_open(path, "r", test_site);
        try {
            StreamScope scope(exceptional, test_site);
            throw Exit{7};
        } catch (const Exit& exit) {
            check(exit.code == 7, "exit preserved");
        }
        check(stream_closed(exceptional), "exit closes resource");
#ifndef _WIN32
        if (std::filesystem::exists("/dev/full")) {
            // A real OS flush failure: normal scope exit reports it; unwind
            // preserves the primary exception rather than terminating/replacing it.
            auto full = stream_open("/dev/full", "w", test_site);
            fails(
                [&] {
                    StreamScope scope(full, test_site);
                    stream_write(full, "x", test_site);
                },
                "R4016");
            check(stream_closed(full), "failed close still invalidates handle");
            full = stream_open("/dev/full", "w", test_site);
            try {
                StreamScope scope(full, test_site);
                stream_write(full, "x", test_site);
                throw Exit{8};
            } catch (const Exit& exit) {
                check(exit.code == 8, "primary error survives close failure");
            }
            check(stream_closed(full), "unwind closes failed output");
        }
#endif
        phase = "IPv4 setup and negative operations";
        StreamScope listener(tcp_listen("127.0.0.1", 0, 2000, test_site), test_site);
        const auto port = tcp_port(listener.stream, test_site);
        check(port > 0 && port <= 65535, "ephemeral port");
        fails([&] { (void)tcp_listen("127.0.0.1", port, 20, test_site); }, "R4017");
        fails([&] { (void)tcp_listen("localhost", 0, 20, test_site); }, "R4017");
        fails([&] { (void)tcp_listen("127.0.0.1", -1, 20, test_site); }, "R4017");
        fails([&] { (void)tcp_connect("127.0.0.1", 0, 20, test_site); }, "R4017");
        fails([&] { (void)tcp_connect(std::string("a\0b", 3), port, 20, test_site); }, "R4017");
        fails([&] { stream_timeout(listener.stream, 0, test_site); }, "R4017");
        fails([&] { stream_timeout(listener.stream, 86400001, test_site); }, "R4017");
        stream_timeout(listener.stream, 15, test_site);
        fails([&] { (void)tcp_accept(listener.stream, test_site); }, "R4018");
        stream_timeout(listener.stream, 2000, test_site);
        phase = "localhost resolution and connection";
        StreamScope client(tcp_connect("localhost", port, 2000, test_site), test_site);
        StreamScope accepted(tcp_accept(listener.stream, test_site), test_site);
        stream_timeout(accepted.stream, 15, test_site);
        fails([&] { (void)stream_read(accepted.stream, 1, test_site); }, "R4018");
        stream_timeout(accepted.stream, 2000, test_site);
        fails([&] { stream_seek(client.stream, 0, test_site); }, "R4016");
        fails([&] { (void)stream_tell(client.stream, test_site); }, "R4016");
        fails([&] { tcp_shutdown_write(listener.stream, test_site); }, "R4017");
        // Transfer larger than the runtime's single-read allocation limit, with a
        // concurrent OS consumer so partial sends/backpressure are exercised.
        phase = "large TCP transfer";
        stream_timeout(client.stream, 10000, test_site);
        stream_timeout(accepted.stream, 10000, test_site);
        const std::string payload(20 * 1024 * 1024, 'z');
        std::exception_ptr worker_error;
        std::size_t received = 0;
        std::jthread worker([&] {
            try {
                for (;;) {
                    auto part = stream_read(accepted.stream, 65536, test_site);
                    if (part.empty())
                        break;
                    check(part.find_first_not_of('z') == std::string::npos, "large transfer bytes");
                    received += part.size();
                }
            } catch (...) {
                worker_error = std::current_exception();
            }
        });
        stream_write(client.stream, payload, test_site);
        tcp_shutdown_write(client.stream, test_site);
        tcp_shutdown_write(client.stream, test_site);
        worker.join();
        if (worker_error)
            std::rethrow_exception(worker_error);
        check(received == payload.size(), "complete large TCP transfer");
        fails([&] { stream_write(client.stream, "x", test_site); }, "R4016");
        phase = "half-close reply";
        stream_write(accepted.stream, "reply", test_site);
        tcp_shutdown_write(accepted.stream, test_site);
        std::string reply;
        while (reply.size() < 5) {
            auto part = stream_read(client.stream, static_cast<Int>(5 - reply.size()), test_site);
            check(!part.empty(), "reply not truncated");
            reply += part;
        }
        check(reply == "reply", "half-close retains readable side");
        check(stream_read(client.stream, 1, test_site).empty(), "TCP orderly EOF");
#ifdef _WIN32
        DWORD handles_before = 0, handles_after = 0;
        check(GetProcessHandleCount(GetCurrentProcess(), &handles_before) != 0,
              "query handle baseline");
#endif
        // Each iteration closes the listener before binding the same port again.
        for (int i = 0; i < 100; ++i) {
            StreamScope loop(tcp_listen("127.0.0.1", 0, 100, test_site), test_site);
            check(tcp_port(loop.stream, test_site) != 0, "repeated scoped sockets");
        }
        for (int i = 0; i < 5; ++i) {
            StreamScope fallback(tcp_connect("localhost", port, 2000, test_site), test_site);
            StreamScope peer(tcp_accept(listener.stream, test_site), test_site);
        }
#ifdef _WIN32
        check(GetProcessHandleCount(GetCurrentProcess(), &handles_after) != 0,
              "query final handle count");
        check(handles_after <= handles_before + 1,
              "scopes and losing connection attempts do not leak handles");
#endif
        phase = "IPv6 loopback";
        StreamScope ipv6_listener(tcp_listen("::1", 0, 2000, test_site), test_site);
        StreamScope ipv6_client(
            tcp_connect("::1", tcp_port(ipv6_listener.stream, test_site), 2000, test_site),
            test_site);
        StreamScope ipv6_server(tcp_accept(ipv6_listener.stream, test_site), test_site);
        stream_write(ipv6_client.stream, "6", test_site);
        check(stream_read(ipv6_server.stream, 1, test_site) == "6", "native IPv6 loopback");
        std::cout << checks << " native stream checks passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "FAIL (" << phase << "): " << failure.what() << '\n';
        return 1;
    }
}
