// Shared strict JSON core used by compiler tools and native Wood programs.
#ifndef WOOD_JSON_HPP
#define WOOD_JSON_HPP
#include <charconv>
#include <cmath>
#include <cstdint>
#include <locale>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
namespace wood {
// Strict JSON for manifests, locks and tool reports. Bounded recursion, finite
// numbers, UTF-8 validation, and duplicate-key rejection are deliberate contracts.
class Json {
  public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Value =
        std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

  private:
    std::shared_ptr<const Value> value_;

  public:
    const Value& raw() const {
        static const Value null_value(nullptr);
        return value_ ? *value_ : null_value;
    }
    bool operator==(const Json& other) const;

    Json() = default;
    Json(bool v) : value_(std::make_shared<const Value>(v)) {}
    Json(int v) : value_(std::make_shared<const Value>(static_cast<std::int64_t>(v))) {}
    Json(std::int64_t v) : value_(std::make_shared<const Value>(v)) {}
    Json(double v) : value_(std::make_shared<const Value>(v)) {}
    Json(std::string v) : value_(std::make_shared<const Value>(std::move(v))) {}
    Json(const char* v) : value_(std::make_shared<const Value>(std::string(v))) {}
    Json(Array v) : value_(std::make_shared<const Value>(std::move(v))) {}
    Json(Object v) : value_(std::make_shared<const Value>(std::move(v))) {}
    static Json parse(std::string_view text);
    std::string dump() const;
    bool contains(const std::string& key) const;
    const Json& at(const std::string& key) const;
    const Object& object() const;
    const Array& array() const;
    const std::string& string() const;
    std::int64_t integer() const;
    bool boolean() const;
};
} // namespace wood
namespace wood {
namespace json_detail {
inline void utf8_append(std::string& out, std::uint32_t cp) {
    if (cp < 0x80)
        out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xc0 | cp >> 6);
        out += static_cast<char>(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xe0 | cp >> 12);
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    } else {
        out += static_cast<char>(0xf0 | cp >> 18);
        out += static_cast<char>(0x80 | ((cp >> 12) & 63));
        out += static_cast<char>(0x80 | ((cp >> 6) & 63));
        out += static_cast<char>(0x80 | (cp & 63));
    }
}
inline std::size_t utf8_length(std::string_view text, std::size_t i) {
    const auto c = static_cast<unsigned char>(text[i]);
    const std::size_t n = c < 128                  ? 1
                          : c >= 0xc2 && c <= 0xdf ? 2
                          : c >= 0xe0 && c <= 0xef ? 3
                          : c >= 0xf0 && c <= 0xf4 ? 4
                                                   : 0;
    if (!n || i + n > text.size())
        return 0;
    for (std::size_t j = 1; j < n; ++j) {
        const auto b = static_cast<unsigned char>(text[i + j]);
        if (b < 0x80 || b > 0xbf ||
            (j == 1 && ((c == 0xe0 && b < 0xa0) || (c == 0xed && b >= 0xa0) ||
                        (c == 0xf0 && b < 0x90) || (c == 0xf4 && b >= 0x90))))
            return 0;
    }
    return n;
}
inline std::string quote(std::string_view text) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c >> 4];
            result += hex[c & 15];
        } else {
            const auto length = utf8_length(text, i);
            if (!length)
                result += "\\ufffd";
            else {
                result.append(text.substr(i, length));
                i += length - 1;
            }
        }
    }
    return result + '"';
}
class Reader {
    std::string_view text;
    std::size_t pos = 0;
    [[noreturn]] void bad(std::string message) const {
        throw std::runtime_error("JSON at byte " + std::to_string(pos + 1) + ": " + message);
    }
    void whitespace() {
        while (pos < text.size() &&
               (text[pos] == ' ' || text[pos] == '\n' || text[pos] == '\r' || text[pos] == '\t'))
            ++pos;
    }
    bool take(char c) {
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    static bool digit(char c) {
        return c >= '0' && c <= '9';
    }
    std::uint32_t hex4() {
        std::uint32_t result = 0;
        for (int j = 0; j < 4; ++j) {
            if (pos == text.size())
                bad("incomplete Unicode escape");
            const char c = text[pos++];
            const int value = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                     : -1;
            if (value < 0)
                bad("invalid Unicode escape");
            result = result * 16 + static_cast<std::uint32_t>(value);
        }
        return result;
    }
    std::string string() {
        if (!take('"'))
            bad("expected string");
        std::string result;
        while (pos < text.size()) {
            const auto c = static_cast<unsigned char>(text[pos++]);
            if (c == '"')
                return result;
            if (c < 32)
                bad("unescaped control character");
            if (c == '\\') {
                if (pos == text.size())
                    bad("unterminated escape");
                switch (text[pos++]) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u': {
                    auto cp = hex4();
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (!take('\\') || !take('u'))
                            bad("missing low surrogate");
                        const auto low = hex4();
                        if (low < 0xdc00 || low > 0xdfff)
                            bad("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff)
                        bad("unpaired low surrogate");
                    utf8_append(result, cp);
                    break;
                }
                default:
                    bad("unknown escape");
                }
            } else {
                const auto length = utf8_length(text, pos - 1);
                if (!length)
                    bad("invalid UTF-8");
                result.append(text.substr(pos - 1, length));
                pos += length - 1;
            }
        }
        bad("unterminated string");
    }
    Json number() {
        const auto start = pos;
        take('-');
        if (pos == text.size() || !digit(text[pos]))
            bad("invalid number");
        if (!take('0'))
            while (pos < text.size() && digit(text[pos]))
                ++pos;
        bool floating = false;
        if (take('.')) {
            floating = true;
            const auto begin = pos;
            while (pos < text.size() && digit(text[pos]))
                ++pos;
            if (pos == begin)
                bad("missing fractional digits");
        }
        if (take('e') || take('E')) {
            floating = true;
            if (!take('+'))
                take('-');
            const auto begin = pos;
            while (pos < text.size() && digit(text[pos]))
                ++pos;
            if (begin == pos)
                bad("missing exponent digits");
        }
        const auto source = text.substr(start, pos - start);
        if (!floating) {
            std::int64_t value = 0;
            const auto parsed =
                std::from_chars(source.data(), source.data() + source.size(), value);
            if (parsed.ec == std::errc{})
                return Json(value);
            bad("integer outside signed 64-bit range");
        }
        std::istringstream input{std::string(source)};
        input.imbue(std::locale::classic());
        double value = 0;
        input >> value;
        if (input.fail() || !std::isfinite(value))
            bad("number outside binary64 range");
        return Json(value);
    }
    Json value(std::size_t depth) {
        if (depth > 64)
            bad("nesting exceeds 64 levels");
        whitespace();
        if (pos == text.size())
            bad("expected value");
        if (text[pos] == '"')
            return Json(string());
        if (take('{')) {
            Json::Object object;
            whitespace();
            if (take('}'))
                return Json(std::move(object));
            for (;;) {
                whitespace();
                auto key = string();
                whitespace();
                if (!take(':'))
                    bad("expected ':'");
                auto item = value(depth + 1);
                if (!object.emplace(std::move(key), std::move(item)).second)
                    bad("duplicate object key");
                whitespace();
                if (take('}'))
                    return Json(std::move(object));
                if (!take(','))
                    bad("expected ',' or '}'");
            }
        }
        if (take('[')) {
            Json::Array array;
            whitespace();
            if (take(']'))
                return Json(std::move(array));
            for (;;) {
                array.push_back(value(depth + 1));
                whitespace();
                if (take(']'))
                    return Json(std::move(array));
                if (!take(','))
                    bad("expected ',' or ']'");
            }
        }
        for (const auto& [word, result] : std::vector<std::pair<std::string_view, Json>>{
                 {"true", Json(true)}, {"false", Json(false)}, {"null", Json()}})
            if (text.substr(pos, word.size()) == word) {
                pos += word.size();
                return result;
            }
        if (text[pos] == '-' || digit(text[pos]))
            return number();
        bad("unexpected token");
    }

