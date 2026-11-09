// Single-TU instrumentation. Counts global-new request bytes, including STL
// alignment padding but excluding this instrumentation's headers and C allocator
// bookkeeping. Process peak RSS/working set is separate and includes input setup.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// clang-format off
#include <windows.h>
#include <psapi.h>
// clang-format on
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#else
#include <sys/resource.h>
#endif
#ifndef WOOD_BENCH_MEMORY_COUNTERS
#define WOOD_BENCH_MEMORY_COUNTERS 1
#endif
#if WOOD_BENCH_MEMORY_COUNTERS
namespace native_memory {
inline std::atomic<bool> enabled{false};
inline std::atomic<std::uint64_t> count{0}, bytes{0}, live{0}, peak{0};
struct alignas(std::max_align_t) Header {
    void* raw;
    std::size_t size;
    bool tracked;
};
inline void* allocate(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) {
    if (alignment < alignof(Header))
        alignment = alignof(Header);
    const auto overhead = sizeof(Header) + alignment - 1;
    if (size > std::numeric_limits<std::size_t>::max() - overhead)
        throw std::bad_alloc();
    void* raw = std::malloc((size ? size : 1) + overhead);
    if (!raw)
        throw std::bad_alloc();
    const auto address =
        (reinterpret_cast<std::uintptr_t>(raw) + sizeof(Header) + alignment - 1) & ~(alignment - 1);
    auto* header = reinterpret_cast<Header*>(address) - 1;
    ::new (header) Header{raw, size, enabled.load(std::memory_order_relaxed)};
    if (header->tracked) {
        count.fetch_add(1, std::memory_order_relaxed);
        bytes.fetch_add(size, std::memory_order_relaxed);
        const auto current = live.fetch_add(size, std::memory_order_relaxed) + size;
        auto previous = peak.load(std::memory_order_relaxed);
        while (previous < current &&
               !peak.compare_exchange_weak(previous, current, std::memory_order_relaxed)) {
        }
    }
    return reinterpret_cast<void*>(address);
}
inline void release(void* value) noexcept {
    if (!value)
        return;
    const auto* header = static_cast<Header*>(value) - 1;
    if (header->tracked)
        live.fetch_sub(header->size, std::memory_order_relaxed);
    std::free(header->raw);
}
} // namespace native_memory
void* operator new(std::size_t n) {
    return native_memory::allocate(n);
}
void* operator new[](std::size_t n) {
    return native_memory::allocate(n);
}
void operator delete(void* p) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p) noexcept {
    native_memory::release(p);
}
void operator delete(void* p, std::size_t) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    native_memory::release(p);
}
void* operator new(std::size_t n, std::align_val_t a) {
    return native_memory::allocate(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void* p, std::align_val_t) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
    native_memory::release(p);
}
void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    native_memory::release(p);
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(n);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept {
    return ::operator new(n, t);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
    native_memory::release(p);
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(n, a);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t& t) noexcept {
    return ::operator new(n, a, t);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    native_memory::release(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    native_memory::release(p);
}
extern "C" void bench_reset() {
    native_memory::count = 0;
    native_memory::bytes = 0;
    native_memory::live = 0;
    native_memory::peak = 0;
    native_memory::enabled = true;
}
extern "C" std::int64_t bench_stop() {
    native_memory::enabled = false;
    return static_cast<std::int64_t>(native_memory::count.load());
}
extern "C" std::int64_t bench_bytes() {
    return static_cast<std::int64_t>(native_memory::bytes.load());
}
extern "C" std::int64_t bench_peak() {
    return static_cast<std::int64_t>(native_memory::peak.load());
}
#else
// Timing-only control: use the ordinary allocator, with no interposition or
// counter overhead. -1 means unavailable, never an allocation measurement.
extern "C" void bench_reset() {}
extern "C" std::int64_t bench_stop() {
    return -1;
}
extern "C" std::int64_t bench_bytes() {
    return -1;
}
extern "C" std::int64_t bench_peak() {
    return -1;
}
#endif
extern "C" std::int64_t bench_cpu_mask() {
#ifdef _WIN32
    DWORD_PTR process_mask = 0, system_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask))
        return -1;
    return static_cast<std::int64_t>(process_mask);
#else
    return -1; // This measurement is currently Windows-specific.
#endif
}
extern "C" std::int64_t bench_rss() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return -1;
    return static_cast<std::int64_t>(counters.PeakWorkingSetSize);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return -1;
#ifdef __APPLE__
    return usage.ru_maxrss;
#else
    return usage.ru_maxrss * 1024;
#endif
#endif
}
