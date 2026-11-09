#include "wood/support.hpp"
#include "wood/filesystem.hpp"
#include <cerrno>
#include <deque>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace wood {
std::string path_text(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
fs::path path_from(std::string_view value) {
    return fs::path(std::u8string(value.begin(), value.end()));
}
std::string read_file(const fs::path& path, std::size_t limit) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot read '" + path_text(path) + "'");
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > limit)
        throw std::runtime_error("file exceeds size limit: " + path_text(path));
    std::string text(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    if (!input.read(text.data(), static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read '" + path_text(path) + "'");
    return text;
}
void write_file(const fs::path& path, std::string_view content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output || !output.write(content.data(), static_cast<std::streamsize>(content.size())))
        throw std::runtime_error("cannot write '" + path_text(path) + "'");
    output.close();
    if (!output)
        throw std::runtime_error("cannot close '" + path_text(path) + "'");
}
TemporaryDirectory::TemporaryDirectory(const fs::path& parent) {
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate =
            parent / ("wood-" + std::to_string(random()) + "-" + std::to_string(random()));
#ifdef _WIN32
        std::error_code error;
        if (fs::create_directory(candidate, error)) {
            path = std::move(candidate);
            return;
        }
        if (error && error != std::errc::file_exists)
            throw fs::filesystem_error("cannot create temporary directory", candidate, error);
#else
        if (::mkdir(candidate.c_str(), 0700) == 0) {
            path = std::move(candidate);
            return;
        }
        if (errno != EEXIST)
            throw std::system_error(errno, std::generic_category(),
                                    "cannot create temporary directory");
#endif
    }
    throw std::runtime_error("cannot create unique temporary directory");
}
TemporaryDirectory::~TemporaryDirectory() {
    if (!path.empty()) {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
}
void publish(const fs::path& from, const fs::path& to) {
#ifdef _WIN32
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw fs::filesystem_error(
            "cannot publish output", from, to,
            std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
    fs::rename(from, to);
#endif
}
void write_atomic(const fs::path& path, std::string_view content) {
    const auto target = fs::absolute(path);
    TemporaryDirectory stage(target.parent_path());
    const auto file = stage.path / "output";
    write_file(file, content);
    publish(file, target);
}
bool same_path(const fs::path& a, const fs::path& b) {
    if (fs::absolute(a).lexically_normal() == fs::absolute(b).lexically_normal())
        return true;
    std::error_code error;
    return fs::equivalent(a, b, error) && !error;
}
bool is_link(const fs::path& path) {
    return filesystem_support::is_link(path);
}
bool contained(const fs::path& path, const fs::path& root) {
    return filesystem_support::contained(path, root);
}
fs::path resolve_path(const fs::path& input, bool allow_missing) {
    return filesystem_support::resolve_path(input, allow_missing);
}
} // namespace wood
