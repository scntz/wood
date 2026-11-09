#pragma once
#include "wood/json.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace wood {
namespace fs = std::filesystem;
std::string path_text(const fs::path& path);
fs::path path_from(std::string_view value);
std::string read_file(const fs::path& path, std::size_t limit = 16 * 1024 * 1024);
void write_file(const fs::path& path, std::string_view content);
void publish(const fs::path& from, const fs::path& to);
bool same_path(const fs::path& a, const fs::path& b);
bool contained(const fs::path& path, const fs::path& root);
bool is_link(const fs::path& path);
fs::path resolve_path(const fs::path& path, bool allow_missing = false);
struct TemporaryDirectory {
    fs::path path;
    explicit TemporaryDirectory(const fs::path& parent = fs::temp_directory_path());
    ~TemporaryDirectory();
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};
void write_atomic(const fs::path& path, std::string_view content);
std::string sha256(std::string_view input);

} // namespace wood
