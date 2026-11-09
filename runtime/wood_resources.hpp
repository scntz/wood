#ifndef WOOD_RESOURCES_HPP
#define WOOD_RESOURCES_HPP
namespace wd {
inline void resource_validate(const Stream& value, Site at) {
    (void)stream_state(value, at);
}
inline void resource_validate(const Database& value, Site at) {
    (void)db_state(value, at);
}
inline void resource_validate(const Statement& value, Site at) {
    (void)stmt_state(value, at);
}
inline void resource_validate(const Transaction& value, Site at) {
    if (!value.state || !value.state->present())
        error(at, "R4020", "transaction is closed");
}
inline void resource_validate(const TlsContext& value, Site at) {
    if (!value.state || !value.state->handle)
        error(at, "R4021", "TLS context is closed");
}
inline void resource_validate(const Task& value, Site at) {
    (void)task_state(value, at);
}
inline void resource_close(const Stream& value, Site at) {
    stream_close(value, at);
}
inline void resource_close(const Database& value, Site at) {
    db_close(value, at);
}
inline void resource_close(const Statement& value, Site at) {
    stmt_close(value, at);
}
inline void resource_close(const Transaction& value, Site at) {
    db_rollback(value, at);
}
inline void resource_close(const TlsContext& value, Site at) {
    tls_context_close(value, at);
}
inline void resource_close(const Task& value, Site at) {
    task_close(value, at);
}
inline void resource_abandon(const Stream& value) noexcept {
    if (value.state)
        value.state->close();
}
inline void resource_abandon(const Database& value) noexcept {
    if (value.state)
        value.state->close();
}
inline void resource_abandon(const Statement& value) noexcept {
    if (value.state)
        value.state->close();
}
inline void resource_abandon(const Transaction& value) noexcept {
    if (value.state)
        value.state->abandon();
}
inline void resource_abandon(const TlsContext& value) noexcept {
    if (value.state)
        value.state->close();
}
inline void resource_abandon(const Task& value) noexcept {
    if (value.state)
        value.state->close();
}
template <class T> struct ResourceScope {
    T value;
    Site site;
    int exceptions;
    ResourceScope(T input, Site at)
        : value(std::move(input)), site(at), exceptions(std::uncaught_exceptions()) {
        resource_validate(value, at);
    }
    ResourceScope(const ResourceScope&) = delete;
    ~ResourceScope() noexcept(false) {
        if (std::uncaught_exceptions() > exceptions)
            resource_abandon(value);
        else
            resource_close(value, site);
    }
};
} // namespace wd
#endif
