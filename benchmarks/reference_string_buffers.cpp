// Independent checked C++ algorithms: no wd::join/replace/CSV implementation.
#include "native_memory.hpp"
#include "wood_runtime.hpp"
#include <vector>
namespace {
constexpr wd::Site site{"reference_string_buffers.cpp", 1, 1};
void grow(std::size_t& length, std::size_t amount, std::size_t limit) {
    if (amount > limit - length)
        wd::error(site, "R4003", "string is too long");
    length += amount;
}
std::size_t limit() {
    return std::min(std::string{}.max_size(), static_cast<std::size_t>(wd::max_int));
}
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE std::string assemble(const std::vector<std::string>& values, const std::string& sep) {
    std::size_t length = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) grow(length, sep.size(), limit());
        grow(length, values[i].size(), limit());
    }
    std::string out;
    out.reserve(length);
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out.append(sep);
        out.append(values[i]);
    }
    return out;
}
NOINLINE std::string substitute(const std::string& text, const std::string& old,
                                const std::string& replacement) {
    if (old.empty()) wd::error(site, "R4009", "replacement pattern cannot be empty");
    std::size_t length = 0, start = 0;
    for (;;) {
        const auto end = text.find(old, start);
        if (end == std::string::npos) { grow(length, text.size() - start, limit()); break; }
        grow(length, end - start, limit());
        grow(length, replacement.size(), limit());
        start = end + old.size();
    }
    std::string out;
    out.reserve(length);
    start = 0;
    for (;;) {
        const auto end = text.find(old, start);
        if (end == std::string::npos) { out.append(text, start, std::string::npos); break; }
        out.append(text, start, end - start);
        out.append(replacement);
        start = end + old.size();
    }
    return out;
}
NOINLINE std::string csv(const std::vector<std::vector<std::string>>& rows) {
    std::size_t length = 0;
    const auto quote = [](const std::string& value, std::size_t fields) {
        return value.find_first_of(",\"\r\n") != std::string::npos || (fields == 1 && value.empty());
    };
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            const auto& field = row[i];
            if (i) grow(length, 1, limit());
            grow(length, field.size(), limit());
            if (quote(field, row.size())) {
                grow(length, 2, limit());
                for (char c : field) if (c == '"') grow(length, 1, limit());
            }
        }
        grow(length, 2, limit());
    }
    std::string out;
    out.reserve(length);
    for (const auto& row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) out.push_back(',');
            const auto& field = row[i];
            if (quote(field, row.size())) {
                out.push_back('"');
                for (char c : field) { out.push_back(c); if (c == '"') out.push_back(c); }
                out.push_back('"');
            } else out.append(field);
        }
        out.append("\r\n");
    }
    return out;
}
int run() {
    const auto args = wd::args();
    if (args.size() != 4) return 2;
    const auto mode = args.get(0, site);
    const auto count = wd::to_int(args.get(1, site), site);
    const auto rounds = wd::to_int(args.get(2, site), site);
    if (count < 1 || count > 20000 || rounds < 1 || rounds > 100) return 2;
    std::vector<std::string> fields;
    std::vector<std::vector<std::string>> rows;
    for (wd::Int i = 0; i < count; ++i) {
        fields.push_back("member-" + wd::to_str(i) + ":" + wd::repeat("payload-", 12, site));
        rows.push_back({wd::to_str(i), "mesh,\"" + fields.back() + "\"", "ready"});
    }
    if (mode == "csv_plain") {
        for (wd::Int i = 0; i < count; ++i)
            rows.at(i) = {wd::to_str(i), fields.at(i), "ready"};
    } else if (mode == "csv_dense") {
        for (wd::Int i = 0; i < count; ++i)
            rows.at(i) = {wd::to_str(i), wd::repeat("\"\r\n,", 32, site), "ready"};
    } else if (mode == "csv_wide") {
        for (wd::Int i = 0; i < count; ++i) {
            rows.at(i).clear();
            for (wd::Int j = 0; j < 16; ++j)
                rows.at(i).push_back(fields.at((i + j) % count));
        }
    } else if (mode == "csv_empty") {
        for (wd::Int i = 0; i < count; ++i)
            rows.at(i) = std::vector<std::string>(static_cast<std::size_t>(i % 3));
    }
    const auto text = assemble(fields, "|");
    std::string result;
    wd::Int checksum = 0;
    bench_reset();
    const auto started = wd::time_monotonic_ns(site);
    for (wd::Int round = 0; round < rounds; ++round) {
        if (mode == "join") result = assemble(fields, "::");
        else if (mode == "replace") result = substitute(text, "|", "::");
        else if (mode == "absent") result = substitute(text, "not-present", "::");
        else if (mode == "csv" || mode == "csv_plain" || mode == "csv_dense" ||
                 mode == "csv_wide" || mode == "csv_empty") result = csv(rows);
        else return 2;
        checksum = wd::add(checksum, wd::len(result, site), site);
    }
    const auto elapsed = wd::time_monotonic_ns(site) - started;
    const auto allocations = bench_stop();
    wd::write_text(args.get(3, site), result, site);
    wd::Dict<std::string, wd::Int> metrics;
    for (const auto& entry : std::initializer_list<std::pair<std::string, wd::Int>>{
             {"checksum", checksum}, {"elapsed_ns", elapsed}, {"allocations", allocations},
             {"allocated_bytes", bench_bytes()}, {"peak_bytes", bench_peak()},
             {"process_peak_bytes", bench_rss()}}) metrics.set(entry.first, entry.second, site);
    wd::print(wd::json_dump(metrics, site));
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    wd::init_args(argc, argv);
    return run();
}
