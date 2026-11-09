// Native resource primitives. Included after the checked value runtime; embedded
// verbatim into generated programs. Protocols and copying loops live in Wood.
#ifndef WOOD_STREAMS_HPP
#define WOOD_STREAMS_HPP
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <exception>
#ifdef _WIN32
// wood_runtime.hpp defines WIN32_LEAN_AND_MEAN before including windows.h.
#include <io.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef _MSC_VER
#pragma comment(lib, "ws2_32.lib")
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace wd {
namespace stream_detail {
#ifdef _WIN32
using Socket = SOCKET;
using AddressSize = int;
inline constexpr Socket invalid_socket = INVALID_SOCKET;
inline int socket_error() {
    return WSAGetLastError();
}
inline int close_socket(Socket fd) {
    return closesocket(fd);
}
inline bool interrupted(int code) {
    return code == WSAEINTR;
}
inline bool pending(int code) {
    return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEALREADY;
}
inline void initialize(Site at) {
    struct Winsock {
        int status;
        Winsock() {
            WSADATA data{};
            status = WSAStartup(MAKEWORD(2, 2), &data);
        }
        ~Winsock() {
            if (!status)
                WSACleanup();
        }
    };
    static const Winsock winsock;
    if (winsock.status)
        error(at, "R4017", "cannot initialize native networking");
}
#else
using Socket = int;
using AddressSize = socklen_t;
inline constexpr Socket invalid_socket = -1;
inline int socket_error() {
    return errno;
}
inline int close_socket(Socket fd) {
    return ::close(fd);
}
inline bool interrupted(int code) {
    return code == EINTR;
}
inline bool pending(int code) {
    return code == EAGAIN || code == EWOULDBLOCK || code == EINPROGRESS;
}
inline void initialize(Site) {}
#endif
[[noreturn]] inline void network_error(Site at, const char* action, int code) {
    error(at, "R4017", std::string(action) + " failed (OS error " + std::to_string(code) + ")");
}
inline void timeout_value(Int milliseconds, Site at) {
    if (milliseconds < 1 || milliseconds > 86400000)
        error(at, "R4017", "socket timeout must be in 1..86400000 milliseconds");
}
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
inline Deadline deadline(Int milliseconds) {
    return Clock::now() + std::chrono::milliseconds(milliseconds);
}
inline void check_deadline(Deadline until, Site at) {
    if (native_detail::cancelled())
        error(at, "R4022", "socket operation cancelled");
    if (Clock::now() >= until)
        error(at, "R4018", "socket operation timed out");
}
inline void wait_socket(Socket fd, bool writing, Deadline until, Site at) {
    for (;;) {
        check_deadline(until, at);
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(until - Clock::now());
        const auto milliseconds = std::clamp<std::int64_t>(remaining.count(), 1, 50);
#ifdef _WIN32
        fd_set read_set{}, write_set{}, error_set{};
        FD_ZERO(&read_set);
        FD_ZERO(&write_set);
        FD_ZERO(&error_set);
        if (writing)
            FD_SET(fd, &write_set);
        else
            FD_SET(fd, &read_set);
        FD_SET(fd, &error_set);
        timeval time{};
        time.tv_sec = static_cast<long>(milliseconds / 1000);
        time.tv_usec = static_cast<long>((milliseconds % 1000) * 1000);
        const int result = select(0, &read_set, &write_set, &error_set, &time);
#else
        pollfd descriptor{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
        const int result = poll(&descriptor, 1, static_cast<int>(milliseconds));
#endif
        if (result > 0)
            return;
        if (result == 0)
            continue; // Recheck the logical deadline and cooperative cancellation.
        const auto code = socket_error();
        if (!interrupted(code))
            network_error(at, "socket wait", code);
    }
}
// Ownership starts immediately, including every setup failure path.
struct SocketOwner {
    Socket fd = invalid_socket;
    explicit SocketOwner(Socket value) : fd(value) {}
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner(SocketOwner&& other) noexcept : fd(other.release()) {}
    ~SocketOwner() {
        if (fd != invalid_socket)
            close_socket(fd);
    }
    Socket release() {
        return std::exchange(fd, invalid_socket);
    }
};
inline void configure(Socket fd, Site at) {
#ifdef _WIN32
    u_long enabled = 1;
    if (ioctlsocket(fd, FIONBIO, &enabled) != 0)
        network_error(at, "nonblocking socket setup", socket_error());
    if (!SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0))
        network_error(at, "noninheritable socket setup", static_cast<int>(GetLastError()));
#else
    const int flags = fcntl(fd, F_GETFL);
    const int descriptor_flags = fcntl(fd, F_GETFD);
    if (flags == -1 || descriptor_flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1 ||
        fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC) == -1)
        network_error(at, "socket setup", errno);
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
        network_error(at, "SIGPIPE suppression", errno);
#endif
#endif
}
// Wake either for a completed attempt or the next stagger boundary. Unlike an
// individual I/O wait, reaching this boundary is not itself an operation error.
inline std::vector<bool> connection_ready(const std::vector<SocketOwner>& attempts, Deadline wake,
                                          Site at) {
    std::vector<bool> ready(attempts.size(), false);
    const auto now = Clock::now();
    if (now >= wake)
        return ready;
    const auto milliseconds = std::min<std::int64_t>(50,
        std::chrono::ceil<std::chrono::milliseconds>(wake - now).count());
#ifdef _WIN32
    static_assert(FD_SETSIZE >= 64, "Wood TCP connection racing requires 64 socket slots");
    fd_set writable{}, failures{};
    FD_ZERO(&writable);
    FD_ZERO(&failures);
    for (const auto& attempt : attempts)
        if (attempt.fd != invalid_socket) {
            FD_SET(attempt.fd, &writable);
            FD_SET(attempt.fd, &failures);
        }
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(milliseconds / 1000);
    timeout.tv_usec = static_cast<long>((milliseconds % 1000) * 1000);
    const int result = select(0, nullptr, &writable, &failures, &timeout);
    if (result > 0)
        for (std::size_t i = 0; i < attempts.size(); ++i)
            if (attempts[i].fd != invalid_socket)
                ready[i] =
                    FD_ISSET(attempts[i].fd, &writable) || FD_ISSET(attempts[i].fd, &failures);
#else
    std::vector<pollfd> descriptors;
    for (const auto& attempt : attempts)
        descriptors.push_back({attempt.fd, POLLOUT, 0});
    const int result = poll(descriptors.data(), static_cast<nfds_t>(descriptors.size()),
                            static_cast<int>(milliseconds));
    if (result > 0)
        for (std::size_t i = 0; i < descriptors.size(); ++i)
            ready[i] = descriptors[i].revents != 0;
#endif
    if (result < 0 && !interrupted(socket_error()))
        network_error(at, "connection wait", socket_error());
    return ready;
}
enum class Kind { File, Connection, Listener };
struct SecureTransport {
    virtual ~SecureTransport() = default;
    virtual std::size_t read(char*, std::size_t, Deadline, Site) = 0;
    virtual std::size_t write(const char*, std::size_t, Deadline, Site) = 0;
    virtual void shutdown(Deadline, Site) = 0;
    virtual void finish(Deadline until, Site at) {
        shutdown(until, at);
    }
};
struct State {
    Kind kind;
    std::FILE* file = nullptr;
    Socket socket = invalid_socket;
    bool readable = false, writable = false, eof = false;
    Int timeout_ms = 30000;
    std::string input;
    std::size_t offset = 0;
    std::unique_ptr<SecureTransport> secure;
    explicit State(Kind value) : kind(value) {}
    State(const State&) = delete;
    ~State() {
        (void)close();
    }
    bool closed() const {
        return !file && socket == invalid_socket;
    }
    int close() noexcept {
        secure.reset();
        input.clear();
        offset = 0;
        if (file) {
            auto* previous = std::exchange(file, nullptr);
            return std::fclose(previous) == 0 ? 0 : errno;
        }
        if (socket != invalid_socket) {
            const auto previous = std::exchange(socket, invalid_socket);
            return close_socket(previous) == 0 ? 0 : socket_error();
        }
        return 0;
    }
};
inline void count_value(Int count, Site at) {
    if (count < 0 || count > 16777216)
        error(at, "R4016", "stream byte count must be in 0..16777216");
}
} // namespace stream_detail

