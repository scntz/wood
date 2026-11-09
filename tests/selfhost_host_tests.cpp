#include "wood/support.hpp"
#include <iostream>
#include <vector>
#include <stdexcept>
extern "C" std::int64_t wood_driver_call(const char*, std::int64_t) noexcept;
extern "C" std::int64_t wood_driver_response(std::int64_t*, std::int64_t) noexcept;
using wood::Json;
namespace fs = std::filesystem;
int checks = 0;
void require(bool condition) { ++checks; if (!condition) throw std::runtime_error("bridge regression failed at check " + std::to_string(checks)); }
Json request(Json::Object input) {
    const auto text = Json(std::move(input)).dump();
    const auto size = wood_driver_call(text.data(), static_cast<std::int64_t>(text.size()));
    require(size > 0 && size <= 65536);
    std::vector<std::int64_t> buffer(static_cast<std::size_t>(size));
    require(wood_driver_response(buffer.data(), size - 1) == -1);
    require(wood_driver_response(buffer.data(), size) == size);
    std::string output;
    for (auto byte : buffer) output += static_cast<char>(byte);
    return Json::parse(output);
}
void null_success(Json::Object input) {
    auto result = request(std::move(input));
    require(result.at("ok").boolean());
    require(std::holds_alternative<std::nullptr_t>(result.at("value").raw()));
}
int main() {
    try {
        wood::TemporaryDirectory fixture;
        auto path = [&](const char* name) { return wood::path_text(fixture.path / wood::path_from(name)); };
        null_success({{"op", "cleanup"}});
        auto temp = request({{"op", "temp"}, {"parent", wood::path_text(fixture.path)}});
        require(temp.at("ok").boolean());
        auto directory = temp.at("value").string();
        require(fs::is_directory(wood::path_from(directory)));
        null_success({{"op", "atomic"}, {"path", path("source")}, {"content", "original"}});
        null_success({{"op", "copy_new"}, {"from", path("source")}, {"to", path("copy")}});
        require(wood::read_file(wood::path_from(path("copy"))) == "original");
        auto failed = request({{"op", "copy_new"}, {"from", path("source")}, {"to", path("copy")}});
        require(!failed.at("ok").boolean());
        null_success({{"op", "publish"}, {"from", path("source")}, {"to", path("copy")}});
        null_success({{"op", "rename"}, {"from", path("copy")}, {"to", path("renamed")}});
        require(fs::is_regular_file(fixture.path / "renamed"));
        null_success({{"op", "release"}, {"path", directory}});
        require(!fs::exists(wood::path_from(directory)));
        require(!request({{"op", "release"}, {"path", wood::path_text(fixture.path)}}).at("ok").boolean());
        require(fs::is_directory(fixture.path));
        require(!request({{"op", "rename"}, {"from", path("missing-árbol-🌳")}, {"to", path("unused")}}).at("ok").boolean());
        auto assets = request({{"op", "assets"}});
        require(assets.at("ok").boolean());
        auto root = wood::path_from(assets.at("value").at("root").string());
        require(fs::is_regular_file(root / "stdlib.json"));
        null_success({{"op", "cleanup"}});
        require(!fs::exists(root));
        std::cout << checks << " hosted bridge checks, 0 failures\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
