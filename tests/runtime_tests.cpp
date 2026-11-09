#include "wood_runtime.hpp"
#include <bit>
#include <functional>
#include <random>
#include <vector>

namespace {
int checks = 0, failures = 0;
constexpr wd::Site test_site{"test.wd", 1, 1};
template <std::size_t Size> struct TrackedCallable {
    static inline int alive = 0, allocations = 0;
    static inline bool throw_copy = false;
    std::byte padding[Size]{};
    wd::Int value = 0;
    TrackedCallable() {
        ++alive;
    }
    TrackedCallable(const TrackedCallable& other) : value(other.value) {
        if (throw_copy)
            throw std::runtime_error("copy failure");
        ++alive;
    }
    TrackedCallable(TrackedCallable&& other) noexcept : value(other.value) {
        ++alive;
    }
    ~TrackedCallable() {
        --alive;
    }
    wd::Int operator()(wd::Int amount) {
        return value += amount;
    }
    static void* operator new(std::size_t size) {
        ++allocations;
        return ::operator new(size);
    }
    static void operator delete(void* value) {
        ::operator delete(value);
    }
};
struct alignas(64) AlignedCallable {
    wd::Int value = 42;
    std::byte padding[56]{};
    wd::Int operator()() {
        return value;
    }
};
void check(bool value, const std::string& name) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << name << '\n';
    }
}
void fails(const std::function<void()>& action, const std::string& code) {
    try {
        action();
        check(false, "missing runtime error " + code);
    } catch (const wd::Error& error) {
        check(error.code == code, "runtime error code");
    }
}
std::vector<wd::Int> range(wd::Int start, wd::Int stop, wd::Int step) {
    wd::Range cursor(start, stop, step, test_site);
    std::vector<wd::Int> result;
    while (cursor.has_next()) {
        result.push_back(cursor.next());
        if (result.size() > 1000)
            throw std::runtime_error("range failed to terminate");
    }
    return result;
}
} // namespace
int main() {
    using wd::Int;
    // Independent oracle: the former guarded-advance algorithm. Check prefixes
    // of enormous ranges without materializing them or duplicating the count formula.
    const auto compare_range = [](Int start, Int stop, Int step, int limit) {
        wd::Range actual(start, stop, step, test_site);
        Int value = start;
        bool done = step > 0 ? start >= stop : start <= stop;
        bool same = true;
        for (int i = 0; i < limit; ++i) {
            if (actual.has_next() != !done) {
                same = false;
                break;
            }
            if (done)
                break;
            if (actual.next() != value) {
                same = false;
                break;
            }
            if ((step > 0 && value > wd::max_int - step) ||
                (step < 0 && value < wd::min_int - step))
                done = true;
            else {
                value += step;
                done = step > 0 ? value >= stop : value <= stop;
            }
        }
        check(same && actual.has_next() == !done, "range matches guarded oracle");
    };
    for (Int start = -12; start <= 12; ++start)
        for (Int stop = -12; stop <= 12; ++stop)
            for (Int step = -12; step <= 12; ++step)
                if (step != 0)
                    compare_range(start, stop, step, 30);
    std::mt19937_64 range_random(0x574f4f44);
    for (int i = 0; i < 10000; ++i) {
        const auto start = std::bit_cast<Int>(range_random()),
                   stop = std::bit_cast<Int>(range_random());
        auto step = std::bit_cast<Int>(range_random());
        if (step == 0)
            step = 1;
        compare_range(start, stop, step, 32);
    }
    for (const auto step : {Int{1}, Int{2}, wd::max_int, wd::min_int, Int{-1}, Int{-2}}) {
        compare_range(wd::min_int, wd::max_int, step, 32);
        compare_range(wd::max_int, wd::min_int, step, 32);
    }
    using Small = TrackedCallable<1>;
    using Large = TrackedCallable<128>;
    Int consumed = 0;
    wd::Callable<void, Int> discard_result{[&](Int value) { return consumed += value; }};
    discard_result(3);
    check(consumed == 3, "void callable discards an invocable result");
    {
        wd::Callable<Int, Int> original{Small{}};
        check(original(2) == 2 && original(3) == 5, "mutable inline callable state");
        auto copy = original;
        check(copy(1) == 6 && original(2) == 7, "owned callable copy isolates state");
        wd::Callable<Int, Int> moved{std::move(copy)};
        check(moved(3) == 9, "move preserves callable state");
        fails([&] { copy(1); }, "R4025");
        original = original;
        check(original(1) == 8, "self assignment");
        Small::throw_copy = true;
        try {
            original = moved;
            check(false, "expected throwing copy");
        } catch (const std::runtime_error&) {
            check(original(1) == 9, "copy assignment strong guarantee");
        }
        Small::throw_copy = false;
        original.swap(moved);
        check(original(1) == 10 && moved(1) == 10, "inline swap and state");
        check(Small::allocations == 0,
              "small callable copy/move/swap never allocate target storage");
        wd::Callable<Int, Int> large{Large{}};
        auto large_copy = large;
        check(Large::allocations == 2 && large(3) == 3 && large_copy(2) == 2,
              "large captures use independently owned heap storage");
        large.swap(original);
        check(large(1) == 11 && original(1) == 4, "mixed inline and heap swap");
        auto large_move = std::move(large_copy);
        check(large_move(1) == 3 && Large::allocations == 2,
              "heap move transfers without allocation");
        wd::Callable<Int> aligned{AlignedCallable{}};
        check(aligned() == 42 && aligned() == 42, "over-aligned capture storage");
    }
    check(Small::alive == 0 && Large::alive == 0, "all callable targets destroyed exactly once");
    auto absent_item = wd::yielded(std::optional<Int>{});
    check(wd::has_item(absent_item) && !wd::step_value(absent_item, test_site),
          "optional None payload differs from exhausted step");
    auto none_item = wd::yielded(wd::None{});
    check(wd::has_item(none_item), "None is a valid yielded item");
    fails([] { wd::step_value(wd::Step<Int>{}, test_site); }, "R4024");
    auto native_cursor = wd::iter(wd::List<Int>{1, 2});
    auto native_cursor_copy = native_cursor;
    check(wd::step_value(wd::next(native_cursor, test_site), test_site) == 1 &&
              wd::step_value(wd::next(native_cursor_copy, test_site), test_site) == 1,
          "cursor snapshot copies progress independently");
    auto edge_range = wd::iter(wd::make_range(wd::max_int - 1, wd::max_int, 2, test_site));
    check(wd::step_value(wd::next(edge_range, test_site), test_site) == wd::max_int - 1 &&
              !wd::has_item(wd::next(edge_range, test_site)),
          "range cursor stops before overflow");
    check(wd::to_json(wd::None{}, test_site).dump() == "null", "None JSON null");
    check(wd::to_json(std::optional<Int>{}, test_site).dump() == "null",
          "empty optional JSON null");
    check(wd::to_json(std::optional<Int>{5}, test_site).dump() == "5",
          "present optional JSON number");
    check(wd::to_json(std::tuple{wd::None{}, std::optional<Int>{}, std::tuple{Int{2}}}, test_site)
                  .dump() == "[null,null,[2]]",
          "nested tuple optional JSON");
    check(wd::to_str(std::tuple{wd::None{}, std::string("x")}) == "(None, \"x\")",
          "heterogeneous tuple formatting");
    check(wd::to_str(std::tuple{}) == "()", "empty tuple formatting");
    check(wd::to_str(std::tuple{Int{1}}) == "(1,)", "singleton tuple formatting");
    fails([] { (void)wd::unwrap(std::optional<Int>{}, test_site); }, "R4023");
    check(wd::equal(wd::None{}, std::optional<Int>{}) &&
              wd::equal(std::optional<Int>{}, wd::None{}),
          "None tests both operand orders");
    check(!wd::equal(wd::None{}, std::optional<Int>{1}), "present value is not None");
    const auto reverse = wd::slice_indices(6, {}, {}, wd::min_int, test_site);
    check(reverse.start == 5 && reverse.count == 1 && reverse.step == wd::min_int,
          "minimum signed slice stride without negation overflow");
    check(wd::slice_indices(6, {}, Int{-1}, -1, test_site).count == 0,
          "omitted reverse stop differs from explicit minus one");
    check(wd::slice_indices(0, {}, {}, -1, test_site).count == 0, "empty reverse slice");
    if constexpr (sizeof(std::size_t) >= sizeof(Int)) {
        const auto huge =
            wd::slice_indices(static_cast<std::size_t>(wd::max_int), {}, {}, 1, test_site);
        check(huge.count == static_cast<std::size_t>(wd::max_int),
              "slice cardinality at maximum logical size without allocation");
    }
    fails([] { (void)wd::slice_indices(3, {}, {}, 0, test_site); }, "R4005");
    fails([] { (void)wd::repeat(std::string("xx"), wd::max_int, test_site); }, "R4003");
    fails([] { (void)wd::repeat(wd::List<Int>{1, 2}, wd::max_int, test_site); }, "R4003");
    check(wd::repeat(std::string(""), wd::max_int, test_site).empty(), "empty huge repetition");
    auto repeated = wd::repeat(wd::List<wd::List<Int>>{{1}}, 2, test_site);
    repeated.mut_at(0, test_site).mut_at(0, test_site) = 7;
    check(repeated.get(1, test_site).get(0, test_site) == 1,
          "repeated nested lists preserve independent mutation");
    check(wd::time_sleep_duration(0.0, test_site).count() == 0, "zero sleep");
    check(wd::time_sleep_duration(-0.0, test_site).count() == 0, "negative zero sleep");
    check(wd::time_sleep_duration(0.0000000001, test_site).count() == 1,
          "positive subnanosecond sleep rounds up");
    check(wd::time_sleep_duration(86400.0, test_site).count() == 86400000000000LL,
          "maximum sleep duration converts without actually sleeping");
    for (double seconds :
         {-1.0, 86400.0001, std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        fails([&] { (void)wd::time_sleep_duration(seconds, test_site); }, "R4015");
    using Clock = std::chrono::steady_clock;
    check(wd::time_counter_ns(Clock::duration::zero(), test_site) == 0, "counter zero");
    fails([] { (void)wd::time_counter_ns(Clock::duration{-1}, test_site); }, "R4015");
    if constexpr (std::ratio_equal_v<Clock::period, std::nano>) {
        check(wd::time_counter_ns(Clock::duration{9007199254740993LL}, test_site) ==
                  9007199254740993LL,
              "integer clock retains precision above binary64 exact range");
        check(wd::time_counter_ns(Clock::duration{wd::max_int}, test_site) == wd::max_int,
              "counter conversion accepts int64 nanosecond boundary");
    }
    check(wd::add(wd::max_int, 0, test_site) == wd::max_int, "max plus zero");
    check(wd::add(wd::min_int, 0, test_site) == wd::min_int, "min plus zero");
    fails([] { (void)wd::add(wd::max_int, 1, test_site); }, "R4001");
    fails([] { (void)wd::add(wd::min_int, -1, test_site); }, "R4001");
    fails([] { (void)wd::sub(wd::max_int, -1, test_site); }, "R4001");
    fails([] { (void)wd::sub(wd::min_int, 1, test_site); }, "R4001");
    fails([] { (void)wd::neg(wd::min_int, test_site); }, "R4001");
    fails([] { (void)wd::mul(wd::min_int, -1, test_site); }, "R4001");
    fails([] { (void)wd::mul(-1, wd::min_int, test_site); }, "R4001");
    fails([] { (void)wd::mul(wd::max_int, 2, test_site); }, "R4001");
    fails([] { (void)wd::mul(wd::min_int, 2, test_site); }, "R4001");
    fails([] { (void)wd::mul(-3037000500LL, -3037000500LL, test_site); }, "R4001");
    check(wd::mul(wd::min_int, 1, test_site) == wd::min_int, "min times one");
    check(wd::mul(wd::max_int, -1, test_site) == -wd::max_int, "max times minus one");
    check(wd::mul(wd::min_int, 0, test_site) == 0, "min times zero");
    check(wd::shift_left(-1, 63, test_site) == wd::min_int, "extreme negative left shift");
    check(wd::shift_right(wd::min_int, 63, test_site) == -1, "extreme right shift");
    for (Int count = 0; count < 63; ++count)
        for (Int value : {wd::min_int, Int{-7}, Int{-1}, Int{0}, Int{7}, wd::max_int})
            check(wd::shift_right(value, count, test_site) ==
                      wd::floor_div(value, Int{1} << count, test_site),
                  "arithmetic right shift agrees with exact floor division");
    fails([] { (void)wd::shift_left(1, 63, test_site); }, "R4001");
    fails([] { (void)wd::shift_left(-2, 63, test_site); }, "R4001");
    fails([] { (void)wd::shift_right(1, -1, test_site); }, "R4014");
    fails([] { (void)wd::shift_left(1, 64, test_site); }, "R4014");
    check(wd::mul(-3037000499LL, -3037000499LL, test_site) == 9223372030926249001LL,
          "large safe product");
    fails([] { (void)wd::floor_div(1, 0, test_site); }, "R4002");
    fails([] { (void)wd::mod(1, 0, test_site); }, "R4002");
    fails([] { (void)wd::div(1.0, -0.0, test_site); }, "R4002");
    fails([] { (void)wd::floor_div(wd::min_int, -1, test_site); }, "R4001");
    check(wd::mod(wd::min_int, -1, test_site) == 0, "min modulo minus one");
    for (Int a = -32; a <= 32; ++a)
        for (Int b = -32; b <= 32; ++b) {
            check(wd::add(a, b, test_site) == a + b && wd::sub(a, b, test_site) == a - b &&
                      wd::mul(a, b, test_site) == a * b,
                  "small arithmetic");
            if (b == 0)
                continue;
            const auto q = wd::floor_div(a, b, test_site), r = wd::mod(a, b, test_site);
            check(q == static_cast<Int>(
                           std::floor(static_cast<double>(a) / static_cast<double>(b))) &&
                      a == b * q + r && (r == 0 || ((r < 0) == (b < 0))),
                  "floor division identity");
        }
    check(wd::to_int(-1.75, test_site) == -1 && wd::to_int(1.75, test_site) == 1,
          "float truncation");
    check(wd::to_int(-0x1p63, test_site) == wd::min_int, "float min conversion");
    fails([] { (void)wd::to_int(0x1p63, test_site); }, "R4003");
    fails([] { (void)wd::to_int(std::numeric_limits<double>::infinity(), test_site); }, "R4003");
    fails([] { (void)wd::to_int(std::numeric_limits<double>::quiet_NaN(), test_site); }, "R4003");
    check(wd::len(std::string("a\0b", 3), test_site) == 3, "embedded NUL length");
    check(wd::to_str(true) == "true" && wd::to_str(false) == "false", "bool formatting");
    for (Int byte = 0; byte <= 255; ++byte)
        check(wd::byte_value(wd::byte_string(byte, test_site), test_site) == byte,
              "all byte values round trip");
    check(wd::to_str(wd::min_int) == "-9223372036854775808", "integer formatting");
    check(wd::to_str(1.0) == "1.0" && wd::to_str(1.5) == "1.5", "float formatting");
    fails([] { wd::assert_that(false, test_site, "expected"); }, "R4004");
    fails([] { wd::Range invalid(0, 10, 0, test_site); }, "R4005");
    check(range(0, 5, 2) == std::vector<Int>{0, 2, 4}, "positive range");
    check(range(5, 0, -2) == std::vector<Int>{5, 3, 1}, "negative range");
    check(range(0, 5, -1).empty() && range(5, 0, 1).empty() && range(1, 1, 1).empty(),
          "empty ranges");
    check(range(wd::max_int - 1, wd::max_int, 2) == std::vector<Int>{wd::max_int - 1},
          "range positive overflow termination");
    check(range(wd::min_int + 1, wd::min_int, -2) == std::vector<Int>{wd::min_int + 1},
          "range negative overflow termination");
    check(range(0, wd::min_int, wd::min_int) == std::vector<Int>{0}, "min range step");
    check(range(wd::max_int, wd::min_int, wd::min_int) == std::vector<Int>{wd::max_int, -1},
          "extreme range step");
    wd::List<Int> list{1, 2, 3};
    auto copy = list;
    copy.mut_at(0, test_site) = 9;
    check(list.get(0, test_site) == 1 && copy.get(0, test_site) == 9, "COW indexed mutation");
    copy = list;
    copy.push(4, test_site);
    check(list.size() == 3 && copy.size() == 4, "COW append");
    copy = list;
    check(copy.pop(test_site) == 3 && copy.size() == 2 && list.size() == 3, "COW pop");
    wd::List<wd::List<Int>> matrix{list};
    auto saved = matrix;
    matrix.mut_at(0, test_site).mut_at(0, test_site) = 8;
    check(saved.get(0, test_site).get(0, test_site) == 1, "nested COW isolation");
    wd::List<bool> flags{false};
    flags.mut_at(0, test_site) = true;
    check(flags.get(0, test_site), "bool element reference");
    check(wd::at(list, -1, test_site) == 3 && wd::at(list, -3, test_site) == 1,
          "negative list indexes");
    fails([&] { (void)list.get(-4, test_site); }, "R4006");
    fails([&] { (void)list.get(wd::min_int, test_site); }, "R4006");
    fails([&] { (void)list.mut_at(3, test_site); }, "R4006");
    wd::List<Int> empty;
    fails([&] { (void)empty.get(0, test_site); }, "R4006");
    fails([&] { (void)empty.pop(test_site); }, "R4007");
    check(empty == wd::List<Int>{}, "empty list equality");
    wd::List<double> nonfinite{std::numeric_limits<double>::quiet_NaN()};
    check(!(nonfinite == nonfinite), "list equality uses float value rules, not storage identity");
    check(wd::to_str(matrix) == "[[8, 2, 3]]", "nested formatting");
    check(wd::to_str(wd::List<std::string>{"a\n", "b"}) == "[\"a\\n\", \"b\"]",
          "list string escaping");
    check(wd::split("a,,b,", ",", test_site) == wd::List<std::string>{"a", "", "b", ""},
          "split preserves empty fields");
    check(wd::split("", ",", test_site) == wd::List<std::string>{""}, "split empty text");
    check(wd::join(wd::List<std::string>{}, ",", test_site).empty(), "join empty list");
    check(wd::replace("aaa", "aa", "x", test_site) == "xa", "nonoverlapping replace");
    // An independent in-place model skips inserted bytes, matching replacement
    // of nonoverlapping occurrences in the original input (including NUL bytes).
    std::mt19937 buffer_random(0x57444a52);
    const auto bytes = [&](std::size_t size) {
        std::string out(size, '\0');
        for (char& value : out)
            value = static_cast<char>(buffer_random() % 256);
        return out;
    };
    for (int trial = 0; trial < 10000; ++trial) {
        const auto input = bytes(buffer_random() % 128);
        auto old = bytes(1 + buffer_random() % 7);
        if (trial % 2 == 0 && !input.empty())
            old = input.substr(buffer_random() % input.size(), 1 + buffer_random() % 5);
        const auto replacement = bytes(buffer_random() % 10);
        auto expected = input;
        std::size_t position = 0;
        while ((position = expected.find(old, position)) != std::string::npos) {
            expected.replace(position, old.size(), replacement);
            position += replacement.size();
        }
        check(wd::replace(input, old, replacement, test_site) == expected,
              "random byte replacement matches independent in-place model");
        wd::List<std::string> parts;
        const auto separator = bytes(buffer_random() % 6);
        expected.clear();
        const auto count = buffer_random() % 9;
        for (std::uint32_t i = 0; i < count; ++i) {
            auto part = bytes(buffer_random() % 80);
            if (i)
                expected += separator;
            expected += part;
            parts.push(std::move(part), test_site);
        }
        const auto snapshot = parts;
        check(wd::join(parts, separator, test_site) == expected && parts == snapshot,
              "random byte join preserves input snapshots");
    }
    check(wd::replace("aaaaa", "aa", "", test_site) == "a", "deletion retains unmatched suffix");
    check(wd::replace("aaaaa", "aa", "aaa", test_site) == "aaaaaaa",
          "replacement containing pattern is not rescanned");
    check(wd::detail::checked_string_size(9, 1, 10, test_site) == 10,
          "string size accepts exact budget");
    fails([] { (void)wd::detail::checked_string_size(9, 2, 10, test_site); }, "R4003");
    fails([] { (void)wd::detail::checked_string_size(11, 0, 10, test_site); }, "R4003");
    fails([] { (void)wd::detail::checked_string_size(1, std::numeric_limits<std::size_t>::max(),
                                                   std::numeric_limits<std::size_t>::max(), test_site); },
          "R4003");
    fails([] { (void)wd::split("a", "", test_site); }, "R4009");
    fails([] { (void)wd::replace("a", "", "b", test_site); }, "R4009");
    check(wd::strip(" \t\r\nabc \t") == "abc" && wd::strip(" \n").empty(), "ASCII stripping");
    check(wd::find("abc", "b", test_site) == 1 && wd::find("abc", "d", test_site) == -1,
          "string find");
    check(wd::to_int(std::string("+42"), test_site) == 42, "signed string integer");
    check(wd::to_int(std::string("-9223372036854775808"), test_site) == wd::min_int,
          "minimum string integer");
    for (const auto& text : {"", " 2", "2 ", "2x", "2.0", "+", "+-1", "9223372036854775808"})
        fails([&] { (void)wd::to_int(std::string(text), test_site); }, "R4003");
    check(wd::to_float(std::string("-1.25e2"), test_site) == -125.0, "string float");
    check(wd::to_float(std::string("4.9406564584124654e-324"), test_site) > 0,
          "subnormal string float");
    for (const auto& text :
         {"", "nan", "inf", " 1", "1 ", "1e", ".5", "1.", "1e999", "1e-999", "1\n"})
        fails([&] { (void)wd::to_float(std::string(text), test_site); }, "R4003");
    fails([] { (void)wd::file_path(std::string("a\0b", 3), test_site); }, "R4008");
    check(wd::json_string(std::string("a\0b", 3)) == "\"a\\u0000b\"", "JSON embedded NUL");
    check(wd::json_string(std::string("\xff", 1)) == "\"\\ufffd\"",
          "JSON invalid UTF-8 replacement");
    check(wd::json_string("árbol") == "\"árbol\"", "JSON preserves Unicode");
    wd::Dict<Int, Int> mapping;
    std::map<Int, Int> model;
    std::vector<Int> order;
    std::uint64_t random = 1234567;
    for (int operation = 0; operation < 2000; ++operation) {
        random = random * 6364136223846793005ULL + 1;
        const auto key = static_cast<Int>((random >> 32) % 127);
        if (random & 1) {
            if (!model.contains(key))
                order.push_back(key);
            model[key] = operation;
            mapping.set(key, operation, test_site);
        } else {
            const auto removed = model.erase(key) != 0;
            check(mapping.remove(key) == removed, "dictionary remove matches reference");
            order.erase(std::remove(order.begin(), order.end(), key), order.end());
        }
        check(mapping.size() == model.size(), "dictionary size matches reference");
        bool contents_match = true;
        for (const auto& [entry, value] : model)
            if (!mapping.contains(entry) || mapping.get(entry, test_site) != value)
                contents_match = false;
        check(contents_match, "dictionary contents match reference");
        const auto snapshot_keys = wd::keys(mapping);
        bool order_matches = snapshot_keys.size() == order.size();
        for (std::size_t i = 0; i < order.size(); ++i)
            if (snapshot_keys.get(static_cast<Int>(i), test_site) != order[i])
                order_matches = false;
        check(order_matches, "dictionary insertion order matches reference");
        auto mapping_copy = mapping;
        mapping_copy.set(-1, 42, test_site);
        check(!mapping.contains(-1), "dictionary copy isolation");
    }
    fails([&] { (void)mapping.get(-2, test_site); }, "R4011");
    fails([&] { (void)mapping.mut_at(-2, test_site); }, "R4011");
    const auto json = wd::json_parse("{\"x\":[true,1,1.0,null]}", test_site);
    check(wd::json_parse(json.dump(), test_site) == json, "JSON exact kind round trip");
    const auto json_copy = json;
    check(&json.raw() == &json_copy.raw(), "JSON copies share immutable storage");
    fails([] { (void)wd::json_int(wd::Json(1.0), test_site); }, "R4012");
    fails([] { (void)wd::json_parse("{\"x\":1,\"x\":2}", test_site); }, "R4012");
    fails([] { (void)wd::to_json(std::numeric_limits<double>::infinity(), test_site); }, "R4012");
    fails([] { (void)wd::to_json(std::string("\xff", 1), test_site); }, "R4012");
    for (Int start : {wd::min_int, Int{-9}, Int{-1}, Int{0}, Int{2}, Int{3}, wd::max_int})
        for (Int stop : {wd::min_int, Int{-9}, Int{-1}, Int{0}, Int{2}, Int{3}, wd::max_int}) {
            const auto sliced = wd::slice(std::string("abc"), start, stop, test_site);
            const auto list_slice =
                wd::slice(wd::List<std::string>{"a", "b", "c"}, start, stop, test_site);
            check(wd::join(list_slice, "", test_site) == sliced,
                  "slice string/list boundary agreement");
        }
    std::cout << checks << " runtime checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
