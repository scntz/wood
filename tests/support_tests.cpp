#include "wood/support.hpp"
#include <iostream>
#include <iterator>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
int checks = 0, failures = 0;
void check(bool value, const char* name) {
    ++checks;
    if (!value) {
        ++failures;
        std::cerr << "FAIL " << name << '\n';
    }
}
void rejects(std::string_view text) {
    try {
        (void)wood::Json::parse(text);
        check(false, "invalid JSON accepted");
    } catch (const std::runtime_error&) {
        check(true, "rejected JSON");
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 2) {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        const std::string text{std::istreambuf_iterator<char>(std::cin), {}};
        try {
            if (std::string_view(argv[1]) == "sha256")
                std::cout << wood::sha256(text) << '\n';
            else if (std::string_view(argv[1]) == "json")
                std::cout << wood::Json::parse(text).dump() << '\n';
            else
                return 2;
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    check(wood::sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty SHA");
    check(wood::sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc SHA");
    check(wood::sha256(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "million a SHA");
    check(wood::sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "long SHA");
    for (const auto& text :
         {"null", "true", "false", "-9223372036854775808", "9223372036854775807", "1.25e-20", "[]",
          "{}", "{\"x\":[1,true,null,\"á\"]}", "\"\\ud83c\\udf33\""}) {
        const auto parsed = wood::Json::parse(text);
        check(wood::Json::parse(parsed.dump()).dump() == parsed.dump(), "JSON round trip");
    }
    for (const auto& text :
         {"", "[", "{]", "[1,]", "{\"a\":1,}", "{\"x\":1,\"x\":2}", "01", "1.", "+1", "1e", "NaN",
          "1e999", "9223372036854775808", "\"\\ud800\"", "\"\\udc00\"", "true false", "\"\\q\""})
        rejects(text);
    rejects(std::string(66, '[') + "0" + std::string(66, ']'));
    rejects(std::string("\"\xff\"", 3));
    const auto tree = wood::Json::parse("{\"x\":[1,2,3]}");
    const auto copy = tree;
    check(&tree.raw() == &copy.raw(), "JSON copies share immutable nodes");
    check(wood::Json::parse("1.0").dump() == "1.0", "JSON float kind is retained");
    check(wood::Json::parse("-0.0").dump() == "-0.0", "JSON negative float zero retained");
    check(!(wood::Json(1) == wood::Json(1.0)), "JSON equality is kind-sensitive");
    std::cout << checks << " support checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
