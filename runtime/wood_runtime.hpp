// Wood's small, portable native runtime. Embedded into each generated program.
#ifndef WOOD_NATIVE_RUNTIME_HPP
#define WOOD_NATIVE_RUNTIME_HPP
#ifndef WOOD_JSON_HPP
#include "../include/wood/json.hpp"
#endif
#ifndef WOOD_FILESYSTEM_HPP
#include "../include/wood/filesystem.hpp"
#endif
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <pthread.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace wd {
using Int = std::int64_t;
template <typename R, typename... Args> using Function = R (*)(Args...);
struct None {
    bool operator==(const None&) const = default;
};
template <typename T, typename U> bool equal(const T& left, const U& right) {
    return left == right;
}
template <typename T> bool equal(const std::optional<T>& value, None) {
    return !value.has_value();
}
template <typename T> bool equal(None, const std::optional<T>& value) {
    return !value.has_value();
}
static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
              "Wood requires IEEE 754 binary64 double");
struct Site {
    const char* file;
    std::size_t line;
    std::size_t column;
};
class Error : public std::runtime_error {
  public:
    std::string file;
    Site site;
    std::string code;
    std::string message;
    Error(Site at, std::string id, const std::string& message)
        : std::runtime_error(message), file(at.file), site{file.c_str(), at.line, at.column},
          code(std::move(id)), message(message) {}
    Error(const Error& other) : Error(other.site, other.code, other.message) {}
};
struct Failure {
    std::string code, message, file;
    Int line, column;
    bool operator==(const Failure&) const = default;
};
inline Failure failure(const Error& value) {
    return {value.code, value.message, value.file, static_cast<Int>(value.site.line),
            static_cast<Int>(value.site.column)};
}
[[noreturn]] inline void raise(const Failure& value, Site) {
    throw Error({value.file.c_str(), static_cast<std::size_t>(value.line),
                 static_cast<std::size_t>(value.column)},
                value.code, value.message);
}
[[noreturn]] inline void raise(const std::string& message, Site at) {
    throw Error(at, "R4010", message);
}
[[noreturn]] inline void error(Site site, const char* code, const std::string& message) {
    throw Error(site, code, message);
}
// error(code, message): a program's own code is 1 to 64 ASCII letters, digits, '.', '_' or '-' and starts
// with a letter. A capital letter and four digits is the shape of wood's own codes, so it is refused.
inline Failure make_error(const std::string& code, const std::string& message, Site at) {
    bool valid = !code.empty() && code.size() <= 64;
    for (std::size_t i = 0; valid && i < code.size(); ++i) {
        const char c = code[i];
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        valid = letter || (i > 0 && ((c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'));
    }
    if (!valid)
        error(at, "R4003",
              "error code must be 1 to 64 letters, digits, '.', '_' or '-', starting with a letter");
    bool reserved = code.size() == 5 && code[0] >= 'A' && code[0] <= 'Z';
    for (std::size_t i = 1; reserved && i < 5; ++i)
        reserved = code[i] >= '0' && code[i] <= '9';
    if (reserved)
        error(at, "R4003", "codes like R4010 are wood's own; use a name of your own, such as app.not_found");
    return {code, message, at.file, static_cast<Int>(at.line), static_cast<Int>(at.column)};
}
inline constexpr Int min_int = std::numeric_limits<Int>::min();
inline constexpr Int max_int = std::numeric_limits<Int>::max();
struct Exit {
    int code;
};
[[noreturn]] inline void exit_program(Int code, Site at) {
    if (code < 0 || code > 255)
        error(at, "R4003", "exit code must be in 0..255");
    throw Exit{static_cast<int>(code)};
}
inline Int add(Int a, Int b, Site site) {
    if ((b > 0 && a > max_int - b) || (b < 0 && a < min_int - b))
        error(site, "R4001", "integer overflow in addition");
    return a + b;
}
inline Int sub(Int a, Int b, Site site) {
    if ((b > 0 && a < min_int + b) || (b < 0 && a > max_int + b))
        error(site, "R4001", "integer overflow in subtraction");
    return a - b;
}
inline Int mul(Int a, Int b, Site site) {
    if ((a > 0 && ((b > 0 && a > max_int / b) || (b < 0 && b < min_int / a))) ||
        (a < 0 && ((b > 0 && a < min_int / b) || (b < 0 && a < max_int / b))))
        error(site, "R4001", "integer overflow in multiplication");
    return a * b;
}
inline Int neg(Int value, Site site) {
    if (value == min_int)
        error(site, "R4001", "integer overflow in negation");
    return -value;
}
inline void shift_count(Int count, Site site) {
    if (count < 0 || count > 63)
        error(site, "R4014", "shift count must be in 0..63");
}
inline Int shift_left(Int value, Int count, Site site) {
    shift_count(count, site);
    if (count == 63) {
        if (value == 0)
            return 0;
        if (value == -1)
            return min_int;
        error(site, "R4001", "integer overflow in left shift");
    }
    return mul(value, Int{1} << count, site);
}
inline Int shift_right(Int value, Int count, Site site) {
    shift_count(count, site);
    // C++20 and later specify arithmetic right shift, rounding toward -infinity.
    return value >> count;
}
inline Int floor_div(Int a, Int b, Site site) {
    if (b == 0)
        error(site, "R4002", "division by zero");
    if (a == min_int && b == -1)
        error(site, "R4001", "integer overflow in division");
    const Int q = a / b, r = a % b;
    return q - ((r != 0 && ((r < 0) != (b < 0))) ? 1 : 0);
}
inline Int mod(Int a, Int b, Site site) {
    if (b == 0)
        error(site, "R4002", "division by zero");
    if (a == min_int && b == -1)
        return 0;
    const Int r = a % b;
    return r != 0 && ((r < 0) != (b < 0)) ? r + b : r;
}
inline double div(double a, double b, Site site) {
    if (b == 0.0)
        error(site, "R4002", "division by zero");
    return a / b;
}
inline Int to_int(Int value, Site) {
    return value;
}
inline Int to_int(double value, Site site) {
    // Binary64 rounds INT64_MAX to 2^63; the upper bound must be exclusive.
    if (!std::isfinite(value) || value < -0x1p63 || value >= 0x1p63)
        error(site, "R4003", "float is outside the signed 64-bit range");
    return static_cast<Int>(value);
}
template <typename T> inline double to_float(T value, Site) {
    return static_cast<double>(value);
}
inline Int to_int(const std::string& value, Site site) {
    Int result = 0;
    std::string_view text(value);
    if (text.starts_with('+')) {
        text.remove_prefix(1);
        if (text.starts_with('-'))
            error(site, "R4003", "invalid decimal integer sign");
    }
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        error(site, "R4003", "invalid decimal integer or value outside signed 64-bit range");
    return result;
}
inline double to_float(const std::string& value, Site site) {
    // Accept the same finite decimal syntax as source literals, with an optional sign.
    std::size_t i = 0;
    if (i < value.size() && (value[i] == '+' || value[i] == '-'))
        ++i;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto start = i;
    while (i < value.size() && digit(value[i]))
        ++i;
    if (i == start)
        error(site, "R4003", "invalid finite decimal float");
    if (i < value.size() && value[i] == '.') {
        const auto fraction = ++i;
        while (i < value.size() && digit(value[i]))
            ++i;
        if (i == fraction)
            error(site, "R4003", "invalid finite decimal float");
    }
    const auto mantissa_end = i;
    if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
        ++i;
        if (i < value.size() && (value[i] == '+' || value[i] == '-'))
            ++i;
        const auto exponent = i;
        while (i < value.size() && digit(value[i]))
            ++i;
        if (i == exponent)
            error(site, "R4003", "invalid finite decimal float");
    }
    if (i != value.size())
        error(site, "R4003", "invalid finite decimal float");
    std::istringstream input(value);
    input.imbue(std::locale::classic());
    double result = 0;
    input >> std::noskipws >> result;
    const bool nonzero =
        value.substr(0, mantissa_end).find_first_of("123456789") != std::string::npos;
    if (input.fail() || !std::isfinite(result) || (result == 0.0 && nonzero))
        error(site, "R4003", "float is outside the finite binary64 range");
    return result;
}

inline std::size_t index_offset(Int index, std::size_t size, Site site) {
    if (size > static_cast<std::uint64_t>(max_int))
        error(site, "R4003", "collection is too large");
    const auto count = static_cast<Int>(size);
    if (index < 0) {
        if (index < -count)
            error(site, "R4006", "index out of bounds");
        index += count;
    }
    if (index >= count)
        error(site, "R4006", "index out of bounds");
    return static_cast<std::size_t>(index);
}
// Value semantics with copy-on-write storage. No references into elements escape
// generated mutation statements; iteration holds its own immutable snapshot.
template <typename T> class List {
    // Avoid vector<bool>'s proxy references, including in nested mutation paths.
    struct Slot {
        T value;
        bool operator==(const Slot&) const = default;
    };
    std::shared_ptr<std::vector<Slot>> storage_;
    void detach() {
        if (!storage_)
            storage_ = std::make_shared<std::vector<Slot>>();
        else if (storage_.use_count() != 1)
            storage_ = std::make_shared<std::vector<Slot>>(*storage_);
    }

  public:
    List() = default;
    List(std::initializer_list<T> values) : storage_(std::make_shared<std::vector<Slot>>()) {
        storage_->reserve(values.size());
        for (const auto& value : values)
            storage_->push_back(Slot{value});
    }
    std::size_t size() const {
        return storage_ ? storage_->size() : 0;
    }
    void reserve(std::size_t count, Site site) {
        if (count > static_cast<std::uint64_t>(max_int) || count > std::vector<Slot>{}.max_size())
            error(site, "R4003", "list is too large");
        if (count == 0)
            return;
        detach();
        storage_->reserve(count);
    }
    T get(Int index, Site site) const {
        return get_ref(index, site);
    }
    // Internal nonescaping reads only. The caller must retain this list and
    // must not mutate it while using the reference. Wood reads still use get().
    const T& get_ref(Int index, Site site) const & {
        const auto offset = index_offset(index, size(), site);
        return (*storage_)[offset].value;
    }
    const T& get_ref(Int, Site) const && = delete;
    T& mut_at(Int index, Site site) {
        const auto offset = index_offset(index, size(), site);
        detach();
        return (*storage_)[offset].value;
    }
    void push(T value, Site site) {
        if (size() >= static_cast<std::uint64_t>(max_int))
            error(site, "R4003", "list is too large");
        detach();
        // Retain the native vector's amortized growth policy. Explicit doubling
        // reduced requests but increased peak memory at capacity boundaries.
        storage_->push_back(Slot{std::move(value)});
    }
    T pop(Site site) {
        if (size() == 0)
            error(site, "R4007", "cannot pop an empty list");
        detach();
        T value = std::move(storage_->back().value);
        storage_->pop_back();
        return value;
    }
    bool operator==(const List& other) const {
        if (size() != other.size())
            return false;
        return size() == 0 || *storage_ == *other.storage_;
    }
};
template <typename T> inline T at(const List<T>& list, Int index, Site site) {
    return list.get(index, site);
}
inline std::string at(const std::string& text, Int index, Site site) {
    return std::string(1, text[index_offset(index, text.size(), site)]);
}
inline Int slice_bound(Int index, Int size) {
    if (index < 0)
        return index < -size ? 0 : size + index;
    return std::min(index, size);
}
struct SliceIndices {
    Int start, step;
    std::size_t count;
};
inline SliceIndices slice_indices(std::size_t length, std::optional<Int> first,
                                  std::optional<Int> last, Int step, Site site) {
    if (step == 0)
        error(site, "R4005", "slice step cannot be zero");
    if (length > static_cast<std::uint64_t>(max_int))
        error(site, "R4003", "sequence is too large");
    const auto size = static_cast<Int>(length);
    const bool reverse = step < 0;
    const auto bound = [=](Int index) {
        if (index < 0)
            return index < -size ? (reverse ? Int{-1} : Int{0}) : index + size;
        return index >= size ? (reverse ? size - 1 : size) : index;
    };
    const Int start = first ? bound(*first) : (reverse ? size - 1 : 0);
    const Int stop = last ? bound(*last) : (reverse ? -1 : size);
    std::uint64_t count = 0;
    if (!reverse && start < stop)
        count = 1 + static_cast<std::uint64_t>(stop - 1 - start) / static_cast<std::uint64_t>(step);
    else if (reverse && start > stop) {
        // Unsigned magnitude also represents abs(INT64_MIN), without negating it.
        const auto stride = std::uint64_t{0} - static_cast<std::uint64_t>(step);
        count = 1 + static_cast<std::uint64_t>(start - 1 - stop) / stride;
    }
    return {start, step, static_cast<std::size_t>(count)};
}
inline std::string slice(const std::string& value, std::optional<Int> start,
                         std::optional<Int> stop, Int step, Site site) {
    const auto indices = slice_indices(value.size(), start, stop, step, site);
    if (step == 1)
        return value.substr(static_cast<std::size_t>(indices.start), indices.count);
    std::string result;
    result.reserve(indices.count);
    auto index = indices.start;
    for (std::size_t i = 0; i < indices.count; ++i) {
        result.push_back(value[static_cast<std::size_t>(index)]);
        if (i + 1 < indices.count)
            index += step; // A subsequent selected index is necessarily in bounds.
    }
    return result;
}
template <typename T>
List<T> slice(const List<T>& value, std::optional<Int> start, std::optional<Int> stop, Int step,
              Site site) {
    const auto indices = slice_indices(value.size(), start, stop, step, site);
    if (step == 1 && indices.start == 0 && indices.count == value.size())
        return value;
    List<T> result;
    result.reserve(indices.count, site);
    auto index = indices.start;
    for (std::size_t i = 0; i < indices.count; ++i) {
        result.push(value.get(index, site), site);
        if (i + 1 < indices.count)
            index += step;
    }
    return result;
}
inline std::string slice(const std::string& value, Int start, Int stop, Site site) {
    return slice(value, start, stop, 1, site);
}
template <typename T> List<T> slice(const List<T>& value, Int start, Int stop, Site site) {
    return slice(value, start, stop, 1, site);
}
inline std::size_t sequence_size(std::size_t size, Int repeats, Site site) {
    if (repeats <= 0 || size == 0)
        return 0;
    if (size > static_cast<std::uint64_t>(max_int) / static_cast<std::uint64_t>(repeats))
        error(site, "R4003", "sequence is too large");
    const auto count = static_cast<std::uint64_t>(size) * static_cast<std::uint64_t>(repeats);
    if (count > std::numeric_limits<std::size_t>::max())
        error(site, "R4003", "sequence exceeds platform size limit");
    return static_cast<std::size_t>(count);
}
inline std::string concat(const std::string& a, const std::string& b, Site site) {
    if (a.size() > static_cast<std::uint64_t>(max_int) - b.size() ||
        a.size() > a.max_size() - b.size())
        error(site, "R4003", "string is too long");
    return a + b;
}
// Only owned/dead temporaries select this overload. A live source is passed as
// const&, preserving snapshots and the original value if another operand fails.
inline std::string concat(std::string&& a, const std::string& b, Site site) {
    if (a.size() > static_cast<std::uint64_t>(max_int) - b.size() ||
        a.size() > a.max_size() - b.size())
        error(site, "R4003", "string is too long");
    a.append(b);
    return std::move(a);
}
template <typename T> List<T> concat(const List<T>& a, const List<T>& b, Site site) {
    if (a.size() > static_cast<std::uint64_t>(max_int) - b.size())
        error(site, "R4003", "list is too large");
    List<T> result;
    result.reserve(a.size() + b.size(), site);
    for (const auto* part : {&a, &b})
        for (std::size_t i = 0; i < part->size(); ++i)
            result.push(part->get(static_cast<Int>(i), site), site);
    return result;
}
inline std::string repeat(const std::string& value, Int repeats, Site site) {
    const auto size = sequence_size(value.size(), repeats, site);
    std::string result;
    if (size > result.max_size())
        error(site, "R4003", "string is too long");
    result.reserve(size);
    if (size)
        for (Int i = 0; i < repeats; ++i)
            result += value;
    return result;
}
template <typename T> List<T> repeat(const List<T>& value, Int repeats, Site site) {
    const auto size = sequence_size(value.size(), repeats, site);
    List<T> result;
    result.reserve(size, site);
    if (size)
        for (Int i = 0; i < repeats; ++i)
            for (std::size_t j = 0; j < value.size(); ++j)
                result.push(value.get(static_cast<Int>(j), site), site);
    return result;
}
inline std::string repeat(Int repeats, const std::string& value, Site site) {
    return repeat(value, repeats, site);
}
template <typename T> List<T> repeat(Int repeats, const List<T>& value, Site site) {
    return repeat(value, repeats, site);
}
template <typename T> inline void append(List<T>& list, T value, Site site) {
    list.push(std::move(value), site);
}
template <typename T> inline T pop(List<T>& list, Site site) {
    return list.pop(site);
}
template <typename T> inline Int len(const List<T>& list, Site site) {
    if (list.size() > static_cast<std::uint64_t>(max_int))
        error(site, "R4003", "list is too large");
    return static_cast<Int>(list.size());
}
// Insertion-ordered values, with a tree index: predictable O(log n) lookup
// without exposing deterministic string-hash collision attacks. Erase is O(n).
// Copies share storage until mutation, including nested list/dictionary values.
template <typename K, typename V> class Dict {
    struct Storage {
        std::vector<std::pair<K, V>> entries;
        std::map<K, std::size_t> index;
    };
    std::shared_ptr<Storage> storage_;
    void detach() {
        if (!storage_)
            storage_ = std::make_shared<Storage>();
        else if (storage_.use_count() != 1)
            storage_ = std::make_shared<Storage>(*storage_);
    }

  public:
    std::size_t size() const {
        return storage_ ? storage_->entries.size() : 0;
    }
    bool contains(const K& key) const {
        return storage_ && storage_->index.contains(key);
    }
    V get(const K& key, Site site) const {
        if (!contains(key))
            error(site, "R4011", "dictionary key is absent");
        return storage_->entries[storage_->index.at(key)].second;
    }
    V get_or(const K& key, V fallback) const {
        if (!contains(key))
            return fallback;
        return storage_->entries[storage_->index.at(key)].second;
    }
    V& mut_at(const K& key, Site site) {
        if (!contains(key))
            error(site, "R4011", "dictionary key is absent");
        detach();
        return storage_->entries[storage_->index.at(key)].second;
    }
    void set(K key, V value, Site site) {
        detach();
        const auto found = storage_->index.find(key);
        if (found != storage_->index.end()) {
            storage_->entries[found->second].second = std::move(value);
            return;
        }
        if (size() >= static_cast<std::uint64_t>(max_int))
            error(site, "R4003", "dictionary is too large");
        const auto inserted = storage_->index.emplace(key, size());
        try {
            storage_->entries.emplace_back(std::move(key), std::move(value));
        } catch (...) {
            storage_->index.erase(inserted.first);
            throw;
        }
    }
    bool remove(const K& key) {
        if (!contains(key))
            return false;
        detach();
        const auto index = storage_->index.at(key);
        storage_->index.erase(key);
        storage_->entries.erase(storage_->entries.begin() + static_cast<std::ptrdiff_t>(index));
        for (auto& [stored, offset] : storage_->index) {
            (void)stored;
            if (offset > index)
                --offset;
        }
        return true;
    }
    const std::vector<std::pair<K, V>>& entries() const {
        static const std::vector<std::pair<K, V>> empty;
        return storage_ ? storage_->entries : empty;
    }
    bool operator==(const Dict& other) const {
        if (size() != other.size())
            return false;
        for (const auto& [key, value] : entries()) {
            if (!other.contains(key) || !(value == other.get(key, Site{"<runtime>", 0, 0})))
                return false;
        }
        return true;
    }
};
template <typename K, typename V> V at(const Dict<K, V>& dict, const K& key, Site site) {
    return dict.get(key, site);
}
template <typename K, typename V> Int len(const Dict<K, V>& dict, Site) {
    return static_cast<Int>(dict.size());
}
template <typename K, typename V> List<K> keys(const Dict<K, V>& dict) {
    List<K> result;
    for (const auto& [key, value] : dict.entries()) {
        (void)value;
        result.push(key, Site{"<runtime>", 0, 0});
    }
    return result;
}
template <typename K, typename V> List<V> values(const Dict<K, V>& dict) {
    List<V> result;
    for (const auto& [key, value] : dict.entries()) {
        (void)key;
        result.push(value, Site{"<runtime>", 0, 0});
    }
    return result;
}
template <typename K, typename V> V get(const Dict<K, V>& dict, const K& key, V fallback, Site) {
    return dict.get_or(key, std::move(fallback));
}
template <typename K, typename V> bool remove(Dict<K, V>& dict, const K& key, Site) {
    return dict.remove(key);
}
template <typename K, typename V> bool contains(const Dict<K, V>& dict, const K& key, Site) {
    return dict.contains(key);
}
template <typename T, typename U> bool contains(const List<T>& list, const U& value, Site site) {
    for (std::size_t i = 0; i < list.size(); ++i)
        if (equal(list.get(static_cast<Int>(i), site), value))
            return true;
    return false;
}
inline bool contains(const std::string& text, const std::string& value, Site) {
    return text.find(value) != std::string::npos;
}
template <typename K, typename V> std::string to_str(const Dict<K, V>& dict);

using Json = wood::Json;
inline void json_depth(std::size_t depth, Site site) {
    if (depth > 64)
        error(site, "R4012", "JSON nesting exceeds 64 levels");
}
inline void json_validate(const Json& value, std::size_t depth, Site site) {
    json_depth(depth, site);
    if (const auto* array = std::get_if<Json::Array>(&value.raw()))
        for (const auto& child : *array)
            json_validate(child, depth + 1, site);
    if (const auto* object = std::get_if<Json::Object>(&value.raw()))
        for (const auto& [key, child] : *object) {
            (void)key;
            json_validate(child, depth + 1, site);
        }
}
inline Json to_json(const Json& value, Site site, std::size_t depth = 0) {
    if (depth)
        json_validate(value, depth, site);
    return value;
}
inline Json to_json(Int value, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    return Json(value);
}
inline Json to_json(bool value, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    return Json(value);
}
inline Json to_json(double value, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    if (!std::isfinite(value))
        error(site, "R4012", "JSON numbers must be finite");
    return Json(value);
}
inline Json to_json(const std::string& value, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    for (std::size_t i = 0; i < value.size();) {
        auto length = wood::json_detail::utf8_length(value, i);
        if (!length)
            error(site, "R4012", "JSON strings must contain valid UTF-8");
        i += length;
    }
    return Json(value);
}
inline Json to_json(const Failure& value, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    return Json(Json::Object{{"code", to_json(value.code, site, depth + 1)},
                             {"message", to_json(value.message, site, depth + 1)},
                             {"file", to_json(value.file, site, depth + 1)},
                             {"line", to_json(value.line, site, depth + 1)},
                             {"column", to_json(value.column, site, depth + 1)}});
}
inline Json to_json(None, Site site, std::size_t depth = 0) {
    json_depth(depth, site);
    return Json();
}
template <typename T> Json to_json(const std::optional<T>& value, Site site, std::size_t depth = 0);
template <typename... T>
Json to_json(const std::tuple<T...>& value, Site site, std::size_t depth = 0);
template <typename T> Json to_json(const List<T>& value, Site site, std::size_t depth = 0);
template <typename V>
Json to_json(const Dict<std::string, V>& value, Site site, std::size_t depth = 0);
template <typename T> Json to_json(const std::optional<T>& value, Site site, std::size_t depth) {
    json_depth(depth, site);
    return value ? to_json(*value, site, depth) : Json();
}
template <typename... T> Json to_json(const std::tuple<T...>& value, Site site, std::size_t depth) {
    json_depth(depth, site);
    Json::Array items;
    items.reserve(sizeof...(T));
    std::apply([&](const auto&... item) { (items.push_back(to_json(item, site, depth + 1)), ...); },
               value);
    return Json(std::move(items));
}
template <typename T> Json to_json(const List<T>& value, Site site, std::size_t depth) {
    json_depth(depth, site);
    Json::Array items;
    items.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i)
        items.push_back(to_json(value.get(static_cast<Int>(i), site), site, depth + 1));
    return Json(std::move(items));
}
template <typename V>
Json to_json(const Dict<std::string, V>& value, Site site, std::size_t depth) {
    json_depth(depth, site);
    Json::Object items;
    for (const auto& [key, child] : value.entries()) {
        (void)to_json(key, site, depth + 1);
        items.emplace(key, to_json(child, site, depth + 1));
    }
    return Json(std::move(items));
}
inline Json json_parse(const std::string& text, Site site) {
    if (text.size() > 16 * 1024 * 1024)
        error(site, "R4012", "JSON input exceeds 16 MiB");
    try {
        return Json::parse(text);
    } catch (const std::runtime_error& failure) {
        error(site, "R4012", failure.what());
    }
}
template <typename T> std::string json_dump(const T& value, Site site) {
    return to_json(value, site).dump();
}
inline Json json_null() {
    return Json();
}
inline std::string json_kind(const Json& value) {
    static constexpr const char* names[] = {"null", "bool", "int", "float", "str", "list", "dict"};
    return names[value.raw().index()];
}
inline Int json_int(const Json& value, Site site) {
    if (const auto* result = std::get_if<Int>(&value.raw()))
        return *result;
    error(site, "R4012", "expected JSON int");
}
inline double json_float(const Json& value, Site site) {
    if (const auto* result = std::get_if<double>(&value.raw()))
        return *result;
    if (const auto* result = std::get_if<Int>(&value.raw()))
        return static_cast<double>(*result);
    error(site, "R4012", "expected JSON number");
}
inline bool json_bool(const Json& value, Site site) {
    if (const auto* result = std::get_if<bool>(&value.raw()))
        return *result;
    error(site, "R4012", "expected JSON bool");
}
inline std::string json_str(const Json& value, Site site) {
    if (const auto* result = std::get_if<std::string>(&value.raw()))
        return *result;
    error(site, "R4012", "expected JSON str");
}
inline List<Json> json_list(const Json& value, Site site) {
    if (const auto* result = std::get_if<Json::Array>(&value.raw())) {
        List<Json> out;
        for (const auto& item : *result)
            out.push(item, site);
        return out;
    }
    error(site, "R4012", "expected JSON list");
}
inline Dict<std::string, Json> json_dict(const Json& value, Site site) {
    if (const auto* result = std::get_if<Json::Object>(&value.raw())) {
        Dict<std::string, Json> out;
        for (const auto& [key, child] : *result)
            out.set(key, child, site);
        return out;
    }
    error(site, "R4012", "expected JSON dict");
}
inline std::string to_str(const Json& value) {
    return value.dump();
}

inline std::string to_str(const std::string& value) {
    return value;
}
inline std::string to_str(const Failure& value) {
    return value.code + ": " + value.message;
}
template <typename R, typename... Args> std::string to_str(R (*)(Args...)) {
    return "<function>";
}
inline std::string to_str(bool value) {
    return value ? "true" : "false";
}
inline std::string to_str(Int value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return {buffer, result.ptr};
}
inline std::string to_str(double value) {
    char buffer[128];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{})
        throw std::runtime_error("float formatting failed");
    std::string text(buffer, result.ptr);
    if (std::isfinite(value) && text.find_first_of(".eE") == std::string::npos)
        text += ".0";
    return text;
}
template <typename T> std::string to_str(const List<T>& list);
template <typename K, typename V> std::string to_str(const Dict<K, V>& dict);
template <typename T> std::string to_str(const std::optional<T>& value);
template <typename... T> std::string to_str(const std::tuple<T...>& value);
inline std::string to_str(None) {
    return "None";
}
inline std::string repr(const std::string& value) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case 0:
            out += "\\0";
            break;
        default:
            if (c < 32 || c == 127) {
                out += "\\x";
                out += hex[c >> 4];
                out += hex[c & 15];
            } else
                out += static_cast<char>(c);
        }
    }
    return out + '"';
}
template <typename T> inline std::string repr(const T& value) {
    return to_str(value);
}
template <typename T> std::string repr(const std::optional<T>& value) {
    return value ? repr(*value) : "None";
}
template <typename T> std::string to_str(const std::optional<T>& value) {
    return value ? to_str(*value) : "None";
}
template <typename... T> std::string to_str(const std::tuple<T...>& value) {
    std::string result = "(";
    std::size_t index = 0;
    std::apply([&](const auto&... item) { ((result += (index++ ? ", " : "") + repr(item)), ...); },
               value);
    if constexpr (sizeof...(T) == 1)
        result += ',';
    return result + ')';
}
template <typename... T> Int len(const std::tuple<T...>&, Site) {
    return sizeof...(T);
}
template <typename T> T unwrap(const std::optional<T>& value, Site site) {
    if (!value)
        error(site, "R4023", "cannot unwrap None");
    return *value;
}
template <typename T> std::string to_str(const List<T>& list) {
    std::string result = "[";
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i)
            result += ", ";
        result += repr(list.get(static_cast<Int>(i), Site{"<runtime>", 0, 0}));
    }
    return result + ']';
}
template <typename K, typename V> std::string to_str(const Dict<K, V>& dict) {
    std::string out = "{";
    for (const auto& [key, value] : dict.entries()) {
        if (out.size() > 1)
            out += ", ";
        out += repr(key) + ": " + repr(value);
    }
    return out + '}';
}
inline Int len(const std::string& value, Site site) {
    if (value.size() > static_cast<std::uint64_t>(max_int))
        error(site, "R4003", "string is too long");
    return static_cast<Int>(value.size());
}
inline Int byte_value(const std::string& value, Site site) {
    if (value.size() != 1)
        error(site, "R4013", "byte_value requires exactly one byte");
    return static_cast<unsigned char>(value[0]);
}
inline std::string byte_string(Int value, Site site) {
    if (value < 0 || value > 255)
        error(site, "R4013", "byte_string requires an integer in 0..255");
    return std::string(1, static_cast<char>(static_cast<unsigned char>(value)));
}
template <typename... Args> inline void print_to(std::ostream& stream, const Args&... args) {
    std::size_t count = 0;
    const auto item = [&](const auto& value) {
        if (count++ > 0)
            stream << ' ';
        stream << to_str(value);
    };
    (item(args), ...);
    stream << '\n';
    if (!stream)
        throw std::runtime_error("could not write to output stream");
}
template <typename... Args> inline void print(const Args&... args) {
    print_to(std::cout, args...);
}
template <typename... Args> inline void eprint(const Args&... args) {
    print_to(std::cerr, args...);
}
// Block SIGPIPE only on this thread while flushing a public output operation.
// Consume only a newly generated broken-pipe signal; preserve the caller's mask.
class OutputSignalScope {
#ifndef _WIN32
    sigset_t previous{}, blocked{};
    bool active = false, pending = false;
#endif
  public:
    bool broken = false;
    explicit OutputSignalScope(Site at) {
#ifndef _WIN32
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0)
            error(at, "R4008", "could not write to output stream");
        active = true;
        sigset_t signals{};
        if (sigpending(&signals) == 0)
            pending = sigismember(&signals, SIGPIPE) == 1;
        else {
            pthread_sigmask(SIG_SETMASK, &previous, nullptr);
            active = false;
            error(at, "R4008", "could not write to output stream");
        }
#else
        (void)at;
#endif
    }
    ~OutputSignalScope() {
#ifndef _WIN32
        if (active) {
            if (broken && !pending) {
                timespec zero{};
                while (sigtimedwait(&blocked, nullptr, &zero) < 0 && errno == EINTR) {}
            }
            pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        }
#endif
    }
};
inline void output_bytes(int fd, std::string_view text, Site at, OutputSignalScope& signals) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        auto size = std::min<std::size_t>(text.size() - offset, 2147483647);
#ifdef _WIN32
        const auto lf = text.find('\n', offset);
        const bool newline = lf == offset;
        const char* data = text.data() + offset;
        if (newline) { data = "\r\n"; size = 2; }
        else if (lf != std::string_view::npos) size = std::min(size, lf - offset);
        std::size_t sent = 0;
        const HANDLE handle = GetStdHandle(fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
        while (sent < size) {
            DWORD written = 0;
            if (!WriteFile(handle, data + sent, static_cast<DWORD>(size - sent), &written, nullptr) ||
                written == 0 || written > size - sent)
                error(at, "R4008", "could not write to output stream");
            sent += written;
        }
        offset += newline ? 1 : size;
        (void)signals;
#else
        const auto count = ::write(fd, text.data() + offset, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0 || static_cast<std::size_t>(count) > size) {
            signals.broken = count < 0 && errno == EPIPE;
            error(at, "R4008", "could not write to output stream");
        }
        offset += static_cast<std::size_t>(count);
#endif
    }
}
template <typename... Args>
inline void output_checked(int fd, Site at, const Args&... args) {
    OutputSignalScope signals(at);
    std::size_t count = 0;
    const auto item = [&](const auto& value) {
        if (count++ > 0) output_bytes(fd, " ", at, signals);
        output_bytes(fd, to_str(value), at, signals);
    };
    (item(args), ...);
    output_bytes(fd, "\n", at, signals);
}
template <typename... Args> inline void print_checked(Site at, const Args&... args) {
    output_checked(1, at, args...);
}
template <typename... Args> inline void eprint_checked(Site at, const Args&... args) {
    output_checked(2, at, args...);
}
inline void assert_that(bool condition, Site site, const std::string& message) {
    if (!condition)
        error(site, "R4004", message);
}
inline void assert_that(bool condition, Site site) {
    if (!condition)
        error(site, "R4004", "assertion failed");
}
// All location-bearing builtins receive the location last.
inline void assert_that(bool condition, const std::string& message, Site site) {
    assert_that(condition, site, message);
}

