#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
static std::int64_t run(std::int64_t n, std::int64_t distinct) {
    std::unordered_map<std::string, std::int64_t> counts;
    for (std::int64_t i = 0; i < n; ++i) {
        const std::string key = "key" + std::to_string(i % distinct);
        ++counts[key];
    }
    std::int64_t total = 0;
    for (const auto& entry : counts) total += entry.second;
    return total + static_cast<std::int64_t>(counts.size());
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]), std::atoll(argv[2]))));
    return 0;
}
