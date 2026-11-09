#include <cstdint>

// Real synchronous C ABI implementations for the numerical declarations in
// the manuals. The test caller supplies finite, small arrays and scalar values.
extern "C" double library_sum(const double* values, std::int64_t count) {
    double result = 0;
    for (std::int64_t i = 0; i < count; ++i)
        result += values[i];
    return result;
}
extern "C" void library_scale(double* values, std::int64_t count, double factor) {
    for (std::int64_t i = 0; i < count; ++i)
        values[i] *= factor;
}
