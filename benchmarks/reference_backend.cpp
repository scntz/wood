// C++ baseline using the identical checked Wood SQLite primitives/library.
// This measures generated-loop/policy overhead, not parity with arbitrary C++.
#include "wood_runtime.hpp"
int main() {
    constexpr wd::Site at{"reference_backend.cpp", 1, 1};
    const auto started = std::chrono::steady_clock::now();
    {
        wd::ResourceScope db(wd::db_open(":memory:", 5000, at), at);
        {
            wd::ResourceScope create(
                wd::db_prepare(db.value, "CREATE TABLE numbers(value INTEGER NOT NULL)", at), at);
            if (wd::stmt_step(create.value, at))
                return 1;
        }
        {
            wd::ResourceScope scope(wd::db_begin(db.value, at), at);
            {
                wd::ResourceScope insert(
                    wd::db_prepare(db.value, "INSERT INTO numbers(value) VALUES(?)", at), at);
                for (wd::Int value = 1; value <= 100000; ++value) {
                    wd::stmt_bind_int(insert.value, 1, value, at);
                    if (wd::stmt_step(insert.value, at))
                        return 1;
                    wd::stmt_reset(insert.value, at);
                }
            }
            wd::db_commit(scope.value, at);
        }
        wd::ResourceScope cursor(
            wd::db_prepare(db.value, "SELECT count(*), sum(value) FROM numbers", at), at);
        if (!wd::stmt_step(cursor.value, at) || wd::stmt_int(cursor.value, 0, at) != 100000 ||
            wd::stmt_int(cursor.value, 1, at) != 5000050000)
            return 1;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::cout << "{\"rows\":100000,\"checksum\":5000050000,\"elapsed_ns\":" << elapsed << "}\n";
}
