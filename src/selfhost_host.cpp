// Private hosted-driver OS bridge. No lexer/parser/semantic/codegen dependency.
// Policy, argument parsing and project workflows live in bootstrap/src/driver*.wd.
#include "wood/support.hpp"
#include "wood/process.hpp"
#include "wood/config.hpp"
#include "wood/runtime_source.hpp"
#include "wood/language_info.hpp"
#include "wood/driver_assets.hpp"
#include "error_explanations.hpp"
#include <cstdlib>
#include <memory>
#include <cstring>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef _MSC_VER
// Only binaries linking this private compiler bridge need the frontend stack.
#pragma comment(linker, "/STACK:8388608")
#endif
#endif

namespace {
using wood::Json;
namespace fs = std::filesystem;
std::string response;
std::vector<std::unique_ptr<wood::TemporaryDirectory>> owned;
std::string diagnostic_text(const char* bytes) {
    const std::string value(bytes);
    try { (void)Json(value).dump(); return value; }
    catch (const std::exception&) {
        constexpr char hex[] = "0123456789abcdef";
        std::string safe;
        for (unsigned char byte : value) {
            if (byte < 128) safe += static_cast<char>(byte);
            else { safe += "\\x"; safe += hex[byte / 16]; safe += hex[byte % 16]; }
        }
        return safe;
    }
}
fs::path temporary(const fs::path& parent) {
    auto value = std::make_unique<wood::TemporaryDirectory>(parent);
    auto path = value->path;
    owned.push_back(std::move(value));
    return path;
}
std::string text(const Json& request, const char* key) {
    auto value = request.at(key).string();
    if (value.find('\0') != std::string::npos)
        throw std::runtime_error("path or argument must not contain NUL");
    return value;
}
std::string environment(const std::string& key) {
#ifdef _WIN32
    const auto name = wood::path_from(key).wstring();
    const auto size = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    const auto written = GetEnvironmentVariableW(name.c_str(), value.data(), size);
    if (written >= size) throw std::runtime_error("environment changed while reading");
    value.resize(written);
    return wood::path_text(fs::path(value));
#else
    const auto* value = std::getenv(key.c_str());
    return value ? value : "";
#endif
}
Json call(const Json& request) {
    const auto op = text(request, "op");
    if (op == "assets") {
        const auto root = temporary(fs::temp_directory_path());
        wood::write_file(root / "stdlib.json", wood::driver_standard_bundle);
        wood::write_file(root / "runtime.hpp", wood::runtime_source);
        wood::write_file(root / "language.json", wood::language_info);
        Json::Object explanations;
        for (const auto& entry : wood::detail::error_explanations)
            explanations.emplace(std::string(entry.code), wood::detail::render_error_explanation(entry));
        wood::write_file(root / "explanations.json", Json(std::move(explanations)).dump());
        return Json::Object{{"root", wood::path_text(root)}, {"cxx", std::string(wood::configured_cxx)},
#ifdef _WIN32
                            {"windows", true}
#else
                            {"windows", false}
#endif
        };
    }
    if (op == "environment") return environment(text(request, "name"));
    if (op == "environment_set") {
        const auto name = text(request, "name"), value = text(request, "value");
#ifdef _WIN32
        if (_putenv_s(name.c_str(), value.c_str()) != 0)
#else
        if (setenv(name.c_str(), value.c_str(), 1) != 0)
#endif
            throw std::runtime_error("cannot set child environment");
        return Json();
    }
    if (op == "temp") {
        auto parent = text(request, "parent");
        return wood::path_text(temporary(parent.empty() ? fs::temp_directory_path() : wood::path_from(parent)));
    }
    if (op == "cleanup") { owned.clear(); return Json(); }
    if (op == "release") {
        const auto target = wood::path_from(text(request, "path"));
        for (auto it = owned.begin(); it != owned.end(); ++it)
            if ((*it)->path == target) { owned.erase(it); return Json(); }
        throw std::runtime_error("cannot release an unowned temporary directory");
    }
    if (op == "same") return wood::same_path(wood::path_from(text(request, "left")), wood::path_from(text(request, "right")));
    if (op == "publish") {
        wood::publish(wood::path_from(text(request, "from")), wood::path_from(text(request, "to")));
        return Json();
    }
    if (op == "atomic") {
        wood::write_atomic(wood::path_from(text(request, "path")), request.at("content").string());
        return Json();
    }
    if (op == "copy_new") {
        fs::copy(wood::path_from(text(request, "from")), wood::path_from(text(request, "to")), fs::copy_options::recursive);
        return Json();
    }
    if (op == "rename") {
        const auto from = wood::path_from(text(request, "from"));
        fs::rename(from, wood::path_from(text(request, "to")));
        for (auto it = owned.begin(); it != owned.end(); ++it)
            if ((*it)->path == from) { (*it)->path.clear(); owned.erase(it); break; }
        return Json();
    }
    if (op == "process_stdin_policy")
        return Json(true);
    if (op == "process") {
        std::vector<std::string> args;
        for (const auto& value : request.at("args").array()) args.push_back(value.string());
        const auto timeout = request.at("timeout").integer();
        const auto limit = request.at("limit").integer();
        if (timeout < 0 || timeout > 86400000 || limit < 1 || limit > 16777216)
            throw std::runtime_error("invalid child process limits");
        return wood::run_process(args, wood::path_from(text(request, "log")),
                                 {std::chrono::milliseconds(timeout), static_cast<std::size_t>(limit),
                                  !request.contains("inherit_stdin") || request.at("inherit_stdin").boolean()});
    }
    throw std::runtime_error("unknown hosted-driver OS operation");
}
}
// Sequential, private C ABI. Never let a native exception cross the boundary.
// Large source/runtime bytes use existing bounded Wood streams; replies are small.
extern "C" std::int64_t wood_driver_call(const char* bytes, std::int64_t size) noexcept {
    try {
        if (size < 0 || size > 64 * 1024 * 1024 || (!bytes && size))
            throw std::runtime_error("invalid hosted-driver request size");
        auto value = call(Json::parse(std::string_view(bytes, static_cast<std::size_t>(size))));
        response = Json(Json::Object{{"ok", true}, {"value", std::move(value)}}).dump();
    } catch (const wood::ProcessFailure& error) {
        response = Json(Json::Object{{"ok", false}, {"code", error.code}, {"message", diagnostic_text(error.what())}}).dump();
    } catch (const std::exception& error) {
        response = Json(Json::Object{{"ok", false}, {"code", "D5001"}, {"message", diagnostic_text(error.what())}}).dump();
    } catch (...) {
        response = R"({"ok":false,"code":"D5001","message":"unknown native driver failure"})";
    }
    if (response.size() > 65536)
        response = R"({"ok":false,"code":"D5001","message":"hosted-driver response exceeds 64 KiB"})";
    return static_cast<std::int64_t>(response.size());
}
// The language server belongs to the native CLI, so the reference driver only links these.
extern "C" std::int64_t wood_driver_stdin_read(std::int64_t*, std::int64_t) noexcept { return -1; }
extern "C" std::int64_t wood_driver_stdin_ready(std::int64_t) noexcept { return -1; }
extern "C" std::int64_t wood_driver_stdout_write(const char*, std::int64_t) noexcept { return -1; }
extern "C" std::int64_t wood_driver_response(std::int64_t* output, std::int64_t size) noexcept {
    if (size < 0 || static_cast<std::uint64_t>(size) != response.size() || (!output && size)) return -1;
    for (std::size_t i = 0; i < response.size(); ++i) output[i] = static_cast<unsigned char>(response[i]);
    return size;
}