// Characters are strict UTF-8 scalar values: no overlong forms, surrogates or values past U+10FFFF.
// Returns the byte width of the character at offset, or 0 when the bytes there are not valid UTF-8.
inline std::size_t character_width(const std::string& text, std::size_t at) {
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const auto first = byte(at);
    const std::size_t width = first < 0x80                    ? 1
                              : first >= 0xC2 && first <= 0xDF ? 2
                              : first >= 0xE0 && first <= 0xEF ? 3
                              : first >= 0xF0 && first <= 0xF4 ? 4
                                                               : 0;
    if (width == 0 || width > text.size() - at)
        return 0;
    for (std::size_t i = 1; i < width; ++i)
        if ((byte(at + i) & 0xC0) != 0x80)
            return 0;
    if (width > 1) {
        const auto second = byte(at + 1);
        if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
            (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F))
            return 0;
    }
    return width;
}
// Byte offsets where each character starts, plus the end of the text.
inline std::vector<std::size_t> character_offsets(const std::string& text, Site site) {
    std::vector<std::size_t> offsets;
    std::size_t at = 0;
    while (at < text.size()) {
        offsets.push_back(at);
        const auto width = character_width(text, at);
        if (width == 0)
            error(site, "R4013", "text is not valid UTF-8");
        at += width;
    }
    offsets.push_back(at);
    return offsets;
}
inline Int char_count(const std::string& text, Site site) {
    Int count = 0;
    for (std::size_t at = 0; at < text.size(); ++count) {
        const auto width = character_width(text, at);
        if (width == 0)
            error(site, "R4013", "text is not valid UTF-8");
        at += width;
    }
    return count;
}
inline List<std::string> chars(const std::string& text, Site site) {
    const auto offsets = character_offsets(text, site);
    List<std::string> result;
    result.reserve(offsets.size() - 1, site);
    for (std::size_t i = 0; i + 1 < offsets.size(); ++i)
        result.push(text.substr(offsets[i], offsets[i + 1] - offsets[i]), site);
    return result;
}
// Characters start..stop with the bounds of slice: negative counts from the end, then clamped.
inline std::string char_slice(const std::string& text, Int start, Int stop, Site site) {
    const auto offsets = character_offsets(text, site);
    const auto indices = slice_indices(offsets.size() - 1, start, stop, 1, site);
    const auto first = offsets[static_cast<std::size_t>(indices.start)];
    return text.substr(first, offsets[static_cast<std::size_t>(indices.start) + indices.count] - first);
}

