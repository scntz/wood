// Single-translation-unit benchmark instrumentation, not a Wood dependency.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
namespace allocation_counter {
inline std::atomic<std::int64_t> count{0};
inline std::atomic<bool> enabled{false};
inline void record() noexcept {
    if (enabled.load(std::memory_order_relaxed))
        count.fetch_add(1, std::memory_order_relaxed);
}
inline void* allocate(std::size_t size) {
    if (void* value = std::malloc(size ? size : 1)) {
        record();
        return value;
    }
    throw std::bad_alloc();
}
inline void* aligned_allocate(std::size_t size, std::size_t alignment) {
    void* value = nullptr;
#ifdef _WIN32
    value = _aligned_malloc(size ? size : 1, alignment);
#else
    if (posix_memalign(&value, alignment, size ? size : 1) != 0)
        value = nullptr;
#endif
    if (!value)
        throw std::bad_alloc();
    record();
    return value;
}
inline void aligned_free(void* value) noexcept {
#ifdef _WIN32
    _aligned_free(value);
#else
    std::free(value);
#endif
}
} // namespace allocation_counter
void* operator new(std::size_t size) {
    return allocation_counter::allocate(size);
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* value) noexcept {
    std::free(value);
}
void operator delete[](void* value) noexcept {
    std::free(value);
}
void operator delete(void* value, std::size_t) noexcept {
    std::free(value);
}
void operator delete[](void* value, std::size_t) noexcept {
    std::free(value);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocation_counter::aligned_allocate(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* value, std::align_val_t) noexcept {
    allocation_counter::aligned_free(value);
}
void operator delete[](void* value, std::align_val_t) noexcept {
    allocation_counter::aligned_free(value);
}
void operator delete(void* value, std::size_t, std::align_val_t) noexcept {
    allocation_counter::aligned_free(value);
}
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept {
    allocation_counter::aligned_free(value);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}
void operator delete(void* value, const std::nothrow_t&) noexcept {
    std::free(value);
}
void operator delete[](void* value, const std::nothrow_t&) noexcept {
    std::free(value);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t& tag) noexcept {
    return ::operator new(size, alignment, tag);
}
void operator delete(void* value, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_counter::aligned_free(value);
}
void operator delete[](void* value, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_counter::aligned_free(value);
}
extern "C" void bench_alloc_reset() {
    allocation_counter::count.store(0, std::memory_order_relaxed);
    allocation_counter::enabled.store(true, std::memory_order_relaxed);
}
extern "C" std::int64_t bench_alloc_stop() {
    allocation_counter::enabled.store(false, std::memory_order_relaxed);
    return allocation_counter::count.load(std::memory_order_relaxed);
}