// Copying a handle aliases one state. Closing any alias closes every alias.
// Containers/records still copy their values, including these handle identities.
struct Stream {
    std::shared_ptr<stream_detail::State> state;
    bool operator==(const Stream&) const = default;
};
inline bool stream_closed(const Stream& value) {
    return !value.state || value.state->closed();
}
inline std::string to_str(const Stream& value) {
    return stream_closed(value) ? "<stream:closed>" : "<stream:open>";
}
inline stream_detail::State& stream_state(const Stream& value, Site at) {
    if (stream_closed(value))
        error(at, "R4016", "stream is closed");
    return *value.state;
}
inline Stream stream_open(const std::string& path, const std::string& mode, Site at) {
    if (path.find('\0') != std::string::npos)
        error(at, "R4016", "file path contains NUL");
    if (mode != "r" && mode != "w" && mode != "a" && mode != "x")
        error(at, "R4016", "file mode must be r, w, a, or x (binary)");
    auto state = std::make_shared<stream_detail::State>(stream_detail::Kind::File);
#ifdef _WIN32
    const auto native_mode = mode == "r"   ? L"rbN"
                             : mode == "w" ? L"wbN"
                             : mode == "x" ? L"wbxN"
                                           : L"abN";
    (void)_wfopen_s(&state->file, file_path(path, at).c_str(), native_mode);
#else
    state->file = std::fopen(path.c_str(), (mode == "x" ? "wbx" : mode + "b").c_str());
#endif
    if (!state->file)
        error(at, "R4016", "cannot open file stream (OS error " + std::to_string(errno) + ")");
#ifndef _WIN32
    const int fd = fileno(state->file);
    const int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        error(at, "R4016", "cannot configure file stream");
#endif
    state->readable = mode == "r";
    state->writable = mode != "r";
    return {std::move(state)};
}
inline void stream_close(const Stream& value, Site at) {
    if (!value.state)
        return;
    if (value.state->secure) {
        try {
            value.state->secure->finish(stream_detail::deadline(value.state->timeout_ms), at);
        } catch (...) {
            (void)value.state->close();
            throw;
        }
    }
    const int status = value.state->close();
    if (status)
        error(at, value.state->kind == stream_detail::Kind::File ? "R4016" : "R4017",
              "stream close failed (OS error " + std::to_string(status) + ")");
}
struct StreamScope {
    Stream stream;
    Site site;
    int exceptions;
    StreamScope(Stream value, Site at)
        : stream(std::move(value)), site(at), exceptions(std::uncaught_exceptions()) {
        (void)stream_state(stream, at);
    }
    StreamScope(const StreamScope&) = delete;
    ~StreamScope() noexcept(false) {
        // A cleanup error must never replace an error/exit already in flight.
        if (std::uncaught_exceptions() > exceptions)
            (void)stream.state->close();
        else
            stream_close(stream, site);
    }
};
inline std::string stream_read_native(stream_detail::State& state, std::size_t count,
                                      stream_detail::Deadline until, Site at) {
    using namespace stream_detail;
    if (!state.readable)
        error(at, "R4016", "stream is not readable");
    if (count == 0 || state.eof)
        return {};
    std::string result(count, '\0');
    if (state.secure) {
        const auto received = state.secure->read(result.data(), count, until, at);
        state.eof = received == 0;
        result.resize(received);
        return result;
    }
    if (state.kind == Kind::File) {
        const auto received = std::fread(result.data(), 1, count, state.file);
        if (std::ferror(state.file))
            error(at, "R4016", "file stream read failed");
        state.eof = std::feof(state.file) != 0;
        result.resize(received);
        return result;
    }
    for (;;) {
        check_deadline(until, at);
        const auto received = recv(state.socket, result.data(), static_cast<int>(count), 0);
        if (received >= 0) {
            state.eof = received == 0;
            result.resize(static_cast<std::size_t>(received));
            return result;
        }
        const auto code = socket_error();
        if (interrupted(code))
            continue;
        if (!pending(code))
            network_error(at, "socket read", code);
        wait_socket(state.socket, false, until, at);
    }
}
inline std::string stream_read(const Stream& value, Int count, Site at) {
    stream_detail::count_value(count, at);
    auto& state = stream_state(value, at);
    if (!state.readable)
        error(at, "R4016", "stream is not readable");
    if (state.offset < state.input.size()) {
        const auto available =
            std::min(static_cast<std::size_t>(count), state.input.size() - state.offset);
        auto result = state.input.substr(state.offset, available);
        state.offset += available;
        return result;
    }
    state.input.clear();
    state.offset = 0;
    return stream_read_native(state, static_cast<std::size_t>(count),
                              stream_detail::deadline(state.timeout_ms), at);
}
inline std::string stream_read_line(const Stream& value, Int limit, Site at) {
    stream_detail::count_value(limit, at);
    auto& state = stream_state(value, at);
    if (!state.readable)
        error(at, "R4016", "stream is not readable");
    const auto until = stream_detail::deadline(state.timeout_ms);
    if (state.offset) {
        state.input.erase(0, state.offset);
        state.offset = 0;
    }
    for (;;) {
        const auto end = state.input.find('\n');
        const auto size = end == std::string::npos ? state.input.size() : end + 1;
        if (size > static_cast<std::size_t>(limit))
            error(at, "R4016", "stream line exceeds byte limit");
        if (end != std::string::npos || state.eof) {
            state.offset = size;
            return state.input.substr(0, size);
        }
        // One byte beyond the budget distinguishes exact EOF from an oversized
        // line, without allowing a peer to grow an unbounded input buffer.
        state.input += stream_read_native(
            state, std::min<std::size_t>(4096, static_cast<std::size_t>(limit) + 1 - size), until,
            at);
    }
}
inline void stream_write(const Stream& value, const std::string& bytes, Site at) {
    auto& state = stream_state(value, at);
    if (!state.writable)
        error(at, "R4016", "stream is not writable");
    using namespace stream_detail;
    const auto until = deadline(state.timeout_ms);
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min<std::size_t>(bytes.size() - offset, 16777216);
        if (state.secure) {
            offset += state.secure->write(bytes.data() + offset, count, until, at);
        } else if (state.kind == Kind::File) {
            const auto sent = std::fwrite(bytes.data() + offset, 1, count, state.file);
            if (sent == 0 || std::ferror(state.file))
                error(at, "R4016", "file stream write failed");
            offset += sent;
        } else {
            check_deadline(until, at);
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto sent =
                send(state.socket, bytes.data() + offset, static_cast<int>(count), flags);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
                continue;
            }
            const auto code = socket_error();
            if (sent < 0 && interrupted(code))
                continue;
            if (sent < 0 && pending(code)) {
                wait_socket(state.socket, true, until, at);
                continue;
            }
            network_error(at, "socket write", code);
        }
    }
}
inline void stream_flush(const Stream& value, Site at) {
    auto& state = stream_state(value, at);
    if (state.kind != stream_detail::Kind::File || !state.writable)
        error(at, "R4016", "flush requires a writable file stream");
    if (std::fflush(state.file) != 0)
        error(at, "R4016", "file stream flush failed");
}
inline Int stream_tell(const Stream& value, Site at) {
    auto& state = stream_state(value, at);
    if (state.kind != stream_detail::Kind::File)
        error(at, "R4016", "tell requires a file stream");
#ifdef _WIN32
    const auto position = _ftelli64(state.file);
#else
    const auto position = ftello(state.file);
#endif
    if (position < 0 ||
        static_cast<std::uintmax_t>(position) > static_cast<std::uintmax_t>(max_int))
        error(at, "R4016", "file position is unavailable or outside int64 range");
    return static_cast<Int>(position) - static_cast<Int>(state.input.size() - state.offset);
}
inline void stream_seek(const Stream& value, Int offset, Site at) {
    auto& state = stream_state(value, at);
    if (state.kind != stream_detail::Kind::File || offset < 0)
        error(at, "R4016", "seek requires a file stream and nonnegative absolute offset");
#ifdef _WIN32
    const int result = _fseeki64(state.file, offset, SEEK_SET);
#else
    if (static_cast<std::uintmax_t>(offset) >
        static_cast<std::uintmax_t>(std::numeric_limits<off_t>::max()))
        error(at, "R4016", "seek offset exceeds platform file range");
    const int result = fseeko(state.file, static_cast<off_t>(offset), SEEK_SET);
#endif
    if (result != 0)
        error(at, "R4016", "file seek failed");
    state.input.clear();
    state.offset = 0;
    state.eof = false;
}
inline void stream_timeout(const Stream& value, Int milliseconds, Site at) {
    stream_detail::timeout_value(milliseconds, at);
    auto& state = stream_state(value, at);
    if (state.kind == stream_detail::Kind::File)
        error(at, "R4016", "timeouts require a socket stream");
    state.timeout_ms = milliseconds;
}
inline Stream tcp_connect(const std::string& host, Int port, Int milliseconds, Site at) {
    using namespace stream_detail;
    timeout_value(milliseconds, at);
    if (host.empty() || host.find('\0') != std::string::npos || port < 1 || port > 65535)
        error(at, "R4017", "connect requires a nonempty host and port in 1..65535");
    initialize(at);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;
    addrinfo* addresses = nullptr;
    const auto service = std::to_string(port);
    const int lookup = getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses);
    if (lookup != 0)
        network_error(at, "host resolution", lookup);
    const std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(addresses, freeaddrinfo);
    const auto until = deadline(milliseconds); // DNS uses the host resolver's own policy.
    // Keep the OS's first-family preference, interleaving alternatives so one
    // unreachable family cannot consume the whole caller's connection budget.
    std::vector<addrinfo*> preferred, alternate, ordered;
    const int first_family = addresses ? addresses->ai_family : AF_INET6;
    for (auto* address = addresses; address; address = address->ai_next) {
        auto& family = address->ai_family == first_family ? preferred : alternate;
        if (family.size() < 64)
            family.push_back(address);
    }
    for (std::size_t i = 0; ordered.size() < 64 && (i < preferred.size() || i < alternate.size());
         ++i) {
        if (i < preferred.size())
            ordered.push_back(preferred[i]);
        if (i < alternate.size() && ordered.size() < 64)
            ordered.push_back(alternate[i]);
    }
    std::vector<SocketOwner> attempts;
    attempts.reserve(ordered.size());
    const auto connected = [&](SocketOwner& socket_owner) -> Stream {
        auto state = std::make_shared<State>(Kind::Connection);
        state->socket = socket_owner.release();
        state->readable = true;
        state->writable = true;
        state->timeout_ms = milliseconds;
        return {std::move(state)};
    };
    int last_error = 0;
    std::size_t next = 0, active = 0;
    auto next_start = Clock::now();
    while (next < ordered.size() || active > 0) {
        check_deadline(until, at);
        if (next < ordered.size() && (active == 0 || Clock::now() >= next_start)) {
            const auto* address = ordered[next++];
            SocketOwner socket_owner(::socket(address->ai_family, SOCK_STREAM, IPPROTO_TCP));
            if (socket_owner.fd == invalid_socket) {
                last_error = socket_error();
                continue;
            }
            configure(socket_owner.fd, at);
            const int result = ::connect(socket_owner.fd, address->ai_addr,
                                         static_cast<AddressSize>(address->ai_addrlen));
            if (result == 0)
                return connected(socket_owner);
            last_error = socket_error();
            if (!pending(last_error) && !interrupted(last_error))
                continue;
            attempts.push_back(std::move(socket_owner));
            ++active;
            next_start = Clock::now() + std::chrono::milliseconds(250);
        }
        if (!active)
            continue;
        const auto ready = connection_ready(
            attempts, next < ordered.size() ? std::min(until, next_start) : until, at);
        for (std::size_t i = 0; i < attempts.size(); ++i) {
            if (!ready[i])
                continue;
            auto& socket_owner = attempts[i];
            last_error = 0;
            AddressSize size = sizeof(last_error);
            if (getsockopt(socket_owner.fd, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&last_error), &size) != 0)
                last_error = socket_error();
            if (!last_error)
                return connected(socket_owner);
            close_socket(socket_owner.release());
            --active;
            // A definite failure should not delay the next alternative.
            next_start = Clock::now();
        }
    }
    network_error(at, "connect", last_error);
}
inline Stream tcp_listen(const std::string& host, Int port, Int milliseconds, Site at) {
    using namespace stream_detail;
    timeout_value(milliseconds, at);
    if (host.empty() || host.find('\0') != std::string::npos || port < 0 || port > 65535)
        error(at, "R4017", "listen requires an explicit numeric address and port in 0..65535");
    initialize(at);
    sockaddr_in v4{};
    sockaddr_in6 v6{};
    const sockaddr* address = nullptr;
    AddressSize size = 0;
    int family = AF_INET;
    if (inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET;
        v4.sin_port = htons(static_cast<unsigned short>(port));
        size = sizeof(v4);
        address = reinterpret_cast<const sockaddr*>(&v4);
    } else if (inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) == 1) {
        family = AF_INET6;
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(static_cast<unsigned short>(port));
        size = sizeof(v6);
        address = reinterpret_cast<const sockaddr*>(&v6);
    } else
        error(at, "R4017", "listen address must be numeric IPv4 or IPv6");
    SocketOwner socket_owner(::socket(family, SOCK_STREAM, IPPROTO_TCP));
    if (socket_owner.fd == invalid_socket)
        network_error(at, "socket creation", socket_error());
    configure(socket_owner.fd, at);
    const int enabled = 1;
