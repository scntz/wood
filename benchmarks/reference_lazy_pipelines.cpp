// Straight optimized C++ baselines. Checked mode shares only scalar checks/timer.
#include "wood_runtime.hpp"
namespace {
constexpr wd::Site site{"reference_lazy_pipelines.cpp", 1, 1};
#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
// noinline prevents the harness from constant-folding an entire workload.
template <bool Checked> BENCH_NOINLINE wd::Int pipeline(wd::Int count, wd::Int rounds) {
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round) {
        const auto scale = round % 7 + 1;
        wd::Int accepted = 0, total = 0;
        for (wd::Int item = 0; item < count && accepted < count / 4; ++item) {
            if (item % 3 != 0)
                continue;
            if constexpr (Checked)
                total = wd::add(total, wd::add(wd::mul(item, scale, site), 1, site), site);
            else
                total += item * scale + 1;
            ++accepted;
        }
        if constexpr (Checked)
            checksum = wd::add(checksum, total, site);
        else
            checksum += total;
    }
    return checksum;
}
template <bool Checked> BENCH_NOINLINE wd::Int capture_calls(wd::Int count, wd::Int rounds) {
    std::string text;
    for (int i = 0; i < 128; ++i)
        text += "native wood";
    const auto callback = [text](wd::Int value) {
        if constexpr (Checked)
            return wd::add(wd::len(text, site), value, site);
        else
            return static_cast<wd::Int>(text.size()) + value;
    };
    wd::Int checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        for (wd::Int item = 0; item < count; ++item) {
            if constexpr (Checked)
                checksum = wd::add(checksum, callback(item % 256), site);
            else
                checksum += callback(item % 256);
        }
    return checksum;
}
int run() {
    const auto arguments = wd::args();
    const auto workload = arguments.get(0, site);
    const auto count = wd::to_int(arguments.get(1, site), site);
    const auto rounds = wd::to_int(arguments.get(2, site), site);
    const bool checked = arguments.get(3, site) == "checked";
    if (count < 4 || count > 10000000 || rounds < 1 || rounds > 100)
        return 2;
    const auto started = wd::time_monotonic_ns(site);
    const auto result =
        workload == "pipeline"
            ? (checked ? pipeline<true>(count, rounds) : pipeline<false>(count, rounds))
            : (checked ? capture_calls<true>(count, rounds) : capture_calls<false>(count, rounds));
    const auto elapsed = wd::time_monotonic_ns(site) - started;
    wd::Dict<std::string, wd::Int> report;
    report.set("checksum", result, site);
    report.set("elapsed_ns", elapsed, site);
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