inline List<std::string> split(const std::string& text, const std::string& separator, Site site) {
    if (separator.empty())
        error(site, "R4009", "split separator cannot be empty");
    List<std::string> result;
    std::size_t start = 0;
    for (;;) {
        const auto end = text.find(separator, start);
        result.push(text.substr(start, end == std::string::npos ? end : end - start), site);
        if (end == std::string::npos)
            return result;
        start = end + separator.size();
    }
}
namespace detail {
inline std::size_t checked_string_size(std::size_t size, std::size_t extra,
                                       std::size_t limit, Site site) {
    if (size > limit || extra > limit - size)
        error(site, "R4003", "string is too long");
    return size + extra;
}
} // namespace detail
inline std::string join(const List<std::string>& items, const std::string& separator, Site site) {
    std::string result;
    const auto limit = static_cast<std::size_t>(
        std::min(static_cast<std::uint64_t>(max_int),
                 static_cast<std::uint64_t>(result.max_size())));
    std::size_t size = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i)
            size = detail::checked_string_size(size, separator.size(), limit, site);
        size = detail::checked_string_size(
            size, items.get_ref(static_cast<Int>(i), site).size(), limit, site);
    }
    result.reserve(size);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i)
            result += separator;
        result += items.get_ref(static_cast<Int>(i), site);
    }
    return result;
}
inline std::string strip(const std::string& text) {
    constexpr auto whitespace = " \t\n\r\f\v";
    const auto start = text.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return {};
    return text.substr(start, text.find_last_not_of(whitespace) - start + 1);
}
inline Int find(const std::string& text, const std::string& needle, Site site) {
    const auto offset = text.find(needle);
    if (offset == std::string::npos)
        return -1;
    if (offset > static_cast<std::uint64_t>(max_int))
        error(site, "R4003", "string offset exceeds int range");
    return static_cast<Int>(offset);
}
inline bool starts_with(const std::string& text, const std::string& prefix) {
    return text.starts_with(prefix);
}
inline bool ends_with(const std::string& text, const std::string& suffix) {
    return text.ends_with(suffix);
}
inline std::string replace(const std::string& text, const std::string& old,
                           const std::string& replacement, Site site) {
    if (old.empty())
        error(site, "R4009", "replacement pattern cannot be empty");
    const auto first = text.find(old);
    if (first == std::string::npos || old == replacement)
        return text;
    std::string result;
    const auto limit = static_cast<std::size_t>(
        std::min(static_cast<std::uint64_t>(max_int),
                 static_cast<std::uint64_t>(result.max_size())));
    std::size_t size = text.size();
    for (auto end = first; end != std::string::npos; end = text.find(old, end + old.size())) {
        if (replacement.size() > old.size())
            size = detail::checked_string_size(size, replacement.size() - old.size(), limit, site);
        else
            size -= old.size() - replacement.size();
    }
    result.reserve(size);
    std::size_t start = 0;
    for (auto end = first; end != std::string::npos; end = text.find(old, start)) {
        result.append(text, start, end - start);
        result.append(replacement);
        start = end + old.size();
    }
    result.append(text, start, std::string::npos);
    return result;
}
inline std::filesystem::path file_path(const std::string& path, Site site) {
    if (path.find('\0') != std::string::npos)
        error(site, "R4008", "path contains a NUL byte");
    try {
        return std::filesystem::path(std::u8string(path.begin(), path.end()));
    } catch (const std::filesystem::filesystem_error&) {
        error(site, "R4008", "invalid filesystem path");
    }
}
inline std::string read_text(const std::string& path, Site site) {
    std::ifstream input(file_path(path, site), std::ios::binary);
    if (!input)
        error(site, "R4008", "cannot open file for reading: " + path);
    std::string content;
    char buffer[8192];
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0)
        content.append(buffer, static_cast<std::size_t>(input.gcount()));
    if (!input.eof())
        error(site, "R4008", "cannot read file: " + path);
    return content;
}
inline void write_text(const std::string& path, const std::string& content, Site site) {
    if (content.size() > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        error(site, "R4008", "content exceeds stream size limit");
    std::ofstream output(file_path(path, site), std::ios::binary | std::ios::trunc);
    if (!output)
        error(site, "R4008", "cannot open file for writing: " + path);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output)
        error(site, "R4008", "cannot write file: " + path);
}
inline bool path_exists(const std::string& path, Site site) {
    std::error_code failure;
    const bool exists = std::filesystem::exists(file_path(path, site), failure);
    if (failure)
        error(site, "R4008", "cannot query filesystem path: " + path);
    return exists;
}
inline std::string path_string(const std::filesystem::path& path, Site site) {
    try {
        const auto bytes = path.u8string();
        return std::string(bytes.begin(), bytes.end());
    } catch (const std::filesystem::filesystem_error&) {
        error(site, "R4008", "cannot encode filesystem path as UTF-8");
    }
}
inline std::string path_join(const std::string& base, const std::string& child, Site site) {
    return path_string(file_path(base, site) / file_path(child, site), site);
}
inline std::string path_name(const std::string& path, Site site) {
    return path_string(file_path(path, site).filename(), site);
}
inline std::string path_parent(const std::string& path, Site site) {
    return path_string(file_path(path, site).parent_path(), site);
}
inline std::string path_normalize(const std::string& path, Site site) {
    return path_string(file_path(path, site).lexically_normal(), site);
}
inline std::string path_absolute(const std::string& path, Site site) {
    std::error_code failure;
    const auto result = std::filesystem::absolute(file_path(path, site), failure);
    if (failure)
        error(site, "R4008", "cannot make path absolute: " + path);
    return path_string(result, site);
}
inline std::string path_resolve(const std::string& path, bool allow_missing, Site site) {
    try {
        return wood::filesystem_support::text(
            wood::filesystem_support::resolve_path(std::filesystem::absolute(file_path(path, site)), allow_missing));
    } catch (const std::exception& failure) {
        error(site, "R4008", failure.what());
    }
}
inline bool path_is_absolute(const std::string& path, Site site) {
    return file_path(path, site).is_absolute();
}
inline std::string path_identity(const std::string& path, Site site) {
    try {
        return wood::filesystem_support::identity(file_path(path, site));
    } catch (const std::exception& failure) {
        error(site, "R4008", failure.what());
    }
}
inline bool path_within(const std::string& path, const std::string& root, Site site) {
    try {
        return wood::filesystem_support::contained(std::filesystem::absolute(file_path(path, site)),
                                                  std::filesystem::absolute(file_path(root, site)));
    } catch (const std::exception& failure) {
        error(site, "R4008", failure.what());
    }
}
inline std::string fs_kind(const std::string& path, Site site) {
    std::error_code failure;
    const auto status = std::filesystem::symlink_status(file_path(path, site), failure);
    if (failure == std::errc::no_such_file_or_directory || failure == std::errc::not_a_directory)
        return "missing";
    if (failure)
        error(site, "R4008", "cannot query path kind: " + path);
    if (!std::filesystem::exists(status))
        return "missing";
    // MSVC does not classify directory junctions as symlinks. Share the
    // compiler's fail-closed reparse policy with Wood traversal/trust code.
    bool link = std::filesystem::is_symlink(status);
    try {
        link = link || wood::filesystem_support::is_link(file_path(path, site));
    } catch (const std::exception& failure) {
        error(site, "R4008", failure.what());
    }
    if (link)
        return "symlink";
    if (std::filesystem::is_regular_file(status))
        return "file";
    if (std::filesystem::is_directory(status))
        return "directory";
    return "other";
}
inline List<std::string> fs_list(const std::string& path, Int limit, Site site) {
    if (limit < 0)
        error(site, "R4008", "directory entry limit must be nonnegative");
    std::error_code failure;
    std::filesystem::directory_iterator current(file_path(path, site), failure), end;
    if (failure)
        error(site, "R4008", "cannot list directory: " + path);
    std::vector<std::string> names;
    for (; current != end; current.increment(failure)) {
        if (failure)
            error(site, "R4008", "cannot read directory: " + path);
        if (names.size() >= static_cast<std::uint64_t>(limit))
            error(site, "R4008", "directory entry limit exceeded: " + path);
        names.push_back(path_string(current->path().filename(), site));
    }
    if (failure)
        error(site, "R4008", "cannot read directory: " + path);
    std::sort(names.begin(), names.end());
    List<std::string> result;
    for (auto& name : names)
        append(result, std::move(name), site);
    return result;
}
inline Int fs_size(const std::string& path, Site site) {
    std::error_code failure;
    const auto native = file_path(path, site);
    if (!std::filesystem::is_regular_file(native, failure) || failure)
        error(site, "R4008", "file size requires a regular file: " + path);
    const auto result = std::filesystem::file_size(native, failure);
    if (failure)
        error(site, "R4008", "cannot read file size: " + path);
    if (result > static_cast<std::uintmax_t>(max_int))
        error(site, "R4008", "file size exceeds int range: " + path);
    return static_cast<Int>(result);
}
inline bool fs_mkdirs(const std::string& path, Site site) {
    std::error_code failure;
    const bool result = std::filesystem::create_directories(file_path(path, site), failure);
    if (failure)
        error(site, "R4008", "cannot create directories: " + path);
    return result;
}
inline bool fs_remove(const std::string& path, Site site) {
    std::error_code failure;
    const bool result = std::filesystem::remove(file_path(path, site), failure);
    if (failure)
        error(site, "R4008", "cannot remove path: " + path);
    return result;
}
// Clock reads share a process-local origin, initialized before the first read.
// Do not use high_resolution_clock: it is not required to be monotonic.
inline std::chrono::steady_clock::duration time_elapsed() {
    static const auto origin = std::chrono::steady_clock::now();
    return std::chrono::steady_clock::now() - origin;
}
inline double time_monotonic() {
    return std::chrono::duration<double>(time_elapsed()).count();
}
inline Int time_counter_ns(std::chrono::steady_clock::duration elapsed, Site site) {
    // Split before scaling to avoid overflowing a large whole duration. Keep
    // this path integer-only so long-running processes retain nanosecond units.
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    const auto fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed - seconds);
    constexpr Int billion = 1000000000;
    if (elapsed < std::chrono::steady_clock::duration::zero() ||
        seconds.count() > max_int / billion ||
        (seconds.count() == max_int / billion && fraction.count() > max_int % billion))
        error(site, "R4015", "monotonic counter exceeds the int64 nanosecond range");
    return static_cast<Int>(seconds.count()) * billion + static_cast<Int>(fraction.count());
}
inline Int time_monotonic_ns(Site site) {
    return time_counter_ns(time_elapsed(), site);
}
inline std::chrono::nanoseconds time_sleep_duration(double seconds, Site site) {
    if (!std::isfinite(seconds) || seconds < 0.0 || seconds > 86400.0)
        error(site, "R4015", "sleep requires finite seconds in 0..86400");
    // A positive fraction of a nanosecond must not be truncated to zero.
    return std::chrono::ceil<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds));
}
inline void time_sleep(double seconds, Site site) {
    const auto duration = time_sleep_duration(seconds, site);
    if (duration == std::chrono::nanoseconds::zero())
        return;
    try {
        std::this_thread::sleep_for(duration);
    } catch (const std::system_error& failure) {
        error(site, "R4015", std::string("cannot sleep: ") + failure.what());
    }
}
inline List<std::string> program_arguments;
inline std::optional<std::string> env_get(const std::string& name, Site site) {
    if (name.empty())
        error(site, "R4003", "environment variable name must not be empty");
    if (name.find('\0') != std::string::npos || name.find('=') != std::string::npos)
        error(site, "R4003", "environment variable name must not contain NUL or '='");
#ifdef _WIN32
    if (name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        error(site, "R4003", "environment variable name must be valid UTF-8 on Windows");
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                                          static_cast<int>(name.size()), nullptr, 0);
    if (!length)
        error(site, "R4003", "environment variable name must be valid UTF-8 on Windows");
    std::wstring key(static_cast<std::size_t>(length), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(),
                             static_cast<int>(name.size()), key.data(), length))
        error(site, "R4008", "cannot decode environment variable name");
    SetLastError(0);
    const auto count = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    const auto first_error = GetLastError();
    if (!count) {
        if (first_error == ERROR_ENVVAR_NOT_FOUND)
            return std::nullopt;
        if (first_error)
            error(site, "R4008", "cannot read environment variable");
        return std::string{};
    }
    std::wstring value(count, L'\0');
    SetLastError(0);
    const auto n = GetEnvironmentVariableW(key.c_str(), value.data(), count);
    if (n >= count || (!n && GetLastError()))
        error(site, "R4008", "environment changed or failed while reading");
    const auto bytes = n ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                               static_cast<int>(n), nullptr, 0, nullptr, nullptr) : 0;
    if (n && !bytes)
        error(site, "R4008", "environment variable value is not valid Unicode");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (bytes && !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                     static_cast<int>(n), result.data(), bytes, nullptr, nullptr))
        error(site, "R4008", "cannot encode environment variable value as UTF-8");
    return result;
