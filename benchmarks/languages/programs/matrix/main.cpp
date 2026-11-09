#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
static std::vector<double> fill(std::int64_t n, double seed) {
    std::vector<double> values(static_cast<std::size_t>(n * n), 0.0);
    for (std::int64_t i = 0; i < n; ++i)
        for (std::int64_t j = 0; j < n; ++j)
            values[static_cast<std::size_t>(i * n + j)] = static_cast<double>((i * 7 + j * 3) % 11) * seed;
    return values;
}
static std::int64_t run(std::int64_t n) {
    const std::vector<double> left = fill(n, 0.5);
    const std::vector<double> right = fill(n, 0.25);
    std::vector<double> product(static_cast<std::size_t>(n * n), 0.0);
    for (std::int64_t i = 0; i < n; ++i) {
        for (std::int64_t k = 0; k < n; ++k) {
            const double factor = left[static_cast<std::size_t>(i * n + k)];
            for (std::int64_t j = 0; j < n; ++j)
                product[static_cast<std::size_t>(i * n + j)] = product[static_cast<std::size_t>(i * n + j)] + factor * right[static_cast<std::size_t>(k * n + j)];
        }
    }
    double trace = 0.0;
    for (std::int64_t i = 0; i < n; ++i) trace = trace + product[static_cast<std::size_t>(i * n + i)];
    return static_cast<std::int64_t>(trace);
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]))));
    return 0;
}
