#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
static std::int64_t run(std::int64_t n, std::int64_t rounds) {
    std::int64_t total = 0;
    for (std::int64_t round = 0; round < rounds; ++round) {
        std::vector<std::string> parts;
        for (std::int64_t i = 0; i < n; ++i) parts.push_back(std::to_string(i * 7 + round));
        std::string text;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i != 0) text += ',';
            text += parts[i];
        }
        std::vector<std::string_view> fields;
        std::string_view rest(text);
        for (;;) {
            const std::size_t comma = rest.find(',');
            fields.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos) break;
            rest.remove_prefix(comma + 1);
        }
        for (const std::string_view field : fields) {
            std::int64_t value = 0;
            std::from_chars(field.data(), field.data() + field.size(), value);
            total += value;
        }
        total += static_cast<std::int64_t>(text.size());
    }
    return total;
}
int main(int, char** argv) {
    std::printf("%lld\n", static_cast<long long>(run(std::atoll(argv[1]), std::atoll(argv[2]))));
    return 0;
}