#else
    const auto* value = std::getenv(name.c_str());
    return value ? std::optional<std::string>{value} : std::nullopt;
#endif
}
inline List<std::string> args() {
    return program_arguments;
}
#ifdef _WIN32
inline std::string utf8(const wchar_t* text) {
    const auto size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (!size)
        throw std::runtime_error("invalid Unicode program argument");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr,
                        nullptr);
    result.pop_back();
    return result;
}
inline void init_args(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i)
        program_arguments.push(utf8(argv[i]), Site{"<arguments>", 0, 0});
}
#else
inline void init_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        program_arguments.push(argv[i], Site{"<arguments>", 0, 0});
}
#endif
inline bool eof(Site site) {
    const bool ended = std::cin.peek() == std::char_traits<char>::eof();
    if (std::cin.bad() || std::ferror(stdin))
        error(site, "R4008", "cannot read standard input");
    return ended;
}
inline std::string read_line(Site site) {
    std::string text;
    if ((!std::getline(std::cin, text) && !std::cin.eof()) || std::ferror(stdin))
        error(site, "R4008", "cannot read standard input");
    if (!text.empty() && text.back() == '\r')
        text.pop_back();
    return text;
}

inline std::string json_string(std::string_view text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else if (c < 128)
            out += static_cast<char>(c);
        else {
            const std::size_t count = c >= 0xc2 && c <= 0xdf   ? 2
                                      : c >= 0xe0 && c <= 0xef ? 3
                                      : c >= 0xf0 && c <= 0xf4 ? 4
                                                               : 0;
            bool valid = count != 0 && i + count <= text.size();
            for (std::size_t j = 1; valid && j < count; ++j) {
                const auto byte = static_cast<unsigned char>(text[i + j]);
                valid = byte >= 0x80 && byte <= 0xbf;
                if (j == 1)
                    valid = valid && !(c == 0xe0 && byte < 0xa0) && !(c == 0xed && byte >= 0xa0) &&
                            !(c == 0xf0 && byte < 0x90) && !(c == 0xf4 && byte >= 0x90);
            }
            if (valid) {
                out.append(text.substr(i, count));
                i += count - 1;
            } else
                out += "\\ufffd";
        }
    }
    return out + '"';
}
inline void report(const Error& failure, bool json) {
    if (json) {
        std::cerr << "{\"severity\":\"error\",\"code\":" << json_string(failure.code)
                  << ",\"file\":" << json_string(failure.site.file)
                  << ",\"line\":" << failure.site.line << ",\"column\":" << failure.site.column
                  << ",\"message\":" << json_string(failure.message) << "}\n";
    } else {
        std::cerr << failure.site.file << ':' << failure.site.line << ':' << failure.site.column
                  << ": runtime error[" << failure.code << "]: " << failure.message << '\n';
    }
}
class Range {
    Int current_, step_;
    std::uint64_t remaining_ = 0;

