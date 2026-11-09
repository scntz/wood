// Optional native ABIs, task cancellation, and borrowed synchronous callbacks.
#ifndef WOOD_NATIVE_HPP
#define WOOD_NATIVE_HPP
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <type_traits>
#ifndef _WIN32
#include <dlfcn.h>
#endif
namespace wd {
namespace native_detail {
inline thread_local std::stop_token stop;
inline bool cancelled() noexcept {
    return stop.stop_requested();
}
inline std::filesystem::path library_directory(const char* variable, const char* code, Site at) {
#ifdef _WIN32
    const std::string name(variable);
    const std::wstring wide(name.begin(), name.end());
    std::wstring value(32768, L'\0');
    const auto length =
        GetEnvironmentVariableW(wide.c_str(), value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size())
        error(at, code, std::string(variable) + " exceeds Windows environment limit");
    value.resize(length);
    const auto path = std::filesystem::path(value);
#else
    const char* value = std::getenv(variable);
    const auto path = value ? std::filesystem::path(value) : std::filesystem::path{};
#endif
    if (!path.empty() && !path.is_absolute())
        error(at, code, std::string(variable) + " must be an absolute trusted library directory");
    return path;
}
class Library {
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
  public:
    Library(const Library&) = delete;
    explicit Library(const std::vector<std::string>& names, const char* code, Site at,
                     const std::filesystem::path& directory = {}) {
        for (const auto& name : names) {
#ifdef _WIN32
            const auto path = directory.empty() ? file_path(name, at) : directory / name;
            handle_ = LoadLibraryExW(path.c_str(), nullptr,
                                     directory.empty() ? LOAD_LIBRARY_SEARCH_SYSTEM32
                                                       : LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                                             LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
            const auto path = directory.empty() ? std::filesystem::path(name) : directory / name;
            handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
            if (handle_)
                return;
        }
        error(at, code, "required native library is unavailable: " + names.front());
    }
    ~Library() {
#ifdef _WIN32
        if (handle_)
            FreeLibrary(handle_);
#else
        if (handle_)
            dlclose(handle_);
#endif
    }
    template <class F> F symbol(const char* name, const char* code, Site at) const {
#ifdef _WIN32
        const auto pointer = GetProcAddress(handle_, name);
#else
        const auto pointer = dlsym(handle_, name);
#endif
        if (!pointer)
            error(at, code, std::string("native library is missing ABI symbol: ") + name);
        F result;
        static_assert(sizeof(result) == sizeof(pointer));
        std::memcpy(&result, &pointer, sizeof(result));
        return result;
    }
};
} // namespace native_detail

// A C library may borrow these trampolines only on the calling thread and only
// until its call returns. Exceptions are saved here, never unwound through C.
template <class F, std::size_t Tag> struct CallbackGuard;
struct CallbackFailure {
    std::exception_ptr failure;
    void check() const {
        if (failure)
            std::rethrow_exception(failure);
    }
};
template <std::size_t Tag, class R, class... A> struct CallbackGuard<R (*)(A...), Tag> {
    using Self = CallbackGuard;
    inline static thread_local Self* current = nullptr;
    Self* previous;
    R (*function)(A...);
    CallbackFailure& errors;
    CallbackGuard(R (*value)(A...), CallbackFailure& failures)
        : previous(current), function(value), errors(failures) {
        current = this;
    }
    CallbackGuard(const Self&) = delete;
    ~CallbackGuard() {
        current = previous;
    }
    static R invoke(A... args) noexcept {
        auto* owner = current;
        if (owner && !owner->errors.failure) {
            try {
                return owner->function(args...);
            } catch (...) {
                owner->errors.failure = std::current_exception();
            }
        }
        if constexpr (!std::is_void_v<R>)
            return R{};
    }
    void check() const {
        errors.check();
    }
};
} // namespace wd
#endif
