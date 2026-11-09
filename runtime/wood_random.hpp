#ifndef WOOD_RANDOM_HPP
#define WOOD_RANDOM_HPP
#if defined(__linux__)
#include <cerrno>
#include <sys/random.h>
#endif
namespace wd {
inline std::string random_bytes(Int size, Site at) {
    if (size < 0 || size > 1048576)
        error(at, "R4003", "random byte count must be in 0..1048576");
    if (size)
        task_checkpoint(at);
    std::string output(static_cast<std::size_t>(size), '\0');
    if (!size)
        return output;
#ifdef _WIN32
    struct Provider {
        HMODULE value;
        ~Provider() { if (value) FreeLibrary(value); }
    } provider{LoadLibraryExW(L"bcrypt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
    if (!provider.value)
        error(at, "R4008", "cannot load system random provider");
    using Generate = LONG (WINAPI *)(void*, unsigned char*, ULONG, ULONG);
    const auto generate = reinterpret_cast<Generate>(GetProcAddress(provider.value, "BCryptGenRandom"));
    if (!generate)
        error(at, "R4008", "cannot obtain operating-system randomness");
#elif !defined(__linux__)
    error(at, "R4008", "operating-system randomness is unsupported on this target");
#endif
    Int position = 0;
    while (position < size) {
        task_checkpoint(at);
        const auto count = static_cast<std::size_t>(std::min<Int>(256, size - position));
#ifdef _WIN32
        if (generate(nullptr, reinterpret_cast<unsigned char*>(output.data() + position),
                     static_cast<ULONG>(count), 2) != 0)
            error(at, "R4008", "cannot obtain operating-system randomness");
        position += static_cast<Int>(count);
#elif defined(__linux__)
        const auto n = ::getrandom(output.data() + position, count, GRND_NONBLOCK);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0 || static_cast<std::size_t>(n) > count)
            error(at, "R4008", "cannot obtain operating-system randomness");
        position += n;
#endif
    }
    return output;
}
} // namespace wd
#endif