  public:
    Range(Int start, Int stop, Int step, Site site) : current_(start), step_(step) {
        // Unsigned subtraction gives the exact positive distance even when the
        // corresponding signed subtraction would overflow. Negating INT64_MIN
        // is likewise done in the unsigned domain. Select direction once, so a
        // variable stride does not need repeated sign selection before division.
        if (step > 0) {
            if (start < stop)
                remaining_ =
                    (std::uint64_t(stop) - std::uint64_t(start) - 1) / std::uint64_t(step) + 1;
        } else if (step < 0) {
            if (start > stop)
                remaining_ = (std::uint64_t(start) - std::uint64_t(stop) - 1) /
                                 (std::uint64_t{0} - std::uint64_t(step)) +
                             1;
        } else
            error(site, "R4005", "range step cannot be zero");
    }
    bool has_next() const {
        return remaining_ != 0;
    }
    Int next() {
        const Int value = current_;
        // Advance only when another representable, in-bounds item remains.
        // The constructor's count proves this signed addition cannot overflow.
        if (remaining_ != 0 && --remaining_ != 0)
            current_ += step_;
        return value;
    }
};
} // namespace wd
// These runtime fragments have dependencies on the preceding fragments.
// clang-format off
#include "wood_arrays.hpp"
#include "wood_callables.hpp"
#include "wood_native.hpp"
#include "wood_streams.hpp"
#include "wood_sqlite.hpp"
#include "wood_tls.hpp"
#include "wood_tasks.hpp"
#include "wood_random.hpp"
#include "wood_resources.hpp"
// clang-format on
#endif
