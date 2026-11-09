#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>
static std::int64_t run(std::int64_t n) {
    std::vector<std::int64_t> values;
    std::int64_t x = 12345;
    for (std::int64_t i = 0; i < n; ++i) {
        x = (x * 1664525 + 1013904223) % 4294967296LL;
        values.push_back(x);
    }
    std::vector<std::int64_t> stack{0, n - 1};
    while (!stack.empty()) {
        const std::int64_t high = stack.back();
        stack.pop_back();
        const std::int64_t low = stack.back();
        stack.pop_back();
        if (low < high) {
            const std::int64_t pivot = values[static_cast<std::size_t>((low + high) / 2)];
            std::int64_t i = low, j = high;
            while (i <= j) {
                while (values[static_cast<std::size_t>(i)] < pivot) ++i;
                while (values[static_cast<std::size_t>(j)] > pivot) --j;
                if (i <= j) {
                    std::swap(values[static_cast<std::size_t>(i)], values[static_cast<std::size_t>(j)]);
                    ++i;
                    --j;
                }
            }
            stack.push_back(low);
            stack.push_back(j);
            stack.push_back(i);
            stack.push_back(high);
        }
    }
    std::int64_t checksum = 0;
    for (std::int64_t i = 0; i < n; ++i)
        checksum = (checksum + values[static_cast<std::size_t>(i)] * (i % 7 + 1)) % 1000000007;
    return checksum;
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]))));
    return 0;
}
