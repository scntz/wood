#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
static std::int64_t run(std::int64_t limit) {
    std::vector<char> composite(static_cast<std::size_t>(limit + 1), 0);
    std::int64_t count = 0;
    for (std::int64_t prime = 2; prime <= limit; ++prime) {
        if (!composite[static_cast<std::size_t>(prime)]) {
            ++count;
            for (std::int64_t multiple = prime * prime; multiple <= limit; multiple += prime)
                composite[static_cast<std::size_t>(multiple)] = 1;
        }
    }
    return count;
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]))));
    return 0;
}
