// Straight, optimized C++ with checked Wood-compatible payload arithmetic.
// Immutable inputs are borrowed within scope; output buffers are owned.
#include "native_memory.hpp"
#include "wood_runtime.hpp"
namespace {
constexpr wd::Site site{"reference_performance.cpp", 1, 1};
#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
BENCH_NOINLINE wd::Int text(const std::vector<std::string>& source, wd::Int rounds,
                            const std::string& mode) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        wd::Int index = 0;
        for (const auto& row : source) {
            wd::Int value;
            if (mode == "scan") {
                if (row.empty() || wd::slice(row, 0, 5, site) != "mesh:" ||
                    !wd::contains(row, "payload-", site))
                    wd::raise("invalid input row", site);
                value = wd::add(wd::add(wd::len(row, site),
                                        wd::len(wd::slice(row, -9, wd::max_int, site), site), site),
                                wd::to_int(wd::at(row, -1, site), site), site);
            } else {
                std::string formatted = "[";
                const auto append = [&](const std::string& value) {
                    if (formatted.size() > static_cast<std::uint64_t>(wd::max_int) - value.size() ||
                        formatted.size() > formatted.max_size() - value.size())
                        wd::error(site, "R4003", "string is too long");
                    formatted.append(value);
                };
                append(wd::to_str(index));
                append("] ");
                append(row);
                append(" :: ");
                append(wd::slice(row, 0, 5, site));
                wd::assert_that(wd::ends_with(formatted, "mesh:"), site);
                value = wd::len(formatted, site);
            }
            checksum = wd::add(checksum, value, site);
            ++index;
        }
    }
    return checksum;
}
BENCH_NOINLINE wd::Int pipeline(wd::Int count, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        wd::Int total = 0, accepted = 0;
        for (wd::Int item = 0; item < count && accepted < count / 4; ++item) {
            if (item % 3 != 0)
                continue;
            total = wd::add(total, wd::add(wd::mul(item, round % 7 + 1, site), 1, site), site);
            ++accepted;
        }
        checksum = wd::add(checksum, total, site);
    }
    return checksum;
}
BENCH_NOINLINE wd::Int capture(wd::Int count, wd::Int rounds) {
    const auto text = wd::repeat("native wood", 128, site);
    const auto callback = [text](wd::Int item) { return wd::add(wd::len(text, site), item, site); };
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (wd::Int item = 0; item < count; ++item)
            checksum = wd::add(checksum, callback(item % 256), site);
    return checksum;
}
BENCH_NOINLINE wd::Int ranges(wd::Int count, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (wd::Int start = 0; start < count; ++start)
            for (wd::Int item = start; item < start + 8; item += start % 7 + 1)
                checksum = wd::add(checksum, item % 17, site);
    return checksum;
}
BENCH_NOINLINE wd::Int lists(const std::vector<wd::Int>& source, wd::Int rounds) {
    auto values = source;
    for (wd::Int round = 0; round < rounds; ++round)
        for (std::size_t index = 0; index < values.size(); ++index)
            values.at(index) = wd::add(values.at(index), 1, site);
    wd::Int checksum = 0;
    for (const auto value : values)
        checksum = wd::add(checksum, value, site);
    return checksum;
}
BENCH_NOINLINE wd::Int dictionary(const std::map<wd::Int, wd::Int>& source, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (wd::Int index = 0; index < static_cast<wd::Int>(source.size()); ++index)
            if (source.contains(index))
                checksum = wd::add(checksum, source.at(index), site);
    return checksum;
}
BENCH_NOINLINE wd::Int validation(const std::vector<wd::Int>& source, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (const auto value : source) {
            wd::assert_that(value >= 0, site);
            checksum = wd::add(checksum, value, site);
        }
    return checksum;
}
BENCH_NOINLINE wd::Int payload(const std::vector<std::string>& source, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        wd::Int total = 0;
        for (const auto& row : source)
            total = wd::add(total, wd::len(row, site), site);
        checksum = wd::add(checksum, total, site);
    }
    return checksum;
}
BENCH_NOINLINE double arrays(const wd::Array<double>& left, const wd::Array<double>& right,
                             wd::Int rounds) {
    double checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        if (left.size() != right.size())
            wd::error(site, "R4026", "shape mismatch");
        std::vector<double> result(left.size());
        for (std::size_t index = 0; index < result.size(); ++index)
            result[index] = left.flat(index) * 1.5 + right.flat(index) * 0.25;
        wd::CompensatedSum total;
        for (const auto value : result)
            total.add(value);
        checksum += total.value();
    }
    return checksum;
}
int run() {
    const auto arguments = wd::args();
    const auto mode = arguments.get(0, site);
    const auto count = wd::to_int(arguments.get(1, site), site);
    const auto rounds = wd::to_int(arguments.get(2, site), site);
    if (count < 4 || count > 1000000 || rounds < 1 || rounds > 100)
        return 2;
    std::vector<std::string> source;
    std::vector<wd::Int> numbers;
    std::map<wd::Int, wd::Int> mapping;
    wd::Array<double> left, right;
    if (mode == "scan" || mode == "concat" || mode == "payload" ||
        mode == "payload_direct" || mode == "payload_eager") {
        const auto body = wd::repeat("payload-", 16, site);
        for (wd::Int index = 0; index < count; ++index)
            source.push_back("mesh:" + wd::to_str(index) + ":" + body + wd::to_str(index % 10));
    } else if (mode == "lists" || mode == "validation") {
        for (wd::Int index = 0; index < count; ++index)
            numbers.push_back(index % 97);
    } else if (mode == "dictionary") {
        for (wd::Int index = 0; index < count; ++index)
            mapping.emplace(index, index % 97);
    } else if (mode == "arrays") {
        left = wd::array_full(count, 0.0, site);
        right = wd::array_full(count, 0.0, site);
        for (wd::Int index = 0; index < count; ++index) {
            left.mut_at(index, site) = static_cast<double>(index % 1024) * 0.125;
            right.mut_at(index, site) = static_cast<double>(index % 17) * 0.25;
        }
    }
    bench_reset();
    const auto started = wd::time_monotonic_ns(site);
    double checksum;
    if (mode == "scan" || mode == "concat")
        checksum = static_cast<double>(text(source, rounds, mode));
    else if (mode == "payload" || mode == "payload_direct" || mode == "payload_eager")
        checksum = static_cast<double>(payload(source, rounds));
    else if (mode == "pipeline")
        checksum = static_cast<double>(pipeline(count, rounds));
    else if (mode == "capture")
        checksum = static_cast<double>(capture(count, rounds));
    else if (mode == "ranges")
        checksum = static_cast<double>(ranges(count, rounds));
    else if (mode == "lists")
        checksum = static_cast<double>(lists(numbers, rounds));
    else if (mode == "validation")
        checksum = static_cast<double>(validation(numbers, rounds));
    else if (mode == "dictionary")
        checksum = static_cast<double>(dictionary(mapping, rounds));
    else if (mode == "arrays")
        checksum = arrays(left, right, rounds);
    else
        return 2;
    const auto elapsed = wd::time_monotonic_ns(site) - started;
    const auto allocations = bench_stop();
    wd::Dict<std::string, wd::Json> result;
    result.set("checksum", wd::Json(checksum), site);
    result.set("elapsed_ns", wd::Json(elapsed), site);
    result.set("allocations", wd::Json(allocations), site);
    result.set("allocated_bytes", wd::Json(bench_bytes()), site);
    result.set("peak_bytes", wd::Json(bench_peak()), site);
    result.set("process_peak_bytes", wd::Json(bench_rss()), site);
    result.set("cpu_mask", wd::Json(bench_cpu_mask()), site);
    wd::print(wd::json_dump(result, site));
    return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    wd::init_args(argc, argv);
    return run();
}
