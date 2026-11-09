// Optional OpenSSL 3 transport. No OpenSSL headers, link dependency, or Python.
#ifndef WOOD_TLS_HPP
#define WOOD_TLS_HPP
namespace wd {
namespace tls_detail {
struct SSL;
struct SSL_CTX;
struct SSL_METHOD;
struct BIO;
struct BIO_METHOD;
struct X509;
struct EVP_PKEY;
struct X509_STORE;
struct X509_VERIFY_PARAM;
inline std::filesystem::path directory(Site at) {
    return native_detail::library_directory("WOOD_OPENSSL_DIR", "R4021", at);
}
struct Api {
    Site site;
    std::filesystem::path path;
#ifdef _WIN32
    native_detail::Library crypto{{"libcrypto-3-x64.dll"}, "R4021", site, path};
    native_detail::Library ssl{{"libssl-3-x64.dll"}, "R4021", site, path};
#elif defined(__APPLE__)
    native_detail::Library crypto{{"libcrypto.3.dylib"}, "R4021", site, path};
    native_detail::Library ssl{{"libssl.3.dylib"}, "R4021", site, path};
#else
    native_detail::Library crypto{{"libcrypto.so.3"}, "R4021", site, path};
    native_detail::Library ssl{{"libssl.so.3"}, "R4021", site, path};
#endif
#define WOOD_TLS(lib, name, result, ...)                                                           \
    using name##_type = result (*)(__VA_ARGS__);                                                   \
    name##_type name = lib.symbol<name##_type>(#name, "R4021", site)
    WOOD_TLS(crypto, OpenSSL_version_num, unsigned long);
    WOOD_TLS(crypto, OpenSSL_version, const char*, int);
    WOOD_TLS(crypto, ERR_clear_error, void);
    WOOD_TLS(crypto, ERR_get_error, unsigned long);
    WOOD_TLS(crypto, ERR_error_string_n, void, unsigned long, char*, std::size_t);
    WOOD_TLS(crypto, BIO_new_mem_buf, BIO*, const void*, int);
    WOOD_TLS(crypto, BIO_free, int, BIO*);
    WOOD_TLS(crypto, PEM_read_bio_X509, X509*, BIO*, X509**, int (*)(char*, int, int, void*),
             void*);
    WOOD_TLS(crypto, PEM_read_bio_PrivateKey, EVP_PKEY*, BIO*, EVP_PKEY**,
             int (*)(char*, int, int, void*), void*);
    WOOD_TLS(crypto, X509_free, void, X509*);
    WOOD_TLS(crypto, EVP_PKEY_free, void, EVP_PKEY*);
    WOOD_TLS(crypto, X509_STORE_add_cert, int, X509_STORE*, X509*);
    WOOD_TLS(crypto, X509_VERIFY_PARAM_set1_ip_asc, int, X509_VERIFY_PARAM*, const char*);
    WOOD_TLS(ssl, TLS_method, const SSL_METHOD*);
    WOOD_TLS(ssl, SSL_CTX_new, SSL_CTX*, const SSL_METHOD*);
    WOOD_TLS(ssl, SSL_CTX_free, void, SSL_CTX*);
    WOOD_TLS(ssl, SSL_CTX_ctrl, long, SSL_CTX*, int, long, void*);
    WOOD_TLS(ssl, SSL_CTX_set_options, std::uint64_t, SSL_CTX*, std::uint64_t);
    WOOD_TLS(ssl, SSL_CTX_set_security_level, void, SSL_CTX*, int);
    WOOD_TLS(ssl, SSL_CTX_set_verify, void, SSL_CTX*, int, int (*)(int, void*));
    WOOD_TLS(ssl, SSL_CTX_set_default_verify_paths, int, SSL_CTX*);
    WOOD_TLS(ssl, SSL_CTX_get_cert_store, X509_STORE*, const SSL_CTX*);
    WOOD_TLS(ssl, SSL_CTX_use_certificate, int, SSL_CTX*, X509*);
    WOOD_TLS(ssl, SSL_CTX_use_PrivateKey, int, SSL_CTX*, EVP_PKEY*);
    WOOD_TLS(ssl, SSL_CTX_check_private_key, int, const SSL_CTX*);
    WOOD_TLS(ssl, SSL_new, SSL*, SSL_CTX*);
    WOOD_TLS(ssl, SSL_free, void, SSL*);
    WOOD_TLS(ssl, SSL_set_bio, void, SSL*, BIO*, BIO*);
    WOOD_TLS(ssl, SSL_ctrl, long, SSL*, int, long, void*);
    WOOD_TLS(ssl, SSL_set1_host, int, SSL*, const char*);
    WOOD_TLS(ssl, SSL_set_hostflags, void, SSL*, unsigned int);
    WOOD_TLS(ssl, SSL_get0_param, X509_VERIFY_PARAM*, SSL*);
    WOOD_TLS(ssl, SSL_connect, int, SSL*);
    WOOD_TLS(ssl, SSL_accept, int, SSL*);
    WOOD_TLS(ssl, SSL_read_ex, int, SSL*, void*, std::size_t, std::size_t*);
    WOOD_TLS(ssl, SSL_write_ex, int, SSL*, const void*, std::size_t, std::size_t*);
    WOOD_TLS(ssl, SSL_get_error, int, const SSL*, int);
    WOOD_TLS(ssl, SSL_shutdown, int, SSL*);
    // A custom BIO preserves full Windows SOCKET values and uses MSG_NOSIGNAL
    // on POSIX. TLS must not change the application's process signal policy.
    WOOD_TLS(crypto, BIO_get_new_index, int);
    WOOD_TLS(crypto, BIO_meth_new, BIO_METHOD*, int, const char*);
    WOOD_TLS(crypto, BIO_meth_free, void, BIO_METHOD*);
    WOOD_TLS(crypto, BIO_meth_set_write, int, BIO_METHOD*, int (*)(BIO*, const char*, int));
    WOOD_TLS(crypto, BIO_meth_set_read, int, BIO_METHOD*, int (*)(BIO*, char*, int));
    WOOD_TLS(crypto, BIO_meth_set_ctrl, int, BIO_METHOD*, long (*)(BIO*, int, long, void*));
    WOOD_TLS(crypto, BIO_meth_set_create, int, BIO_METHOD*, int (*)(BIO*));
    WOOD_TLS(crypto, BIO_new, BIO*, const BIO_METHOD*);
    WOOD_TLS(crypto, BIO_set_data, void, BIO*, void*);
    WOOD_TLS(crypto, BIO_get_data, void*, BIO*);
    WOOD_TLS(crypto, BIO_set_init, void, BIO*, int);
    WOOD_TLS(crypto, BIO_set_flags, void, BIO*, int);
    WOOD_TLS(crypto, BIO_clear_flags, void, BIO*, int);
#undef WOOD_TLS
    explicit Api(Site at) : site(at), path(directory(at)) {
        if ((OpenSSL_version_num() >> 28) != 3)
            error(at, "R4021", "OpenSSL major version 3 is required");
    }
    [[noreturn]] void fail(const char* action, Site at) {
        char detail[256]{};
        ERR_error_string_n(ERR_get_error(), detail, sizeof(detail));
        error(at, "R4021", std::string(action) + ": " + detail);
    }
    void check(int status, const char* action, Site at) {
        if (status != 1)
            fail(action, at);
    }
};
inline Api& api(Site at) {
    static Api instance(at);
    return instance;
}
inline Site internal_site() {
    return Site{"<TLS BIO>", 0, 0};
}
struct SocketData {
    stream_detail::Socket fd;
};
inline int bio_create(BIO* bio) {
    auto& a = api(internal_site());
    a.BIO_set_init(bio, 1);
    return 1;
}
inline long bio_ctrl(BIO*, int command, long, void*) {
    return command == 11 ? 1L : 0L;
} // FLUSH
inline int bio_read(BIO* bio, char* bytes, int length) {
    auto& a = api(internal_site());
    auto* data = static_cast<SocketData*>(a.BIO_get_data(bio));
    a.BIO_clear_flags(bio, 15);
    for (;;) {
        const auto result = recv(data->fd, bytes, length, 0);
        if (result >= 0)
            return static_cast<int>(result);
        const int code = stream_detail::socket_error();
        if (stream_detail::interrupted(code))
            continue;
        if (stream_detail::pending(code))
            a.BIO_set_flags(bio, 9); // READ | SHOULD_RETRY
        return -1;
    }
}
inline int bio_write(BIO* bio, const char* bytes, int length) {
    auto& a = api(internal_site());
    auto* data = static_cast<SocketData*>(a.BIO_get_data(bio));
    a.BIO_clear_flags(bio, 15);
    for (;;) {
#ifdef MSG_NOSIGNAL
        const auto result = send(data->fd, bytes, length, MSG_NOSIGNAL);
#else
        const auto result = send(data->fd, bytes, length, 0);
#endif
        if (result >= 0)
            return static_cast<int>(result);
        const int code = stream_detail::socket_error();
        if (stream_detail::interrupted(code))
            continue;
        if (stream_detail::pending(code))
            a.BIO_set_flags(bio, 10); // WRITE | SHOULD_RETRY
        return -1;
    }
}
inline BIO_METHOD* socket_method(Api& a, Site at) {
    struct Method {
        Api& a;
        BIO_METHOD* value = nullptr;
        explicit Method(Api& api, Site at) : a(api) {
            const int index = a.BIO_get_new_index();
            if (index == -1)
                error(at, "R4021", "allocate socket BIO type");
            value = a.BIO_meth_new(index | 0x400, "Wood nonblocking socket"); // SOURCE_SINK
            if (!value)
                a.fail("create socket BIO method", at);
            if (!(a.BIO_meth_set_read(value, bio_read) && a.BIO_meth_set_write(value, bio_write) &&
                  a.BIO_meth_set_create(value, bio_create) &&
                  a.BIO_meth_set_ctrl(value, bio_ctrl))) {
                a.BIO_meth_free(value);
                a.fail("configure socket BIO method", at);
            }
        }
        ~Method() {
            a.BIO_meth_free(value);
        }
    };
    static Method method(a, at);
    return method.value;
}
struct Context {
    Api& api;
    SSL_CTX* handle = nullptr;
    explicit Context(Api& value, Site at) : api(value), handle(api.SSL_CTX_new(api.TLS_method())) {
        if (!handle)
            api.fail("create TLS context", at);
        if (api.SSL_CTX_ctrl(handle, 123, 0x0303, nullptr) != 1) {
            api.SSL_CTX_free(handle);
            handle = nullptr;
            api.fail("set TLS 1.2 minimum", at);
        }
        api.SSL_CTX_set_security_level(handle, 2);
        api.SSL_CTX_set_options(handle,
                                (std::uint64_t{1} << 17) |
                                    (std::uint64_t{1} << 30)); // no compression/renegotiation
    }
    void close() noexcept {
        if (handle)
            api.SSL_CTX_free(std::exchange(handle, nullptr));
    }
    ~Context() {
        close();
    }
};
inline std::string pem_file(const std::string& path, Site at) {
    if (path.find('\0') != std::string::npos)
        error(at, "R4021", "PEM path contains NUL");
    std::ifstream input(file_path(path, at), std::ios::binary);
    if (!input)
        error(at, "R4021", "cannot open PEM file");
    std::string result(1048577, '\0');
    input.read(result.data(), static_cast<std::streamsize>(result.size()));
    if (input.bad())
        error(at, "R4021", "cannot read PEM file");
    result.resize(static_cast<std::size_t>(input.gcount()));
    if (result.empty() || result.size() > 1048576)
        error(at, "R4021", "PEM file must contain 1..1048576 bytes");
    return result;
}
inline int no_password(char*, int, int, void*) {
    return -1; // Encrypted PEM is unsupported; never invoke a terminal prompt.
}
template <class Consumer>
void certificates(Api& a, const std::string& path, Consumer consume, Site at) {
    const auto bytes = pem_file(path, at);
    std::size_t offset = 0;
    unsigned count = 0;
    while (offset < bytes.size()) {
        while (offset < bytes.size() && (bytes[offset] == ' ' || bytes[offset] == '\t' ||
                                         bytes[offset] == '\r' || bytes[offset] == '\n'))
            ++offset;
        if (offset == bytes.size())
            break;
        constexpr std::string_view begin = "-----BEGIN CERTIFICATE-----",
                                   end = "-----END CERTIFICATE-----";
        const auto last = bytes.find(end, offset);
        if (bytes.compare(offset, begin.size(), begin) != 0 || last == std::string::npos ||
            ++count > 512)
            error(at, "R4021", "invalid PEM certificate bundle (maximum 512 certificates)");
        const auto length = last + end.size() - offset;
        BIO* bio = a.BIO_new_mem_buf(bytes.data() + offset, static_cast<int>(length));
        if (!bio)
            a.fail("read certificate PEM", at);
        auto* cert = a.PEM_read_bio_X509(bio, nullptr, no_password, nullptr);
        a.BIO_free(bio);
        if (!cert)
            a.fail("parse certificate PEM", at);
        try {
            consume(cert, count);
        } catch (...) {
            a.X509_free(cert);
            throw;
        }
        a.X509_free(cert);
        offset += length;
    }
    if (!count)
        error(at, "R4021", "PEM bundle contains no certificates");
}
struct Transport final : stream_detail::SecureTransport {
    Api& a;
    SocketData socket;
    SSL* ssl = nullptr;
    bool sent_shutdown = false;
    bool failed = false;
    Transport(Context& context, stream_detail::Socket fd, Site at)
        : a(context.api), socket{fd}, ssl(a.SSL_new(context.handle)) {
        if (!ssl)
            a.fail("create TLS connection", at);
        BIO* bio = nullptr;
        try {
            bio = a.BIO_new(socket_method(a, at));
            if (!bio)
                a.fail("create socket BIO", at);
            a.BIO_set_data(bio, &socket);
            a.SSL_set_bio(ssl, bio, bio); // SSL owns one shared read/write BIO
        } catch (...) {
            if (bio)
                a.BIO_free(bio);
            a.SSL_free(ssl);
            throw;
        }
    }
    ~Transport() override {
        a.SSL_free(ssl);
    }
    bool retry(int result, stream_detail::Deadline until, Site at, const char* action,
               bool eof_allowed = false) {
        const int status = a.SSL_get_error(ssl, result);
        if (status == 2 || status == 3) {
            stream_detail::wait_socket(socket.fd, status == 3, until, at);
            return true;
        }
        if (status == 6 && eof_allowed)
            return false;
        failed = true;
        a.fail(action, at); // unclean TCP EOF is an error, never a TLS EOF
    }
    void handshake(bool server, stream_detail::Deadline until, Site at) {
        for (;;) {
            stream_detail::check_deadline(until, at);
            a.ERR_clear_error();
            const int result = server ? a.SSL_accept(ssl) : a.SSL_connect(ssl);
            if (result == 1)
                return;
            retry(result, until, at, "TLS handshake failed");
        }
    }
    std::size_t read(char* bytes, std::size_t count, stream_detail::Deadline until,
                     Site at) override {
        if (failed)
            error(at, "R4021", "TLS connection has failed");
        for (;;) {
            stream_detail::check_deadline(until, at);
            a.ERR_clear_error();
            std::size_t received = 0;
            const int result = a.SSL_read_ex(ssl, bytes, count, &received);
            if (result == 1)
                return received;
            if (!retry(result, until, at, "TLS read failed", true))
                return 0;
        }
    }
    std::size_t write(const char* bytes, std::size_t count, stream_detail::Deadline until,
                      Site at) override {
        if (failed)
            error(at, "R4021", "TLS connection has failed");
        for (;;) {
            stream_detail::check_deadline(until, at);
            a.ERR_clear_error();
            std::size_t sent = 0;
            const int result = a.SSL_write_ex(ssl, bytes, count, &sent);
            if (result == 1 && sent)
                return sent;
            retry(result, until, at, "TLS write failed");
        }
    }
    void shutdown(stream_detail::Deadline until, Site at) override {
        if (sent_shutdown || failed)
            return;
        for (;;) {
            stream_detail::check_deadline(until, at);
            a.ERR_clear_error();
            const int result = a.SSL_shutdown(ssl);
            // First shutdown sends close_notify. Do not wait for reciprocal EOF.
            if (result >= 0) {
                sent_shutdown = true;
                return;
            }
            retry(result, until, at, "TLS shutdown failed");
        }
    }
    void finish(stream_detail::Deadline until, Site at) override {
        // OpenSSL forbids SSL_shutdown after a fatal protocol/I/O failure.
        // Explicit close still releases the transport; it sends no more records.
        if (failed)
            return;
        shutdown(until, at);
        // Closing a TCP socket with unread records can reset the connection and
        // discard the response at the peer. Stage close: send TLS alert, half
        // close TCP, then drain incoming bytes to FIN under the same deadline.
        // Discarding at explicit close does not expose unauthenticated data.
#ifdef _WIN32
        constexpr int direction = SD_SEND;
#else
        constexpr int direction = SHUT_WR;
#endif
        if (::shutdown(socket.fd, direction) != 0)
            stream_detail::network_error(at, "TLS transport write shutdown",
                                         stream_detail::socket_error());
        char pending[16384];
        for (;;) {
            stream_detail::check_deadline(until, at);
            const auto received = recv(socket.fd, pending, sizeof(pending), 0);
            if (received == 0)
                return;
            if (received > 0)
                continue;
            const int code = stream_detail::socket_error();
            if (stream_detail::interrupted(code))
                continue;
            if (stream_detail::pending(code)) {
                stream_detail::wait_socket(socket.fd, false, until, at);
                continue;
            }
#ifdef _WIN32
            if (code == WSAECONNRESET)
                return;
#else
            if (code == ECONNRESET)
                return;
#endif
            stream_detail::network_error(at, "TLS transport close drain", code);
        }
    }
};
} // namespace tls_detail
struct TlsContext {
    std::shared_ptr<tls_detail::Context> state;
    bool operator==(const TlsContext&) const = default;
};
inline std::string to_str(const TlsContext& context) {
    return context.state && context.state->handle ? "<tls_context:open>" : "<tls_context:closed>";
}
inline void tls_context_close(const TlsContext& context, Site) {
    if (context.state)
        context.state->close();
}
inline TlsContext tls_context_new(const std::string& certificate, const std::string& key, Site at) {
    auto& a = tls_detail::api(at);
    a.ERR_clear_error();
    auto context = std::make_shared<tls_detail::Context>(a, at);
    tls_detail::certificates(
        a, certificate,
        [&](tls_detail::X509* cert, unsigned index) {
            if (index == 1)
                a.check(a.SSL_CTX_use_certificate(context->handle, cert),
                        "install server certificate", at);
            else {
                // SSL_CTX_add1_chain_cert: increment reference; original remains ours.
                if (a.SSL_CTX_ctrl(context->handle, 89, 1, cert) != 1)
                    a.fail("install server certificate chain", at);
            }
        },
        at);
    const auto bytes = tls_detail::pem_file(key, at);
    auto* bio = a.BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size()));
    if (!bio)
        a.fail("read private key PEM", at);
    auto* private_key = a.PEM_read_bio_PrivateKey(bio, nullptr, tls_detail::no_password, nullptr);
    a.BIO_free(bio);
    if (!private_key)
        a.fail("parse unencrypted private key PEM", at);
    const int installed = a.SSL_CTX_use_PrivateKey(context->handle, private_key);
    a.EVP_PKEY_free(private_key);
    a.check(installed, "install private key", at);
    a.check(a.SSL_CTX_check_private_key(context->handle), "certificate/private key mismatch", at);
    return {std::move(context)};
}
inline Stream tls_accept(const Stream& connection, const TlsContext& context, Site at) {
    if (!context.state || !context.state->handle)
        error(at, "R4021", "TLS context is closed");
    auto& old = stream_state(connection, at);
    if (old.secure || old.eof || !old.readable || !old.writable || old.offset < old.input.size())
        error(at, "R4021", "TLS upgrade requires an unread, full-duplex plain TCP connection");
    auto result = stream_transfer(connection, at);
    auto transport =
        std::make_unique<tls_detail::Transport>(*context.state, result.state->socket, at);
    transport->handshake(true, stream_detail::deadline(result.state->timeout_ms), at);
    result.state->secure = std::move(transport);
    return result;
}
inline Stream tls_connect(const std::string& host, Int port, const std::string& ca, Int timeout,
                          Site at) {
    stream_detail::timeout_value(timeout, at);
    if (host.empty() || host.size() > 253 || host.find('\0') != std::string::npos)
        error(at, "R4021", "TLS requires a nonempty ASCII hostname or numeric IP");
    for (unsigned char byte : host)
        if (byte <= 32 || byte >= 127)
            error(at, "R4021",
                  "TLS hostname must be ASCII; encode international DNS names with IDNA");
    auto& a = tls_detail::api(at);
    a.ERR_clear_error();
    tls_detail::Context context(a, at);
    a.SSL_CTX_set_verify(context.handle, 1, nullptr); // VERIFY_PEER, default verification callback
    if (ca.empty())
        a.check(a.SSL_CTX_set_default_verify_paths(context.handle),
                "load default OpenSSL trust roots", at);
    else
        tls_detail::certificates(
            a, ca,
            [&](tls_detail::X509* cert, unsigned) {
                a.check(a.X509_STORE_add_cert(a.SSL_CTX_get_cert_store(context.handle), cert),
                        "load trusted certificate", at);
            },
            at);
    // DNS follows OS policy. TCP establishment and TLS handshake each have an
    // explicit timeout, as do subsequent logical stream operations.
    auto result = tcp_connect(host, port, timeout, at);
    const auto until = stream_detail::deadline(timeout);
    auto transport = std::make_unique<tls_detail::Transport>(context, result.state->socket, at);
    unsigned char address[16]{};
    if (inet_pton(AF_INET, host.c_str(), address) == 1 ||
        inet_pton(AF_INET6, host.c_str(), address) == 1)
        a.check(a.X509_VERIFY_PARAM_set1_ip_asc(a.SSL_get0_param(transport->ssl), host.c_str()),
                "set TLS IP verification", at);
    else {
        a.check(a.SSL_set1_host(transport->ssl, host.c_str()), "set TLS hostname verification", at);
        a.SSL_set_hostflags(transport->ssl, 4 | 32); // NO_PARTIAL_WILDCARDS | NEVER_CHECK_SUBJECT
        if (a.SSL_ctrl(transport->ssl, 55, 0, const_cast<char*>(host.c_str())) != 1)
            a.fail("set TLS SNI hostname", at);
    }
    transport->handshake(false, until, at);
    result.state->secure = std::move(transport);
    return result;
}
inline std::string tls_version(Site at) {
    return tls_detail::api(at).OpenSSL_version(0);
}
} // namespace wd
#endif
