#pragma once
#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include <optional>

namespace wood {
struct Package {
    fs::path root, source, entry, tests;
    std::string name, version, manifest_text;
    std::map<std::string, std::size_t> dependencies;
};
struct Project {
    std::vector<Package> packages;
    std::optional<Json> verified_lock;
    std::vector<PackageRoot> roots() const;
    std::vector<fs::path> tests() const;
    Json lock_document(std::string_view language_version) const;
    void verify_lock(bool required, std::string_view language_version);
    void verify_compilation(const Compilation& compilation) const;
};
fs::path discover_project(const fs::path& start);
Project load_project(const fs::path& manifest);
void init_project(const fs::path& directory);
Json inspect_compilation(const Compilation& compilation);
} // namespace wood
