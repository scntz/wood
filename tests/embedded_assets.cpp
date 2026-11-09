#include "wood/runtime_source.hpp"
#include "wood/language_info.hpp"
#include "wood/driver_assets.hpp"
#include "wood/standard_sources.hpp"
#include "wood/embedded_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {
bool matches(std::string_view embedded, const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot read embedded asset oracle: " << path << '\n';
        return false;
    }
    std::string source{std::istreambuf_iterator<char>{file}, {}};
    // Physical CRLF newlines in a raw C++ literal are normalized by the C++
    // translation phases. Do not alter escaped backslash-r data in JSON/source.
    for (std::size_t at = 0; (at = source.find("\r\n", at)) != std::string::npos;)
        source.erase(at, 1);
    if (embedded != source) {
        std::cerr << "Embedded bytes differ: " << path << '\n';
        return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    if (argc != 6) return 2;
    int checks = 0;
    bool passed = true;
    const auto check = [&](std::string_view value, const std::filesystem::path& file) {
        ++checks;
        if (!matches(value, file)) passed = false;
    };
    check(wood::runtime_source, argv[1]);
    check(wood::language_info, argv[2]);
    check(wood::driver_standard_bundle, argv[3]);
    check(wood::embedded_fixture, argv[5]);
    ++checks;
    if (!wood::embedded_empty.empty()) passed = false;
    for (const auto& [name, source] : wood::standard_sources) {
        check(source, std::filesystem::path{argv[4]} /
            (std::string{name.substr(4)} + ".wd"));
    }
    if (!passed) return 1;
    std::cout << checks << " embedded asset byte comparisons passed\n";
}
