#include "wood_runtime.hpp"
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const wd::Site site{__FILE__, 1, 1};
    const auto requests = wd::to_int(std::string(argv[1]), site);
    wd::assert_that(requests >= 2 && requests <= 10000000, site);
    wd::Dict<std::string, wd::Int> counts;
    for (wd::Int i = 0; i < requests; i = wd::add(i, 1, site)) {
        const auto key = "key" + wd::to_str(wd::mod(i, 257, site));
        counts.set(key, wd::add(wd::get(counts, key, wd::Int{0}, site), 1, site), site);
    }
    wd::Int total = 0;
    for (const auto& [key, value] : counts.entries()) {
        (void)key;
        total = wd::add(total, value, site);
    }
    wd::print(wd::len(counts, site), total);
}
