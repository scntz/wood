// Optimized straight loops with checked payload arithmetic. Bounded loop counters
// cannot overflow after the same input validation used by the Wood program.
#include "allocation_counter.hpp"
#include "wood_runtime.hpp"
namespace {
constexpr wd::Site site{"reference_compiler_intelligence.cpp", 1, 1};
#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
BENCH_NOINLINE wd::Int pipeline(wd::Int count, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        const auto scale = round % 7 + 1;
        wd::Int total = 0, accepted = 0;
        for (wd::Int item = 0; item < count && accepted < count / 4; ++item) {
            if (item % 3 != 0)
                continue;
            total = wd::add(total, wd::add(wd::mul(item, scale, site), 1, site), site);
            ++accepted;
        }
        checksum = wd::add(checksum, total, site);
    }
    return checksum;
}
BENCH_NOINLINE wd::Int capture(wd::Int count, wd::Int rounds) {
    const auto text = wd::repeat("native wood", 128, site);
    const auto callback = [text](wd::Int value) {
        return wd::add(wd::len(text, site), value, site);
    };
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (wd::Int item = 0; item < count; ++item)
            checksum = wd::add(checksum, callback(item % 256), site);
    return checksum;
}
BENCH_NOINLINE wd::Int payload(const std::vector<std::string>& source, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        wd::Int total = 0;
        for (const auto& value : source)
            total = wd::add(total, wd::len(value, site), site);
        checksum = wd::add(checksum, total, site);
    }
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
int run() {
    const auto arguments = wd::args();
    const auto mode = arguments.get(0, site);
    const auto count = wd::to_int(arguments.get(1, site), site);
    const auto rounds = wd::to_int(arguments.get(2, site), site);
    if (count < 4 || count > 10000000 || rounds < 1 || rounds > 100)
        return 2;
    std::vector<std::string> source;
    if (mode == "payload") {
        std::string value;
        for (int i = 0; i < 8; ++i)
            value += "native payload ";
        source.assign(static_cast<std::size_t>(count), value);
    }
    bench_alloc_reset();
    const auto started = wd::time_monotonic_ns(site);
    wd::Int checksum;
    if (mode == "pipeline")
        checksum = pipeline(count, rounds);
    else if (mode == "capture")
        checksum = capture(count, rounds);
    else if (mode == "payload")
        checksum = payload(source, rounds);
    else if (mode == "ranges")
        checksum = ranges(count, rounds);
    else
        return 2;
    const auto elapsed = wd::time_monotonic_ns(site) - started;
    const auto allocations = bench_alloc_stop();
    wd::Dict<std::string, wd::Int> report;
    report.set("checksum", checksum, site);
    report.set("elapsed_ns", elapsed, site);
    report.set("allocations", allocations, site);
    wd::print(wd::json_dump(report, site));
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
