#include "wood_runtime.hpp"
#include <random>
namespace {
constexpr wd::Site array_site{"array-test.wd", 1, 1};
int checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << name << '\n';
    }
}
template <typename F> void fails(F action, const std::string& code) {
    try {
        action();
        check(false, "missing array error");
    } catch (const wd::Error& error) {
        check(error.code == code, "array error code");
    }
}
bool close(double a, double b) {
    return std::abs(a - b) <= 1e-11 * (1 + std::abs(b));
}
} // namespace
int main() {
    using wd::Int;
    for (Int rows = 0; rows <= 8; ++rows)
        for (Int columns = 0; columns <= 8; ++columns) {
            auto a = wd::Array<double>::full(std::tuple{rows, columns}, 0.0, array_site);
            for (Int i = 0; i < rows; ++i)
                for (Int j = 0; j < columns; ++j)
                    a.mut_at(std::tuple{i, j}, array_site) =
                        static_cast<double>(i * columns + j) + 0.125;
            auto t = a.transpose({}, array_site);
            for (Int i = 0; i < rows; ++i)
                for (Int j = 0; j < columns; ++j)
                    check(t.get(std::tuple{j, i}, array_site) ==
                              static_cast<double>(i * columns + j) + 0.125,
                          "independent transpose oracle");
            auto reversed = a.sliced(0, std::nullopt, std::nullopt, -1, array_site);
            for (Int i = 0; i < rows; ++i)
                for (Int j = 0; j < columns; ++j)
                    check(reversed.get(std::tuple{i, j}, array_site) ==
                              static_cast<double>((rows - 1 - i) * columns + j) + 0.125,
                          "negative stride oracle");
            auto dense = reversed.copy(array_site);
            check(dense == reversed && dense.contiguous(), "materialization");
            if (rows && columns) {
                dense.mut_at(std::tuple{Int{0}, Int{0}}, array_site) = 42;
                check(reversed.get(std::tuple{Int{0}, Int{0}}, array_site) != 42,
                      "view COW isolation");
                auto snapshot = a;
                a.mut_at(std::tuple{Int{0}, Int{0}}, array_site) = 99;
                check(snapshot.get(std::tuple{Int{0}, Int{0}}, array_site) == 0.125,
                      "array COW isolation");
            }
            for (Int outputs = 0; outputs <= 7; ++outputs) {
                auto b = wd::Array<double>::full(std::tuple{columns, outputs}, 0.0, array_site);
                for (Int i = 0; i < columns; ++i)
                    for (Int j = 0; j < outputs; ++j)
                        b.mut_at(std::tuple{i, j}, array_site) = static_cast<double>(i - j) * 0.25;
                auto product = wd::array_matmul(a, b, array_site);
                auto reverse_product = wd::array_matmul(
                    a.sliced(0, std::nullopt, std::nullopt, -1, array_site),
                    b.sliced(1, std::nullopt, std::nullopt, -1, array_site), array_site);
                for (Int i = 0; i < rows; ++i)
                    for (Int j = 0; j < outputs; ++j) {
                        double expected = 0, expected_reverse = 0;
                        for (Int k = 0; k < columns; ++k) {
                            const double av = (i == 0 && k == 0)
                                                  ? 99.0
                                                  : static_cast<double>(i * columns + k) + 0.125;
                            expected += av * static_cast<double>(k - j) * 0.25;
                            const double ar =
                                (rows - 1 - i == 0 && k == 0)
                                    ? 99.0
                                    : static_cast<double>((rows - 1 - i) * columns + k) + 0.125;
                            expected_reverse +=
                                ar * static_cast<double>(k - (outputs - 1 - j)) * 0.25;
                        }
                        check(close(product.get(std::tuple{i, j}, array_site), expected),
                              "independent matmul oracle");
                        check(close(reverse_product.get(std::tuple{i, j}, array_site),
                                    expected_reverse),
                              "strided matmul oracle");
                    }
            }
        }
    auto a = wd::array_from_list(wd::List<Int>{1, 2, 3, 4}, array_site);
    auto snapshot = a;
    auto reversed = a.sliced(0, std::nullopt, std::nullopt, -1, array_site);
    auto sum = wd::array_add(a, reversed, array_site);
    check(wd::array_to_list(sum, array_site) == wd::List<Int>{5, 5, 5, 5},
          "overlapping views remain snapshots");
    auto promoted = wd::array_mul(a, 0.5, array_site);
    check(wd::array_to_list(promoted, array_site) == wd::List<double>{0.5, 1.0, 1.5, 2.0},
          "numeric promotion");
    check(a == snapshot, "input preserved");
    auto extreme = wd::array_from_list(wd::List<Int>{wd::max_int, 1}, array_site);
    fails([&] { wd::array_add(extreme, Int{1}, array_site); }, "R4001");
    check(extreme.get(0, array_site) == wd::max_int, "overflow leaves input intact");
    fails([&] { wd::array_div(a, Int{0}, array_site); }, "R4002");
    fails([&] { wd::Array<double>::full(Int{-1}, 0.0, array_site); }, "R4026");
    fails([&] { wd::Array<double>::full(wd::List<Int>{}, 0.0, array_site); }, "R4026");
    fails([&] { wd::Array<double>::full(std::tuple{wd::max_int, Int{2}}, 0.0, array_site); },
          "R4026");
    fails([&] { a.get(4, array_site); }, "R4006");
    fails([&] { a.get(wd::min_int, array_site); }, "R4006");
    fails([&] { a.get(std::tuple{Int{0}, Int{0}}, array_site); }, "R4026");
    fails([&] { a.reshape(Int{3}, array_site); }, "R4026");
    fails([&] { a.transpose({1}, array_site); }, "R4026");
    fails([&] { reversed.foreign_data(array_site); }, "R4026");
    fails([&] { reversed.foreign_mutable_data(array_site); }, "R4026");
    fails([&] { wd::math_sqrt(-1, array_site); }, "R4027");
    fails([&] { wd::array_dot(a, a.reshape(std::tuple{Int{2}, Int{2}}, array_site), array_site); },
          "R4026");
    auto singleton = a.sliced(0, Int{3}, std::nullopt, wd::min_int, array_site);
    check(singleton.size() == 1 && singleton.get(0, array_site) == 4,
          "extreme slice stride singleton");
    auto moved = std::move(singleton);
    check(singleton.size() == 0 && moved.size() == 1, "moved descriptor remains valid");
    check(wd::array_sum(wd::array_from_list(wd::List<double>{1e16, 1, -1e16}, array_site),
                        array_site) == 1,
          "compensated cancellation");
    auto ffi = wd::Array<double>::full(Int{4}, 1.0, array_site);
    auto ffi_snapshot = ffi;
    ffi.foreign_mutable_data(array_site)[0] = 7;
    check(ffi_snapshot.get(0, array_site) == 1 && ffi.get(0, array_site) == 7,
          "foreign mutable detach");
    auto cube = wd::Array<Int>::full(std::tuple{Int{2}, Int{3}, Int{4}}, 0, array_site);
    auto* cube_data = cube.foreign_mutable_data(array_site);
    for (Int i = 0; i < 24; ++i)
        cube_data[i] = i;
    auto permuted = cube.transpose({2, 0, 1}, array_site);
    for (Int x = 0; x < 2; ++x)
        for (Int y = 0; y < 3; ++y)
            for (Int z = 0; z < 4; ++z)
                check(permuted.get(std::tuple{z, x, y}, array_site) == x * 12 + y * 4 + z,
                      "rank-three permutation oracle");
    auto rank8 = wd::Array<Int>::full(
        std::tuple{Int{1}, Int{1}, Int{1}, Int{1}, Int{1}, Int{1}, Int{1}, Int{1}}, 7, array_site);
    check(rank8.get(std::tuple{Int{0}, Int{0}, Int{0}, Int{0}, Int{0}, Int{0}, Int{0}, Int{0}},
                    array_site) == 7,
          "rank-eight boundary");
    fails([&] { wd::Array<Int>::full(wd::List<Int>{1, 1, 1, 1, 1, 1, 1, 1, 1}, 0, array_site); },
          "R4026");
    auto transform = wd::Array<double>::full(std::tuple{Int{4}, Int{4}}, 0.0, array_site);
    auto* coefficients = transform.foreign_mutable_data(array_site);
    for (Int row = 0; row < 4; ++row)
        coefficients[row * 4 + row] = 1;
    coefficients[3] = 4;
    coefficients[7] = 5;
    coefficients[11] = 6;
    for (Int count = 0; count < 32; ++count) {
        auto points = wd::Array<double>::full(std::tuple{count, Int{3}}, 0.0, array_site);
        auto* data = points.foreign_mutable_data(array_site);
        for (Int i = 0; i < count * 3; ++i)
            data[i] = static_cast<double>(i) * 0.125;
        auto moved_points = wd::array_affine(points, transform, array_site);
        auto reversed_points = wd::array_affine(
            points.sliced(0, std::nullopt, std::nullopt, -1, array_site), transform, array_site);
        for (Int i = 0; i < count; ++i)
            for (Int axis = 0; axis < 3; ++axis) {
                check(moved_points.get(std::tuple{i, axis}, array_site) ==
                          static_cast<double>(i * 3 + axis) * 0.125 + axis + 4,
                      "affine independent oracle");
                check(reversed_points.get(std::tuple{i, axis}, array_site) ==
                          static_cast<double>((count - 1 - i) * 3 + axis) * 0.125 + axis + 4,
                      "strided affine oracle");
                check(points.get(std::tuple{i, axis}, array_site) ==
                          static_cast<double>(i * 3 + axis) * 0.125,
                      "affine input retained");
            }
        auto consumed = wd::array_affine(std::move(points), transform, array_site);
        check(consumed == moved_points, "unique affine reuse");
    }
    coefficients = transform.foreign_mutable_data(array_site);
    coefficients[12] = 1;
    fails(
        [&] {
            wd::array_affine(wd::Array<double>::full(std::tuple{Int{0}, Int{3}}, 0.0, array_site),
                             transform, array_site);
        },
        "R4026");
    std::cout << checks << " array checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
