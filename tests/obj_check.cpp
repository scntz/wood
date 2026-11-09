#include "wood/json.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>
namespace {
using V = std::array<double, 3>;
V sub(V a, V b) {
    return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
}
V cross(V a, V b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
double dot(V a, V b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool near(double a, double b) {
    return std::abs(a - b) < 1e-9 * (1 + std::abs(b));
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        require(argc == 5, "expected OBJ REPORT RINGS SIDES");
        const int rings = std::stoi(argv[3]), sides = std::stoi(argv[4]);
        std::ifstream input{std::filesystem::path(argv[1])};
        require(bool(input), "cannot open OBJ");
        std::vector<V> vertices, normals;
        std::vector<std::array<std::size_t, 3>> triangles;
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream row(line);
            std::string kind;
            row >> kind;
            if (kind.empty() || kind == "#")
                continue;
            if (kind == "v" || kind == "vn") {
                V value{};
                require(bool(row >> value[0] >> value[1] >> value[2]), "invalid coordinate");
                for (const auto component : value)
                    require(std::isfinite(component), "nonfinite OBJ coordinate");
                if (kind == "v")
                    vertices.push_back(value);
                else
                    normals.push_back(value);
            } else if (kind == "f") {
                std::array<std::size_t, 3> triangle{};
                for (auto& index : triangle) {
                    std::string token;
                    require(bool(row >> token), "short OBJ face");
                    const auto separator = token.find("//");
                    require(separator != std::string::npos, "expected vertex//normal face");
                    const auto vertex = std::stoull(token.substr(0, separator)),
                               normal = std::stoull(token.substr(separator + 2));
                    require(vertex > 0 && vertex == normal, "invalid normal index");
                    index = static_cast<std::size_t>(vertex - 1);
                }
                triangles.push_back(triangle);
            } else
                require(false, "unexpected OBJ statement");
            std::string extra;
            require(!(row >> extra), "unexpected OBJ fields");
        }
        const auto count = static_cast<std::size_t>(rings * sides);
        require(vertices.size() == count && normals.size() == count &&
                    triangles.size() == 2 * count,
                "wrong mesh counts");
        constexpr double tau = 6.283185307179586;
        for (int ring = 0; ring < rings; ++ring)
            for (int side = 0; side < sides; ++side) {
                const auto u = tau * ring / rings, v = tau * side / sides;
                const auto radius = 2.0 + 0.5 * std::cos(v);
                const double x = 1.25 * radius * std::cos(u), y = 0.75 * 0.5 * std::sin(v),
                             z = 1.5 * radius * std::sin(u);
                const V expected{std::cos(0.4) * x + std::sin(0.4) * z + 1, y + 2,
                                 -std::sin(0.4) * x + std::cos(0.4) * z - 3};
                const auto index = static_cast<std::size_t>(ring * sides + side);
                for (int axis = 0; axis < 3; ++axis)
                    require(near(vertices[index][axis], expected[axis]),
                            "vertex differs from independent analytic transform");
                require(near(dot(normals[index], normals[index]), 1), "normal is not unit length");
            }
        std::map<std::pair<std::size_t, std::size_t>, std::pair<int, int>> edges;
        double area = 0;
        for (const auto& face : triangles) {
            for (auto index : face)
                require(index < count, "OBJ index out of range");
            const auto normal = cross(sub(vertices[face[1]], vertices[face[0]]),
                                      sub(vertices[face[2]], vertices[face[0]]));
            require(dot(normal, normal) > 0, "degenerate triangle");
            area += 0.5 * std::sqrt(dot(normal, normal));
            for (const auto index : face)
                require(dot(normal, normals[index]) > 0, "inconsistent face/vertex normal winding");
            for (int side = 0; side < 3; ++side) {
                auto a = face[side], b = face[(side + 1) % 3];
                const int direction = a < b ? 1 : -1;
                if (a > b)
                    std::swap(a, b);
                auto& edge = edges[{a, b}];
                ++edge.first;
                edge.second += direction;
            }
        }
        for (const auto& [edge, state] : edges) {
            (void)edge;
            require(state.first == 2 && state.second == 0,
                    "mesh is not an oriented closed manifold");
        }
        require(count + triangles.size() == edges.size(), "torus Euler characteristic is not zero");
        std::ifstream report_input{std::filesystem::path(argv[2])};
        const std::string text((std::istreambuf_iterator<char>(report_input)), {});
        const auto report = wood::Json::parse(text);
        require(near(std::get<double>(report.at("area").raw()), area),
                "reported mesh area differs from independent triangle sum");
        std::cout << count << " vertices, " << triangles.size()
                  << " faces: analytic coordinates, unit normals, winding, closed edges, Euler "
                     "characteristic and area passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
