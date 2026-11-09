#include "wood_runtime.hpp"
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    if (argc != 3)
        return 2;
    const wd::Site site{__FILE__, 1, 1};
    wd::init_args(argc, argv);
    const auto arguments = wd::args();
    const auto text = wd::read_text(arguments.get(0, site), site);
    const auto iterations = wd::to_int(arguments.get(1, site), site);
    wd::assert_that(iterations >= 1 && iterations <= 1000000, site);
    wd::Int total = 0;
    for (wd::Int i = 0; i < iterations; i = wd::add(i, 1, site))
        total = wd::add(total, wd::len(text, site), site);
    wd::print(total);
}
