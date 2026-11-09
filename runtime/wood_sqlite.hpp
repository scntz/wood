// SQLite's stable C ABI is loaded only when database functionality is used.
#ifndef WOOD_SQLITE_HPP
#define WOOD_SQLITE_HPP
namespace wd {
namespace sqlite_detail {
struct sqlite3;
struct sqlite3_stmt;
using Integer = long long;
using Destructor = void (*)(void*);
struct Api {
    Site site;
    std::filesystem::path path;
#ifdef _WIN32
    native_detail::Library library{path.empty() ? std::vector<std::string>{"winsqlite3.dll"}
                                                : std::vector<std::string>{"sqlite3.dll"},
                                   "R4020", site, path};
#elif defined(__APPLE__)
    native_detail::Library library{path.empty()
                                       ? std::vector<std::string>{"/usr/lib/libsqlite3.dylib"}
                                       : std::vector<std::string>{"libsqlite3.dylib"},
                                   "R4020", site, path};
#else
    native_detail::Library library{{"libsqlite3.so.0", "libsqlite3.so"}, "R4020", site, path};
#endif
    // Function declarations are intentionally kept next to the loaded symbols.
#define WOOD_SQLITE(name, result, ...)                                                             \
    using name##_type = result (*)(__VA_ARGS__);                                                   \
    name##_type name = library.symbol<name##_type>("sqlite3_" #name, "R4020", site)
    WOOD_SQLITE(libversion, const char*);
    WOOD_SQLITE(libversion_number, int);
    WOOD_SQLITE(threadsafe, int);
    WOOD_SQLITE(open_v2, int, const char*, sqlite3**, int, const char*);
    WOOD_SQLITE(close_v2, int, sqlite3*);
    WOOD_SQLITE(errmsg, const char*, sqlite3*);
    WOOD_SQLITE(errcode, int, sqlite3*);
    WOOD_SQLITE(extended_result_codes, int, sqlite3*, int);
    WOOD_SQLITE(busy_timeout, int, sqlite3*, int);
    WOOD_SQLITE(progress_handler, void, sqlite3*, int, int (*)(void*), void*);
    WOOD_SQLITE(limit, int, sqlite3*, int, int);
    WOOD_SQLITE(db_config, int, sqlite3*, int, ...);
    WOOD_SQLITE(prepare_v2, int, sqlite3*, const char*, int, sqlite3_stmt**, const char**);
    WOOD_SQLITE(finalize, int, sqlite3_stmt*);
    WOOD_SQLITE(step, int, sqlite3_stmt*);
    WOOD_SQLITE(reset, int, sqlite3_stmt*);
    WOOD_SQLITE(clear_bindings, int, sqlite3_stmt*);
    WOOD_SQLITE(bind_parameter_count, int, sqlite3_stmt*);
    WOOD_SQLITE(bind_int64, int, sqlite3_stmt*, int, Integer);
    WOOD_SQLITE(bind_double, int, sqlite3_stmt*, int, double);
    WOOD_SQLITE(bind_text, int, sqlite3_stmt*, int, const char*, int, Destructor);
    WOOD_SQLITE(bind_blob, int, sqlite3_stmt*, int, const void*, int, Destructor);
    WOOD_SQLITE(bind_null, int, sqlite3_stmt*, int);
    WOOD_SQLITE(column_count, int, sqlite3_stmt*);
    WOOD_SQLITE(column_name, const char*, sqlite3_stmt*, int);
    WOOD_SQLITE(column_type, int, sqlite3_stmt*, int);
    WOOD_SQLITE(column_int64, Integer, sqlite3_stmt*, int);
    WOOD_SQLITE(column_double, double, sqlite3_stmt*, int);
    WOOD_SQLITE(column_text, const unsigned char*, sqlite3_stmt*, int);
    WOOD_SQLITE(column_blob, const void*, sqlite3_stmt*, int);
    WOOD_SQLITE(column_bytes, int, sqlite3_stmt*, int);
    WOOD_SQLITE(changes64, Integer, sqlite3*);
    WOOD_SQLITE(last_insert_rowid, Integer, sqlite3*);
#undef WOOD_SQLITE
    explicit Api(Site at)
        : site(at), path(native_detail::library_directory("WOOD_SQLITE_DIR", "R4020", at)) {
#ifdef _WIN32
        if (sizeof(void*) != 8)
            error(at, "R4020", "the optional SQLite adapter requires a 64-bit Windows target");
#endif
        if (libversion_number() < 3038000 || !threadsafe())
            error(at, "R4020", "SQLite 3.38+ with thread safety is required");
    }
};
inline Api& api(Site at) {
    static Api instance(at);
    return instance;
}
inline void utf8(const std::string& text, Site at) {
    for (std::size_t i = 0; i < text.size();) {
        const auto count = wood::json_detail::utf8_length(text, i);
        if (!count)
            error(at, "R4020", "SQLite text must be valid UTF-8; use a blob for arbitrary bytes");
        i += count;
    }
}
struct StatementState;
struct DatabaseState {
    Api& api;
    sqlite3* handle = nullptr;
    std::thread::id owner = std::this_thread::get_id();
    Int timeout_ms;
    std::vector<std::weak_ptr<StatementState>> statements;
    std::vector<std::uint64_t> transactions;
    std::uint64_t next_transaction = 0;
    DatabaseState(Api& value, Int timeout) : api(value), timeout_ms(timeout) {}
    void close() noexcept;
    ~DatabaseState() {
        close();
    }
    void validate(Site at) const {
        if (!handle)
            error(at, "R4020", "database is closed");
        if (owner != std::this_thread::get_id())
            error(at, "R4020", "database belongs to another thread");
    }
    void check(int result, Site at) const {
        if (result != 0)
            error(at, "R4020",
                  "SQLite error " + std::to_string(result) + ": " + api.errmsg(handle));
    }
    void control(const std::string& sql, Site at) {
        sqlite3_stmt* statement = nullptr;
        check(api.prepare_v2(handle, sql.c_str(), -1, &statement, nullptr), at);
        const int result = api.step(statement);
        api.finalize(statement);
        if (result != 101)
            check(result, at);
    }
};
struct StatementState {
    std::shared_ptr<DatabaseState> database;
    sqlite3_stmt* handle = nullptr;
    std::vector<bool> bound;
    enum class Phase { Fresh, Row, Done, Failed } phase = Phase::Fresh;
    explicit StatementState(std::shared_ptr<DatabaseState> db) : database(std::move(db)) {}
    void close() noexcept {
        if (handle)
            database->api.finalize(std::exchange(handle, nullptr));
    }
    ~StatementState() {
        close();
    }
    void validate(Site at) const {
        database->validate(at);
        if (!handle)
            error(at, "R4020", "statement is closed");
    }
    int parameter(Int index, Site at) const {
        validate(at);
        if (phase != Phase::Fresh)
            error(at, "R4020", "reset a statement before rebinding");
        if (index < 1 || static_cast<std::uint64_t>(index) > bound.size())
            error(at, "R4020", "parameter index is outside 1..parameter_count");
        return static_cast<int>(index);
    }
    int column(Int index, Site at, bool row) const {
        validate(at);
        if (index < 0 || index >= database->api.column_count(handle))
            error(at, "R4020", "column index is outside 0..column_count-1");
        if (row && phase != Phase::Row)
            error(at, "R4020", "column access requires a current row");
        return static_cast<int>(index);
    }
};
inline void DatabaseState::close() noexcept {
    if (!handle)
        return;
    for (auto& weak : statements)
        if (auto statement = weak.lock())
            statement->close();
    statements.clear();
    transactions.clear();
    api.close_v2(std::exchange(handle, nullptr)); // rolls back any open transaction
}
struct TransactionState {
    std::shared_ptr<DatabaseState> database;
    std::uint64_t id;
    bool active = true;
    std::string name() const {
        return "wood_tx_" + std::to_string(id);
    }
    bool present() const {
        return active && database->handle &&
               std::find(database->transactions.begin(), database->transactions.end(), id) !=
                   database->transactions.end();
    }
    void finish(bool commit, Site at) {
        if (!present()) {
            active = false;
            return;
        }
        database->validate(at);
        if (database->transactions.back() != id)
            error(at, "R4020", "transactions must finish in reverse scope order");
        if (!commit)
            database->control("ROLLBACK TO " + name(), at);
        database->control("RELEASE " + name(), at);
        database->transactions.pop_back();
        active = false;
    }
    void abandon() noexcept {
        if (!present())
            return;
        try {
            database->control("ROLLBACK TO " + name(), Site{"<transaction cleanup>", 0, 0});
            database->control("RELEASE " + name(), Site{"<transaction cleanup>", 0, 0});
            const auto begin =
                std::find(database->transactions.begin(), database->transactions.end(), id);
            database->transactions.erase(begin, database->transactions.end());
            active = false;
        } catch (...) {
            database->close();
        } // never leave an unowned live transaction
    }
    ~TransactionState() {
        abandon();
    }
};
struct Progress {
    std::chrono::steady_clock::time_point until;
    static int interrupt(void* data) noexcept {
        return native_detail::cancelled() ||
               std::chrono::steady_clock::now() >= static_cast<Progress*>(data)->until;
    }
};
} // namespace sqlite_detail
struct Database {
    std::shared_ptr<sqlite_detail::DatabaseState> state;
    bool operator==(const Database&) const = default;
};
struct Statement {
    std::shared_ptr<sqlite_detail::StatementState> state;
    bool operator==(const Statement&) const = default;
};
struct Transaction {
    std::shared_ptr<sqlite_detail::TransactionState> state;
    bool operator==(const Transaction&) const = default;
};
inline bool db_closed(const Database& db, Site) {
    return !db.state || !db.state->handle;
}
inline std::string to_str(const Database& db) {
    return db.state && db.state->handle ? "<database:open>" : "<database:closed>";
}
inline std::string to_str(const Statement& stmt) {
    return stmt.state && stmt.state->handle ? "<statement:open>" : "<statement:closed>";
}
inline std::string to_str(const Transaction& tx) {
    return tx.state && tx.state->present() ? "<transaction:active>" : "<transaction:closed>";
}
inline sqlite_detail::DatabaseState& db_state(const Database& db, Site at) {
    if (!db.state)
        error(at, "R4020", "database is closed");
    db.state->validate(at);
    return *db.state;
}
inline sqlite_detail::StatementState& stmt_state(const Statement& stmt, Site at) {
    if (!stmt.state)
        error(at, "R4020", "statement is closed");
    stmt.state->validate(at);
    return *stmt.state;
}
inline Database db_open(const std::string& path, Int timeout, Site at) {
    if (path.empty() || path.size() > 32768 || path.find('\0') != std::string::npos ||
        timeout < 1 || timeout > 86400000)
        error(at, "R4020",
              "database requires a nonempty path and timeout in 1..86400000 milliseconds");
    sqlite_detail::utf8(path, at);
    auto& api = sqlite_detail::api(at);
    auto state = std::make_shared<sqlite_detail::DatabaseState>(api, timeout);
    state->check(api.open_v2(path.c_str(), &state->handle, 0x10006, nullptr),
                 at); // FULLMUTEX, READWRITE, CREATE; no URI interpretation
    state->check(api.extended_result_codes(state->handle, 1), at);
    state->check(api.busy_timeout(state->handle, static_cast<int>(timeout)), at);
    for (const auto [category, limit] :
         {std::pair{0, 16777216}, {1, 1048576}, {2, 256}, {3, 128}, {9, 1000}})
        api.limit(state->handle, category, limit);
    int enabled = 0;
    state->check(api.db_config(state->handle, 1010, 1, &enabled), at); // DEFENSIVE
    state->check(api.db_config(state->handle, 1017, 0, &enabled), at); // TRUSTED_SCHEMA
    state->check(api.db_config(state->handle, 1005, 0, &enabled), at); // ENABLE_LOAD_EXTENSION
    state->control("PRAGMA foreign_keys=ON", at);
    return {std::move(state)};
}
inline void db_close(const Database& db, Site at) {
    if (!db_closed(db, at)) {
        db.state->validate(at);
        db.state->close();
    }
}
inline Int db_changes(const Database& db, Site at) {
    auto& state = db_state(db, at);
    return state.api.changes64(state.handle);
}
inline Int db_last_id(const Database& db, Site at) {
    auto& state = db_state(db, at);
    return state.api.last_insert_rowid(state.handle);
}
inline std::string db_version(Site at) {
    return sqlite_detail::api(at).libversion();
}
inline Statement db_prepare(const Database& db, const std::string& sql, Site at) {
    auto& state = db_state(db, at);
    if (sql.empty() || sql.size() > 1048576 || sql.find('\0') != std::string::npos)
        error(at, "R4020", "SQL must contain one statement, no NUL, and at most 1048576 bytes");
    sqlite_detail::utf8(sql, at);
    auto statement = std::make_shared<sqlite_detail::StatementState>(db.state);
    const char* tail = nullptr;
    state.check(state.api.prepare_v2(state.handle, sql.c_str(), static_cast<int>(sql.size() + 1),
                                     &statement->handle, &tail),
                at);
    if (!statement->handle)
        error(at, "R4020", "SQL must contain one statement");
    while (tail && *tail) {
        sqlite_detail::sqlite3_stmt* extra = nullptr;
        const char* next = nullptr;
        const int result = state.api.prepare_v2(state.handle, tail, -1, &extra, &next);
        const bool another = extra != nullptr;
        if (extra)
            state.api.finalize(extra);
        state.check(result, at);
        if (another)
            error(at, "R4020", "multiple SQL statements are not permitted");
        if (next == tail)
            break;
        tail = next;
    }
    statement->bound.resize(
        static_cast<std::size_t>(state.api.bind_parameter_count(statement->handle)), false);
    // Remove expired entries to keep repeated short statements bounded.
    std::erase_if(state.statements, [](const auto& item) { return item.expired(); });
    state.statements.push_back(statement);
    return {std::move(statement)};
}
inline void stmt_close(const Statement& stmt, Site) {
    if (stmt.state)
        stmt.state->close();
}
inline bool stmt_step(const Statement& stmt, Site at) {
    auto& state = stmt_state(stmt, at);
    if (state.phase == sqlite_detail::StatementState::Phase::Done)
        return false;
    if (state.phase == sqlite_detail::StatementState::Phase::Failed)
        error(at, "R4020", "reset a failed statement before stepping");
    if (std::find(state.bound.begin(), state.bound.end(), false) != state.bound.end())
        error(at, "R4020", "every SQL parameter must be explicitly bound");
    if (native_detail::cancelled())
        error(at, "R4020", "database operation cancelled");
    auto& db = *state.database;
    sqlite_detail::Progress progress{std::chrono::steady_clock::now() +
                                     std::chrono::milliseconds(db.timeout_ms)};
    db.api.progress_handler(db.handle, 1000, &sqlite_detail::Progress::interrupt, &progress);
    const int result = db.api.step(state.handle);
    db.api.progress_handler(db.handle, 0, nullptr, nullptr);
    state.phase = result == 100   ? sqlite_detail::StatementState::Phase::Row
                  : result == 101 ? sqlite_detail::StatementState::Phase::Done
                                  : sqlite_detail::StatementState::Phase::Failed;
    if (result != 100 && result != 101)
        db.check(result, at);
    return result == 100;
}
inline void stmt_reset(const Statement& stmt, Site at) {
    auto& state = stmt_state(stmt, at);
    (void)state.database->api.reset(
        state.handle); // returns previous step error; reset still succeeds
    state.database->check(state.database->api.clear_bindings(state.handle), at);
    std::fill(state.bound.begin(), state.bound.end(), false);
    state.phase = sqlite_detail::StatementState::Phase::Fresh;
}
inline void stmt_bind_int(const Statement& stmt, Int index, Int value, Site at) {
    auto& state = stmt_state(stmt, at);
    const int i = state.parameter(index, at);
    state.database->check(state.database->api.bind_int64(state.handle, i, value), at);
    state.bound[static_cast<std::size_t>(i - 1)] = true;
}
inline void stmt_bind_float(const Statement& stmt, Int index, double value, Site at) {
    if (!std::isfinite(value))
        error(at, "R4020", "SQLite float binding must be finite");
    auto& state = stmt_state(stmt, at);
    const int i = state.parameter(index, at);
    state.database->check(state.database->api.bind_double(state.handle, i, value), at);
    state.bound[static_cast<std::size_t>(i - 1)] = true;
}
inline void stmt_bind_bytes(const Statement& stmt, Int index, const std::string& value, bool text,
                            Site at) {
    if (value.size() > 16777216)
        error(at, "R4020", "SQLite value exceeds 16777216 bytes");
    if (text)
        sqlite_detail::utf8(value, at);
    auto& state = stmt_state(stmt, at);
    const int i = state.parameter(index, at);
    const auto copy = reinterpret_cast<sqlite_detail::Destructor>(
        static_cast<std::intptr_t>(-1)); // documented SQLITE_TRANSIENT
    const int result = text ? state.database->api.bind_text(state.handle, i, value.data(),
                                                            static_cast<int>(value.size()), copy)
                            : state.database->api.bind_blob(state.handle, i, value.data(),
                                                            static_cast<int>(value.size()), copy);
    state.database->check(result, at);
    state.bound[static_cast<std::size_t>(i - 1)] = true;
}
inline void stmt_bind_text(const Statement& stmt, Int index, const std::string& value, Site at) {
    stmt_bind_bytes(stmt, index, value, true, at);
}
inline void stmt_bind_blob(const Statement& stmt, Int index, const std::string& value, Site at) {
    stmt_bind_bytes(stmt, index, value, false, at);
}
inline void stmt_bind_null(const Statement& stmt, Int index, Site at) {
    auto& state = stmt_state(stmt, at);
    const int i = state.parameter(index, at);
    state.database->check(state.database->api.bind_null(state.handle, i), at);
    state.bound[static_cast<std::size_t>(i - 1)] = true;
}
inline Int stmt_columns(const Statement& stmt, Site at) {
    auto& state = stmt_state(stmt, at);
    return state.database->api.column_count(state.handle);
}
inline std::string stmt_name(const Statement& stmt, Int index, Site at) {
    auto& state = stmt_state(stmt, at);
    const int i = state.column(index, at, false);
    const auto* name = state.database->api.column_name(state.handle, i);
    if (!name)
        error(at, "R4020", "cannot read SQLite column name");
    return name;
}
inline std::string stmt_kind(const Statement& stmt, Int index, Site at) {
    auto& state = stmt_state(stmt, at);
    const int i = state.column(index, at, true);
    switch (state.database->api.column_type(state.handle, i)) {
    case 1:
        return "int";
    case 2:
        return "float";
    case 3:
        return "str";
    case 4:
        return "blob";
    default:
        return "null";
    }
}
inline Int stmt_int(const Statement& stmt, Int index, Site at) {
    if (stmt_kind(stmt, index, at) != "int")
        error(at, "R4020", "SQLite column is not an integer");
    return stmt.state->database->api.column_int64(stmt.state->handle, static_cast<int>(index));
}
inline double stmt_float(const Statement& stmt, Int index, Site at) {
    const auto kind = stmt_kind(stmt, index, at);
    if (kind != "int" && kind != "float")
        error(at, "R4020", "SQLite column is not numeric");
    const auto value =
        stmt.state->database->api.column_double(stmt.state->handle, static_cast<int>(index));
    if (!std::isfinite(value))
        error(at, "R4020", "SQLite column is not finite");
    return value;
}
inline std::string stmt_bytes(const Statement& stmt, Int index, bool text, Site at) {
    if (stmt_kind(stmt, index, at) != (text ? "str" : "blob"))
        error(at, "R4020", "SQLite column has a different kind");
    auto& api = stmt.state->database->api;
    const int i = static_cast<int>(index);
    const void* data = text ? static_cast<const void*>(api.column_text(stmt.state->handle, i))
                            : api.column_blob(stmt.state->handle, i);
    const int length = api.column_bytes(stmt.state->handle, i);
    if (!data && (text || length || api.errcode(stmt.state->database->handle) == 7))
        error(at, "R4020", "cannot read SQLite column bytes");
    std::string result =
        length ? std::string(static_cast<const char*>(data), static_cast<std::size_t>(length))
               : std::string{};
    if (text)
        sqlite_detail::utf8(result, at);
    return result;
}
inline std::string stmt_text(const Statement& stmt, Int index, Site at) {
    return stmt_bytes(stmt, index, true, at);
}
inline std::string stmt_blob(const Statement& stmt, Int index, Site at) {
    return stmt_bytes(stmt, index, false, at);
}
inline Transaction db_begin(const Database& db, Site at) {
    auto& state = db_state(db, at);
    if (state.transactions.size() >= 64)
        error(at, "R4020", "transaction nesting exceeds 64");
    if (state.next_transaction == std::numeric_limits<std::uint64_t>::max())
        error(at, "R4020", "transaction identity exhausted");
    auto tx = std::make_shared<sqlite_detail::TransactionState>();
    tx->database = db.state;
    tx->id = ++state.next_transaction;
    state.transactions.push_back(tx->id);
    try {
        state.control("SAVEPOINT " + tx->name(), at);
    } catch (...) {
        state.transactions.pop_back();
        tx->active = false;
        throw;
    }
    return {std::move(tx)};
}
inline void db_commit(const Transaction& tx, Site at) {
    if (!tx.state || !tx.state->present())
        error(at, "R4020", "transaction is closed");
    tx.state->finish(true, at);
}
inline void db_rollback(const Transaction& tx, Site at) {
    if (tx.state)
        tx.state->finish(false, at);
}
} // namespace wd
#endif