#ifdef _WIN32
    constexpr int reuse_option = SO_EXCLUSIVEADDRUSE;
#else
    constexpr int reuse_option = SO_REUSEADDR;
#endif
    if (setsockopt(socket_owner.fd, SOL_SOCKET, reuse_option,
                   reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0)
        network_error(at, "listen address setup", socket_error());
    if (family == AF_INET6 &&
        setsockopt(socket_owner.fd, IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char*>(&enabled), sizeof(enabled)) != 0)
        network_error(at, "IPv6-only listen setup", socket_error());
    if (::bind(socket_owner.fd, address, size) != 0 || ::listen(socket_owner.fd, SOMAXCONN) != 0)
        network_error(at, "listen", socket_error());
    auto state = std::make_shared<State>(Kind::Listener);
    state->socket = socket_owner.release();
    state->timeout_ms = milliseconds;
    return {std::move(state)};
}
inline Stream tcp_accept(const Stream& listener, Site at) {
    using namespace stream_detail;
    auto& source = stream_state(listener, at);
    if (source.kind != Kind::Listener)
        error(at, "R4017", "accept requires a listening socket");
    const auto until = deadline(source.timeout_ms);
    for (;;) {
        check_deadline(until, at);
        SocketOwner socket_owner(::accept(source.socket, nullptr, nullptr));
        if (socket_owner.fd != invalid_socket) {
            configure(socket_owner.fd, at);
            auto state = std::make_shared<State>(Kind::Connection);
            state->socket = socket_owner.release();
            state->timeout_ms = source.timeout_ms;
            state->readable = true;
            state->writable = true;
            return {std::move(state)};
        }
        const auto code = socket_error();
        if (interrupted(code))
            continue;
        if (!pending(code))
            network_error(at, "accept", code);
        wait_socket(source.socket, false, until, at);
    }
}
inline Int tcp_port(const Stream& value, Site at) {
    using namespace stream_detail;
    auto& state = stream_state(value, at);
    if (state.kind == Kind::File)
        error(at, "R4017", "port requires a socket stream");
    sockaddr_storage address{};
    AddressSize size = sizeof(address);
    if (getsockname(state.socket, reinterpret_cast<sockaddr*>(&address), &size) != 0)
        network_error(at, "local address", socket_error());
    if (address.ss_family == AF_INET)
        return ntohs(reinterpret_cast<const sockaddr_in&>(address).sin_port);
    return ntohs(reinterpret_cast<const sockaddr_in6&>(address).sin6_port);
}
inline void tcp_shutdown_write(const Stream& value, Site at) {
    using namespace stream_detail;
    auto& state = stream_state(value, at);
    if (state.kind != Kind::Connection)
        error(at, "R4017", "shutdown requires a connected socket");
    if (!state.writable)
        return;
    if (state.secure)
        state.secure->shutdown(deadline(state.timeout_ms), at);
#ifdef _WIN32
    constexpr int direction = SD_SEND;
#else
    constexpr int direction = SHUT_WR;
#endif
    if (::shutdown(state.socket, direction) != 0)
        network_error(at, "write shutdown", socket_error());
    state.writable = false;
}
// A prepared destination makes the actual ownership handoff infallible. Thread
// creation can therefore finish before sender aliases are invalidated.
inline void stream_transfer_into(stream_detail::State& old,
                                 stream_detail::State& next) noexcept {
    next.readable = old.readable;
    next.writable = old.writable;
    next.eof = old.eof;
    next.timeout_ms = old.timeout_ms;
    next.input = std::move(old.input);
    next.offset = std::exchange(old.offset, 0);
    next.secure = std::move(old.secure);
    next.socket = std::exchange(old.socket, stream_detail::invalid_socket);
}
// Ownership transfer invalidates all old aliases before exposing the new state.
// There is no mutable state shared across threads after this operation.
inline Stream stream_transfer(const Stream& value, Site at) {
    auto& old = stream_state(value, at);
    if (old.kind != stream_detail::Kind::Connection)
        error(at, "R4016", "transfer requires a connected socket");
    auto next = std::make_shared<stream_detail::State>(old.kind);
    stream_transfer_into(old, *next);
    return {std::move(next)};
}
} // namespace wd
#endif
