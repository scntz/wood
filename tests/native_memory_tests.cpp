#include "native_memory.hpp"
#include <iostream>
int main() {
    int checks = 0, failures = 0;
    const auto check = [&](bool value) {
        ++checks;
        if (!value)
            ++failures;
    };
    void* (*volatile allocate)(std::size_t) = &::operator new;
    bench_reset();
    void* a = allocate(123);
    void* b = ::operator new(256, std::align_val_t{64});
    check(reinterpret_cast<std::uintptr_t>(b) % 64 == 0);
    check(native_memory::live.load() == 379);
    check(bench_peak() == 379 && bench_bytes() == 379);
    ::operator delete(a);
    check(native_memory::live.load() == 256);
    check(bench_stop() == 2);
    ::operator delete(b, std::align_val_t{64});
    check(native_memory::live.load() == 0);
    bench_reset();
    a = allocate(0);
    check(a != nullptr && bench_peak() == 0 && bench_bytes() == 0);
    ::operator delete(a);
    check(bench_stop() == 1 && native_memory::live.load() == 0);
    check(::operator new(std::numeric_limits<std::size_t>::max(), std::nothrow) == nullptr);
    check(bench_rss() > 0);
    std::cout << checks << " allocation instrumentation checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