  public:
    explicit Reader(std::string_view source) : text(source) {}
    Json run() {
        auto result = value(0);
        whitespace();
        if (pos != text.size())
            bad("trailing content");
        return result;
    }
};
} // namespace json_detail
inline Json Json::parse(std::string_view text) {
    return json_detail::Reader(text).run();
}
inline const Json::Object& Json::object() const {
    if (const auto* p = std::get_if<Object>(&raw()))
        return *p;
    throw std::runtime_error("expected JSON object");
}
inline const Json::Array& Json::array() const {
    if (const auto* p = std::get_if<Array>(&raw()))
        return *p;
    throw std::runtime_error("expected JSON array");
}
inline const std::string& Json::string() const {
    if (const auto* p = std::get_if<std::string>(&raw()))
        return *p;
    throw std::runtime_error("expected JSON string");
}
inline std::int64_t Json::integer() const {
    if (const auto* p = std::get_if<std::int64_t>(&raw()))
        return *p;
    throw std::runtime_error("expected JSON integer");
}
inline bool Json::boolean() const {
    if (const auto* p = std::get_if<bool>(&raw()))
        return *p;
    throw std::runtime_error("expected JSON boolean");
}
inline bool Json::contains(const std::string& key) const {
    return object().contains(key);
}
inline const Json& Json::at(const std::string& key) const {
    auto found = object().find(key);
    if (found == object().end())
        throw std::runtime_error("missing field '" + key + "'");
    return found->second;
}
inline std::string Json::dump() const {
    if (std::holds_alternative<std::nullptr_t>(raw()))
        return "null";
    if (const auto* p = std::get_if<bool>(&raw()))
        return *p ? "true" : "false";
    if (const auto* p = std::get_if<std::int64_t>(&raw()))
        return std::to_string(*p);
    if (const auto* p = std::get_if<double>(&raw())) {
        if (!std::isfinite(*p))
            throw std::runtime_error("nonfinite JSON number");
        char buffer[128];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), *p);
        std::string text(buffer, result.ptr);
        if (text.find_first_of(".eE") == std::string::npos)
            text += ".0";
        return text;
    }
    if (const auto* p = std::get_if<std::string>(&raw()))
        return json_detail::quote(*p);
    if (const auto* p = std::get_if<Array>(&raw())) {
        std::string out = "[";
        for (const auto& item : *p) {
            if (out.size() > 1)
                out += ',';
            out += item.dump();
        }
        return out + ']';
    }
    std::string out = "{";
    for (const auto& [key, item] : object()) {
        if (out.size() > 1)
            out += ',';
        out += json_detail::quote(key) + ':' + item.dump();
    }
    return out + '}';
}
inline bool Json::operator==(const Json& other) const {
    return raw() == other.raw();
}
} // namespace wood

#endif
