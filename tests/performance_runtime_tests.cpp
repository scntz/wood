#include "native_memory.hpp"
#include "wood_runtime.hpp"
namespace {
template <typename T> concept CanBorrowTemporary = requires(T value) {
    std::move(value).get_ref(0, wd::Site{"temporary", 1, 1});
};
static_assert(!CanBorrowTemporary<wd::List<std::string>>);
constexpr wd::Site performance_site{"performance-runtime.wd", 7, 9};
int checks = 0, failures = 0;
void check(bool value) {
    ++checks;
    if (!value)
        ++failures;
}
} // namespace
int main() {
    {
        wd::List<std::string> source{std::string(256, 'a'), std::string(256, 'b')};
        const auto snapshot = source;
        const auto& first = snapshot.get_ref(0, performance_site);
        const auto& last = snapshot.get_ref(-1, performance_site);
        bench_reset();
        const auto length = wd::len(first, performance_site) + wd::len(last, performance_site);
        check(bench_stop() == 0);
        check(length == 512);
        source.mut_at(0, performance_site) = "changed";
        source.pop(performance_site);
        source.push("replacement", performance_site);
        check(first == std::string(256, 'a'));
        check(last == std::string(256, 'b'));
        check(snapshot.size() == 2 && source.get(0, performance_site) == "changed");
        try {
            (void)snapshot.get_ref(2, performance_site);
            check(false);
        } catch (const wd::Error& error) {
            check(error.code == "R4006" && error.site.line == 7 && error.site.column == 9);
        }
    }
    for (const auto count : {128, 129}) {
        wd::List<wd::Int> values;
        bench_reset();
        for (int i = 0; i < count; ++i)
            values.push(i, performance_site);
        const auto allocations = bench_stop();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        // Capacity is private and host-library-specific. Retain an allocation
        // budget that detects a new allocation on every append, not an exact
        // growth-factor contract. Exact native-policy metrics are profiled.
        check(allocations > 1 && allocations < count / 2);
#else
        static_cast<void>(allocations);
#endif
        const auto snapshot = values;
        values.push(-1, performance_site);
        values.mut_at(0, performance_site) = -2;
        check(snapshot.size() == static_cast<std::size_t>(count));
        check(snapshot.get(0, performance_site) == 0);
        check(snapshot.get(-1, performance_site) == count - 1);
        check(values.get(0, performance_site) == -2);
        check(values.get(-1, performance_site) == -1);
    }
    {
        // One owned long-string payload is required by the public cursor
        // protocol. Moves out of a final-use step must not add another copy.
        wd::List<std::string> input{std::string(256, 'p')};
        auto cursor = wd::iter(input);
        bench_reset();
        auto item = wd::next(cursor, performance_site);
        const auto value = wd::step_value(std::move(item), performance_site);
        const auto allocations = bench_stop();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        check(allocations == 1);
#else
        static_cast<void>(allocations);
#endif
        check(value == std::string(256, 'p'));
        input.mut_at(0, performance_site) = "changed";
        check(value == std::string(256, 'p'));
        check(!wd::has_item(wd::next(cursor, performance_site)));
        auto snapshot_cursor = wd::iter(wd::List<std::string>{value});
        auto snapshot = snapshot_cursor;
        check(wd::step_value(wd::next(snapshot_cursor, performance_site), performance_site) == value);
        check(wd::step_value(wd::next(snapshot, performance_site), performance_site) == value);
    }
    {
        wd::List<std::string> input;
        const std::string value(256, 'x');
        for (int i = 0; i < 48; ++i)
            input.push(value, performance_site);
        const auto snapshot = input;
        bench_reset();
        const auto joined = wd::join(input, ":::", performance_site);
        const auto count = bench_stop();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        check(count == 1);
#else
        static_cast<void>(count);
#endif
        check(joined.size() == 48 * value.size() + 47 * 3);
        check(input == snapshot);
        const auto copied = input.get(0, performance_site);
        input.mut_at(0, performance_site) = "changed";
        check(copied == value && snapshot.get(0, performance_site) == value);
        check(joined.starts_with(value));
    }
    for (const auto& replacement : {std::string("long"), std::string(""), std::string("a")}) {
        const std::string input(2048, 'a');
        bench_reset();
        const auto replaced = wd::replace(input, "a", replacement, performance_site);
        const auto count = bench_stop();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        check(count == (replacement.empty() ? 0 : 1));
#else
        static_cast<void>(count);
#endif
        check(replaced == wd::repeat(replacement, 2048, performance_site));
        check(input == std::string(2048, 'a'));
    }
    {
        const std::string input(2048, 'x');
        bench_reset();
        const auto unchanged = wd::replace(input, "absent", "replacement", performance_site);
        const auto count = bench_stop();
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        check(count == 1);
#else
        static_cast<void>(count);
#endif
        check(unchanged == input && unchanged.data() != input.data());
    }
    volatile bool valid = true;
    bench_reset();
    for (int i = 0; i < 1000; ++i)
        wd::assert_that(valid, performance_site);
    check(bench_stop() == 0);
    try {
        wd::assert_that(false, performance_site);
        check(false);
    } catch (const wd::Error& error) {
        check(error.code == "R4004" && error.message == "assertion failed" && error.site.line == 7);
    }
    for (const auto size : {0, 1, 15, 16, 127, 1024}) {
        std::string left(static_cast<std::size_t>(size), 'x');
        const std::string suffix("\0árbol", 7);
        left.reserve(left.size() + suffix.size() + 64);
        const auto* buffer = left.data();
        const auto expected = left + suffix;
        bench_reset();
        const auto result = wd::concat(std::move(left), suffix, performance_site);
        const auto allocations = bench_stop();
        check(result == expected);
        check(result.data() == buffer);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
        check(allocations == 0);
#else
        // MSVC's debug string move allocates an iterator proxy, not a data copy.
        static_cast<void>(allocations);
#endif
        check(suffix == std::string("\0árbol", 7));
    }
    const auto text = std::string(1024, 's');
    auto step = wd::yielded(text);
    const auto snapshot = step;
    const auto* buffer = step.value->data();
    bench_reset();
    const auto result = wd::step_value(std::move(step), performance_site);
    const auto allocations = bench_stop();
    check(result.data() == buffer);
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    check(allocations == 0);
#else
    static_cast<void>(allocations);
#endif
    check(result == text);
    check(wd::step_value(snapshot, performance_site) == text);
    const auto copied = wd::step_value(snapshot, performance_site);
    check(copied == text && *snapshot.value == text);
    try {
        (void)wd::step_value(wd::Step<std::string>{}, performance_site);
        check(false);
    } catch (const wd::Error& error) {
        check(error.code == "R4024" && error.site.line == 7 && error.site.column == 9);
    }
    std::cout << checks << " ownership runtime checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
