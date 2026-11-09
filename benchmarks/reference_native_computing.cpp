#include "native_memory.hpp"
#include "wood_runtime.hpp"
namespace {
constexpr wd::Site benchmark_site{"reference-native.cpp", 1, 1};
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE double blend(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size())
        throw std::runtime_error("shape mismatch");
    std::vector<double> result(a.size());
    const auto* left = a.data();
    const auto* right = b.data();
    auto* output = result.data();
    for (std::size_t i = 0; i < a.size(); ++i)
        output[i] = left[i] * 1.5 + right[i] * 0.25;
    wd::CompensatedSum sum;
    for (const auto value : result)
        sum.add(value);
    return sum.value();
}
NOINLINE double affine(const std::vector<double>& a, const std::vector<double>& matrix) {
    if (a.size() % 3 || matrix.size() != 16)
        throw std::runtime_error("shape mismatch");
    std::vector<double> result(a.size());
    const auto* input = a.data();
    const auto* coefficients = matrix.data();
    auto* output = result.data();
    for (std::size_t i = 0; i < a.size() / 3; ++i)
        for (std::size_t axis = 0; axis < 3; ++axis) {
            double value = 0;
            for (std::size_t k = 0; k < 3; ++k)
                value += input[i * 3 + k] * coefficients[axis * 4 + k];
            output[i * 3 + axis] = value + coefficients[axis * 4 + 3];
        }
    wd::CompensatedSum sum;
    for (const auto value : result)
        sum.add(value);
    return sum.value();
}
NOINLINE double weighted_mean(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size())
        throw std::runtime_error("shape mismatch");
    std::vector<double> result(a.size());
    const auto* left = a.data();
    const auto* right = b.data();
    auto* output = result.data();
    for (std::size_t i = 0; i < a.size(); ++i)
        output[i] = (left[i] * 1.5 + right[i] * 0.5) / 2.0;
    wd::CompensatedSum sum;
    for (const auto value : result)
        sum.add(value);
    return sum.value();
}
NOINLINE double matmul(const std::vector<double>& a, const std::vector<double>& b, std::size_t n) {
    if (n > 384 || a.size() != n * n || b.size() != n * n)
        throw std::runtime_error("shape mismatch");
    std::vector<double> result(n * n);
    const auto* left = a.data();
    const auto* right = b.data();
    auto* output = result.data();
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t k = 0; k < n; ++k) {
            const auto value = left[i * n + k];
            auto* row = output + i * n;
            const auto* source = right + k * n;
            for (std::size_t j = 0; j < n; ++j)
                row[j] += value * source[j];
        }
    wd::CompensatedSum sum;
    for (const auto value : result)
        sum.add(value);
    return sum.value();
}
int run() {
    auto arguments = wd::args();
    const auto mode = arguments.get(0, benchmark_site);
    const auto count = wd::to_int(arguments.get(1, benchmark_site), benchmark_site),
               rounds = wd::to_int(arguments.get(2, benchmark_site), benchmark_site);
    if (count < 4 || count > 1000000 || rounds < 1 || rounds > 100 ||
        (mode == "matmul" && count > 384))
        return 2;
    const auto n = static_cast<std::size_t>(count);
    std::vector<double> a(n), b(n);
    const std::vector<double> matrix{1.25,  0.25, 0.5, 1,  -0.5, 0.75, 0.25, 2,
                                     0.125, 0.5,  1.5, -3, 0,    0,    0,    1};
    if (mode == "blend" || mode == "owned")
        for (std::size_t i = 0; i < n; ++i) {
            a[i] = (i % 1024) * 0.125;
            b[i] = (i % 17) * 0.25;
        }
    else if (mode == "affine") {
        a.resize(n * 3);
        for (std::size_t i = 0; i < n; ++i) {
            a[i * 3] = (i % 31) * 0.125;
            a[i * 3 + 1] = (i % 17) * 0.25;
            a[i * 3 + 2] = (i % 7) * 0.5;
        }
    } else if (mode == "matmul") {
        a.resize(n * n);
        b.resize(n * n);
        for (std::size_t i = 0; i < n; ++i)
            for (std::size_t j = 0; j < n; ++j) {
                a[i * n + j] = ((i + j) % 13) * 0.125;
                b[i * n + j] = ((i * 3 + j) % 11) * 0.25;
            }
    } else
        return 2;
    bench_reset();
    const auto started = wd::time_monotonic_ns(benchmark_site);
    double checksum = 0;
    for (wd::Int round = 0; round < rounds; ++round)
        checksum += mode == "blend"    ? blend(a, b)
                    : mode == "owned"  ? weighted_mean(a, b)
                    : mode == "affine" ? affine(a, matrix)
                                       : matmul(a, b, n);
    const auto elapsed = wd::time_monotonic_ns(benchmark_site) - started;
    const auto allocations = bench_stop(), bytes = bench_bytes(), peak = bench_peak(),
               rss = bench_rss();
    wood::Json::Object report{{"checksum", checksum},       {"elapsed_ns", elapsed},
                              {"allocations", allocations}, {"allocated_bytes", bytes},
                              {"peak_bytes", peak},         {"process_peak_bytes", rss}};
    std::cout << wood::Json(std::move(report)).dump() << '\n';
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
