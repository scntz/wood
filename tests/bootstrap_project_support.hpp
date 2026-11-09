#pragma once
#include "wood/project.hpp"

namespace wood::bootstrap_test {
using wood::Json;
namespace fs = wood::fs;
using Int = std::int64_t;
inline std::string hex(std::string_view value) {
    constexpr char digits[]="0123456789abcdef";
    std::string out; out.reserve(value.size()*2);
    for(char c:value) { auto b=static_cast<unsigned char>(c); out+=digits[b>>4]; out+=digits[b&15]; }
    return out;
}
inline Json diagnostic(std::string code={},std::string message={},std::string file={},wood::Location at={0,0,0,0}) {
    return Json::Object{{"code",code},{"message_hex",hex(message)},{"file",file},{"source",static_cast<Int>(at.source)},
        {"offset",static_cast<Int>(at.offset)},{"line",static_cast<Int>(at.line)},{"column",static_cast<Int>(at.column)}};
}
inline Json reply(Json error,Json packages=Json::Array{},Json units=Json::Array{},Json symbols=Json::Array{},Int next=1) {
    return Json::Object{{"format",1},{"stage","module-bindings"},{"types_checked",false},{"lock_verified",false},{"diagnostic",std::move(error)},
        {"entry",0},{"packages",std::move(packages)},{"modules",std::move(units)},{"symbols",std::move(symbols)},{"next_symbol",next}};
}
struct Input {
    std::vector<wood::PackageRoot> roots;
    Json::Array packages;
    fs::path entry;
};
inline Input configure(const fs::path& input) {
    Input result;
    const auto absolute=fs::absolute(input);
    const bool script=absolute.extension()==".wd";
    const auto manifest=script || fs::is_directory(absolute) ? wood::discover_project(absolute) : absolute;
    if (!manifest.empty() && manifest.filename()=="wood.json") {
        auto project=wood::load_project(manifest);
        result.roots=project.roots();
        result.entry=script ? absolute : project.packages[0].entry;
        if (result.entry.empty()) throw std::runtime_error("project has no entry file");
        for (const auto& package:project.packages) {
            Json::Object dependencies;
            for(const auto& [name,index]:package.dependencies) dependencies.emplace(name,static_cast<Int>(index));
            result.packages.emplace_back(Json::Object{{"root",wood::path_text(package.root)},
                {"source_root",wood::path_text(package.source)},{"entry",wood::path_text(package.entry)},
                {"test_root",wood::path_text(package.tests)},{"name",package.name},{"version",package.version},
                {"dependencies",std::move(dependencies)}});
        }
    } else if (script) {
        result.entry=wood::resolve_path(absolute);
        const auto root=result.entry.parent_path();
        result.roots.push_back({root,{},{}});
        result.packages.emplace_back(Json::Object{{"root",wood::path_text(root)},
            {"source_root",wood::path_text(root)},{"entry",wood::path_text(result.entry)},
            {"test_root",""},{"name",""},{"version",""},{"dependencies",Json::Object{}}});
    } else throw std::runtime_error("no wood.json project found; provide a .wd file or run 'wood init PATH'");
    return result;
}
}
