#include "wood_runtime.hpp"
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const wd::Site at{__FILE__, 1, 1};
    const wd::Int limit = wd::to_int(std::string(argv[1]), at);
    wd::assert_that(limit >= 2 && limit <= 10000000, at);
    wd::List<bool> composite;
    for (wd::Int i = 0; i <= limit; i = wd::add(i, 1, at))
        composite.push(false, at);
    wd::Int count = 0, prime = 2;
    while (prime <= limit) {
        if (!composite.get(prime, at)) {
            count = wd::add(count, 1, at);
            if (prime <= wd::floor_div(limit, prime, at)) {
                wd::Int multiple = wd::mul(prime, prime, at);
                while (multiple <= limit) {
                    composite.mut_at(multiple, at) = true;
                    multiple = wd::add(multiple, prime, at);
                }
            }
        }
        prime = wd::add(prime, 1, at);
    }
    wd::print(count);
}
