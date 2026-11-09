#ifndef WOOD_FILESYSTEM_HPP
#define WOOD_FILESYSTEM_HPP
#include <filesystem>
#include <deque>
#include <string>
#include <stdexcept>
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace wood::filesystem_support {
namespace fs = std::filesystem;
inline std::string text(const fs::path& path) {
    const auto bytes = path.generic_u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
inline fs::path resolve_path(const fs::path& input, bool allow_missing = false);
inline bool is_link(const fs::path& path) {
#ifdef _WIN32
    // Windows directory junctions are reparse points, but MSVC's is_symlink
    // deliberately returns false for them. Unknown reparse types fail closed.
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        throw fs::filesystem_error(
            "cannot query path attributes", path,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
inline bool contained(const fs::path& path, const fs::path& root) {
    auto target = resolve_path(path, true), base = resolve_path(root);
#ifdef _WIN32
    auto fold = [](fs::path value) {
        auto text = wood::filesystem_support::text(value);
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return fs::path(std::u8string(text.begin(), text.end()));
    };
    target = fold(target);
    base = fold(base);
#endif
    const auto relative = target.lexically_relative(base);
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
}
inline fs::path resolve_path(const fs::path& input, bool allow_missing) {
    std::error_code error;
    auto canonical =
        allow_missing ? fs::weakly_canonical(input, error) : fs::canonical(input, error);
    if (!error)
        return canonical;
#ifndef _WIN32
    throw fs::filesystem_error("cannot resolve path", input, error);
#else
    // Some managed Windows filesystems permit attribute queries and file reads
    // but deny GetFinalPathNameByHandle. Resolve every reparse link explicitly;
    // never substitute unchecked lexical normalization for containment checks.
    auto absolute = fs::absolute(input);
    fs::path result = absolute.root_path();
    std::deque<fs::path> parts;
    for (const auto& part : absolute.relative_path())
        parts.push_back(part);
    unsigned links = 0;
    while (!parts.empty()) {
        auto part = parts.front();
        parts.pop_front();
        if (part == ".")
            continue;
        if (part == "..") {
            result = result.parent_path();
            continue;
        }
        const auto candidate = result / part;
        const auto status = fs::symlink_status(candidate, error);
        if (error && !(allow_missing && error == std::errc::no_such_file_or_directory))
            throw fs::filesystem_error("cannot query path", candidate, error);
        if (fs::is_symlink(status) || (fs::exists(status) && is_link(candidate))) {
            if (++links > 40)
                throw std::runtime_error("too many symbolic links in path");
            const auto target = fs::read_symlink(candidate);
            if (target.is_absolute())
                result = target.root_path();
            std::vector<fs::path> replacement;
            for (const auto& item : (target.is_absolute() ? target.relative_path() : target))
                replacement.push_back(item);
            for (auto item = replacement.rbegin(); item != replacement.rend(); ++item)
                parts.push_front(*item);
        } else {
            if (!allow_missing && !fs::exists(status))
                throw std::runtime_error("path does not exist: " + text(candidate));
            result = candidate;
        }
    }
    return result.lexically_normal();
#endif
}
inline std::string identity(const fs::path& path) {
    auto result = text(resolve_path(path));
#ifdef _WIN32
    for (char& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    return result;
}
} // namespace wood::filesystem_support
#endif
