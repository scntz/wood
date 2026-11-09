#include "wood_runtime.hpp"

namespace {
constexpr wd::Site benchmark_site{"reference_callbacks.cpp", 1, 1};
wd::Int square_plus(wd::Int value) {
    return wd::add(wd::mul(value, value, benchmark_site), 1, benchmark_site);
}
wd::Int square_minus(wd::Int value) {
    return wd::sub(wd::mul(value, value, benchmark_site), 1, benchmark_site);
}
int run() {
    auto count = wd::to_int(wd::at(wd::args(), 0, benchmark_site), benchmark_site);
    wd::Function<wd::Int, wd::Int> callback = square_plus;
    if (count < 0) {
        callback = square_minus;
        count = wd::neg(count, benchmark_site);
    }
    wd::Int total = 0;
    for (wd::Int i = 0; i < count; ++i)
        total = wd::add(total, callback(wd::mod(i, 256, benchmark_site)), benchmark_site);
    wd::print(total);
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
