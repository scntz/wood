#include <cstdint>
#include <limits>

// Real separately compiled C ABI integration. Never allow signed C++ overflow.
extern "C" std::int64_t wood_saturating_add(std::int64_t left, std::int64_t right) {
    constexpr auto low = std::numeric_limits<std::int64_t>::min();
    constexpr auto high = std::numeric_limits<std::int64_t>::max();
    if (right > 0 && left > high - right)
        return high;
    if (right < 0 && left < low - right)
        return low;
    return left + right;
}
extern "C" double wood_native_lerp(double left, double right, double weight) {
    return left * (1.0 - weight) + right * weight;
}
extern "C" void wood_native_accept(std::int64_t, double) {}
extern "C" std::int64_t wood_bytes_sum(const char* bytes, std::int64_t length) {
    std::int64_t result = 0;
    for (std::int64_t i = 0; i < length; ++i)
        result += static_cast<unsigned char>(bytes[i]);
    return result;
}
extern "C" std::int64_t wood_native_fold(std::int64_t count,
                                         std::int64_t (*step)(std::int64_t, std::int64_t)) {
    std::int64_t result = 0;
    for (std::int64_t i = 0; i < count; ++i)
        result = step(result, i);
    return result;
}
extern "C" double wood_native_map(double value, double (*function)(double)) {
    return function(value);
}
extern "C" void wood_native_visit(std::int64_t value, void (*visit)(std::int64_t)) {
    visit(value);
}
extern "C" std::int64_t wood_native_pair(std::int64_t (*first)(std::int64_t),
                                         std::int64_t (*second)(std::int64_t)) {
    const auto value = second(3);
    return first(2) + value;
}
extern "C" double wood_array_sum(const double* values, std::int64_t count) {
    double result = 0;
    for (std::int64_t i = 0; i < count; ++i)
        result += values[i];
    return result;
}
extern "C" void wood_array_scale(double* values, std::int64_t count, double factor) {
    for (std::int64_t i = 0; i < count; ++i)
        values[i] *= factor;
}
extern "C" void wood_array_copy(double* output, std::int64_t count, const double* input,
                                std::int64_t input_count) {
    if (count != input_count)
        return;
    for (std::int64_t i = 0; i < count; ++i)
        output[i] = input[i];
}
