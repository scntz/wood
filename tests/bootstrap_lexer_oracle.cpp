#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include <array>
#include <iostream>
#include <random>
#include <set>
#include <string_view>

namespace {
constexpr auto names = std::to_array<std::string_view>({"Eof","Newline","Indent","Dedent","Identifier","Integer","Float","String","Fn","Import","From","As","Try","Except","Raise","Dot","If","Elif","Else","While","For","In","Return","Break","Continue","Pass","True","False","And","Or","Not","LParen","RParen","LBracket","RBracket","LBrace","RBrace","Comma","Colon","Arrow","FatArrow","Plus","Minus","Star","Slash","FloorDiv","Percent","BitAnd","BitOr","BitXor","BitNot","ShiftLeft","ShiftRight","Augment","Equal","EqualEqual","BangEqual","Less","LessEqual","Greater","GreaterEqual"});
static_assert(names.size() == static_cast<std::size_t>(wood::TokenKind::GreaterEqual) + 1);
std::string hex_text(std::string_view text) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(text.size() * 2);
    for (unsigned char byte : text) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
wood::Json position(const wood::Location& at, wood::Json::Array tokens,
                    const std::string& code = {}, const std::string& message = {}) {
    return wood::Json::Object{{"format",1}, {"code",code}, {"message",message},
        {"offset",static_cast<std::int64_t>(at.offset)},
        {"line",static_cast<std::int64_t>(at.line)},
        {"column",static_cast<std::int64_t>(at.column)}, {"tokens",std::move(tokens)}};
}
wood::Json scan(std::string_view source) {
    try {
        const auto tokens = wood::lex(source);
        wood::Json::Array output;
        for (const auto& token : tokens) {
            output.emplace_back(wood::Json::Object{
                {"kind",std::string(names.at(static_cast<std::size_t>(token.kind)))},
                {"text_hex",hex_text(token.text)},
                {"offset",static_cast<std::int64_t>(token.location.offset)},
                {"line",static_cast<std::int64_t>(token.location.line)},
                {"column",static_cast<std::int64_t>(token.location.column)}});
        }
        return position(tokens.back().location,std::move(output));
    } catch (const wood::Diagnostic& error) {
        return position(error.location,{},error.code,error.what());
    }
}
int generate(const wood::fs::path& directory) {
    wood::fs::create_directories(directory);
    int count = 0;
    std::array<bool,names.size()> seen{};
    std::set<std::string> errors;
    auto save = [&](std::string_view source) {
        wood::write_file(directory / (std::to_string(count++) + ".wd"),source);
        try {
            for (const auto& token : wood::lex(source))
                seen.at(static_cast<std::size_t>(token.kind)) = true;
        } catch (const wood::Diagnostic& error) { errors.insert(error.code); }
    };
    for (const auto& source : {"", " ", "# comment", "\r\n\r\n", "x\r\ny\rz\n", "x #comment",
        "fn a(x:int)->int:\n  return x+1\n", "x\n  y\n z", "\t", "x\t", "x=(\n # c\n 1\n)\n",
        "([)]", "}", "{[", "1.", "1e", "1e+", "1e-", "1word", "1.0word", "1..2", ".5",
        "'x", "'x\n'", "'x\\", "'\\q'", "'\\\n'", "'\\n\\r\\t\\0\\\\\\\"\\\''",
        "record def True False None self", "import from as try except raise fn if elif else while for in return break continue pass true false and or not",
        ". , : -> => + - * / // % & | ^ ~ << >> = == != < <= > >= <<= >>= //= += -= *= /= %= &= |= ^=",
        "\xef\xbb\xbf", "\xef\xbb\xbfx\n", "x\xef\xbb\xbf"}) save(source);
    save("([{}])");
    save("0 01 1.25 2e3 2.0E-3 9e+1");
    save(std::string(256,'(') + "0" + std::string(256,')'));
    save(std::string(257,'('));
    save(std::string(257,'['));
    save(std::string(257,'{'));
    for (int levels : {256,257}) {
        std::string source;
        for (int depth = 0; depth < levels; ++depth) {
            source.append(static_cast<std::size_t>(depth),' ');
            source += "x\n";
        }
        save(source);
    }
    for (int byte = 0; byte < 256; ++byte) {
        save(std::string(1,static_cast<char>(byte)));
        save("'" + std::string(1,static_cast<char>(byte)) + "'");
    }
    std::mt19937 random(0x574f4f44);
    const std::string seed = "fn add(x: int, y: int) -> int:\n    # native compiler\n    return x+y\nprint(add(1,2))\n";
    for (int trial = 0; trial < 256; ++trial) {
        auto source = seed;
        for (int edit = 0; edit < 3; ++edit) {
            const auto index = random() % source.size();
            source[index] = static_cast<char>(random() & 255);
        }
        save(source);
    }
    for (bool present : seen)
        if (!present) throw std::runtime_error("generated fixtures do not cover every token kind");
    if (errors.size() != 7) throw std::runtime_error("generated fixtures do not cover all seven lexical errors");
    std::cout << count << '\n';
    return 0;
}
int run(const std::vector<wood::fs::path>& arguments) {
    if (arguments.size() == 2 && arguments[0] == "--generate") return generate(arguments[1]);
    if (arguments.size() == 2 && arguments[0] == "--contract") {
        const auto contract = wood::Json::parse(wood::read_file(arguments[1]));
        const auto& kinds = contract.at("tokens").array();
        if (contract.at("format").integer() != 1 || kinds.size() != names.size()) return 1;
        for (std::size_t i = 0; i < kinds.size(); ++i)
            if (kinds[i].string() != names[i]) return 1;
        return 0;
    }
    if (arguments.empty()) return 2;
    int status = 0;
    for (const auto& path : arguments) {
        auto report = scan(wood::read_file(path));
        if (!report.at("code").string().empty()) status = 1;
        std::cout << report.dump() << '\n';
    }
    return status;
}
int guarded(const std::vector<wood::fs::path>& arguments) {
    try { return run(arguments); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return guarded(std::vector<wood::fs::path>(argv + 1,argv + argc));
}
