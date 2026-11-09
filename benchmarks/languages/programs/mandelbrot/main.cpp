#include <cstdint>
#include <cstdio>
#include <cstdlib>
static std::int64_t run(std::int64_t size, std::int64_t limit) {
    std::int64_t inside = 0;
    for (std::int64_t row = 0; row < size; ++row) {
        const double ci = 2.0 * static_cast<double>(row) / static_cast<double>(size) - 1.0;
        for (std::int64_t column = 0; column < size; ++column) {
            const double cr = 2.0 * static_cast<double>(column) / static_cast<double>(size) - 1.5;
            double zr = 0.0, zi = 0.0;
            std::int64_t step = 0;
            while (step < limit && zr * zr + zi * zi <= 4.0) {
                const double next_zr = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = next_zr;
                ++step;
            }
            if (step == limit) ++inside;
        }
    }
    return inside;
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]), std::atoll(argv[2]))));
    return 0;
}
