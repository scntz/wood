#include "wood/compiler.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace wood {
std::vector<Token> lex(std::string_view source, std::size_t source_id) {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"import", TokenKind::Import}, {"from", TokenKind::From},
        {"as", TokenKind::As},         {"try", TokenKind::Try},
        {"except", TokenKind::Except}, {"raise", TokenKind::Raise},
        {"fn", TokenKind::Fn},         {"if", TokenKind::If},
        {"elif", TokenKind::Elif},     {"else", TokenKind::Else},
        {"while", TokenKind::While},   {"for", TokenKind::For},
        {"in", TokenKind::In},         {"return", TokenKind::Return},
        {"break", TokenKind::Break},   {"continue", TokenKind::Continue},
        {"pass", TokenKind::Pass},     {"true", TokenKind::True},
        {"false", TokenKind::False},   {"and", TokenKind::And},
        {"or", TokenKind::Or},         {"not", TokenKind::Not}};
    std::vector<Token> tokens;
    std::vector<std::size_t> indents{0};
    std::vector<std::pair<char, Location>> parens;
    std::size_t i = 0, line = 1, column = 1;
    bool line_start = true, logical_content = false;
    auto at = [&] { return Location{i, line, column, source_id}; };
    auto emit = [&](TokenKind kind, std::string text, Location location) {
        tokens.push_back({kind, std::move(text), location});
    };
    auto advance = [&] {
        ++i;
        ++column;
    };
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto alpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    // A UTF-8 BOM is allowed only at the start of a file.
    if (source.starts_with("\xef\xbb\xbf"))
        i = 3;
    while (i < source.size()) {
        if (line_start) {
            const auto beginning = at();
            std::size_t spaces = 0;
            while (i < source.size() && source[i] == ' ') {
                advance();
                ++spaces;
            }
            if (i < source.size() && source[i] == '\t')
                fail("E1001", at(), "tabs are not allowed outside strings; use spaces");
            if (i == source.size())
                break;
            if (parens.empty() && source[i] != '#' && source[i] != '\r' && source[i] != '\n') {
                if (spaces > indents.back()) {
                    if (indents.size() >= 256)
                        fail("E1002", at(), "indentation nesting exceeds 255 levels");
                    indents.push_back(spaces);
                    emit(TokenKind::Indent, "", beginning);
                } else {
                    while (spaces < indents.back()) {
                        indents.pop_back();
                        emit(TokenKind::Dedent, "", beginning);
                    }
                    if (spaces != indents.back())
                        fail("E1002", at(), "dedent does not match an outer indentation level");
                }
            }
            line_start = false;
        }
        const char c = source[i];
        if (c == ' ') {
            advance();
            continue;
        }
        if (c == '\t')
            fail("E1001", at(), "tabs are not allowed outside strings; use spaces");
        if (c == '#') {
            while (i < source.size() && source[i] != '\r' && source[i] != '\n')
                advance();
            continue;
        }
        if (c == '\r' || c == '\n') {
            const auto location = at();
            if (c == '\r' && i + 1 < source.size() && source[i + 1] == '\n')
                ++i;
            ++i;
            ++line;
            column = 1;
            line_start = true;
            if (parens.empty() && logical_content) {
                emit(TokenKind::Newline, "", location);
                logical_content = false;
            }
            continue;
        }
        const auto location = at();
        logical_content = true;
        if (alpha(c)) {
            const auto start = i;
            while (i < source.size() && (alpha(source[i]) || digit(source[i])))
                advance();
            std::string word(source.substr(start, i - start));
            auto found = keywords.find(word);
            emit(found == keywords.end() ? TokenKind::Identifier : found->second, word, location);
            continue;
        }
        if (digit(c)) {
            const auto start = i;
            while (i < source.size() && digit(source[i]))
                advance();
            bool floating = false;
            if (i < source.size() && source[i] == '.') {
                floating = true;
                advance();
                if (i == source.size() || !digit(source[i]))
                    fail("E1003", at(), "expected digits after decimal point");
                while (i < source.size() && digit(source[i]))
                    advance();
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                floating = true;
                advance();
                if (i < source.size() && (source[i] == '+' || source[i] == '-'))
                    advance();
                if (i == source.size() || !digit(source[i]))
                    fail("E1003", at(), "expected digits in exponent");
                while (i < source.size() && digit(source[i]))
                    advance();
            }
            if (i < source.size() && alpha(source[i]))
                fail("E1003", at(), "invalid suffix on numeric literal");
            emit(floating ? TokenKind::Float : TokenKind::Integer,
                 std::string(source.substr(start, i - start)), location);
            continue;
        }
        if (c == '"' || c == '\'') {
            const char quote = c;
            advance();
            std::string value;
            while (i < source.size() && source[i] != quote) {
                if (source[i] == '\r' || source[i] == '\n')
                    fail("E1004", location, "unterminated string literal");
                if (source[i] == '\\') {
                    advance();
                    if (i == source.size())
                        fail("E1004", location, "unterminated string escape");
                    switch (source[i]) {
                    case 'n':
                        value += '\n';
                        break;
                    case 'r':
                        value += '\r';
                        break;
                    case 't':
                        value += '\t';
                        break;
                    case '0':
                        value += '\0';
                        break;
                    case '\\':
                        value += '\\';
                        break;
                    case '\'':
                        value += '\'';
                        break;
                    case '"':
                        value += '"';
                        break;
                    default:
                        fail("E1005", at(), "unknown string escape");
                    }
                } else {
                    if (static_cast<unsigned char>(source[i]) < 32)
                        fail("E1005", at(), "control character in string; use an escape");
                    value += source[i];
                }
                advance();
            }
            if (i == source.size())
                fail("E1004", location, "unterminated string literal");
            advance();
            emit(TokenKind::String, std::move(value), location);
            continue;
        }
        // Longest match first, including three-byte augmented shifts/division.
        bool augmented = false;
        for (const std::string_view op :
             {"<<=", ">>=", "//=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="}) {
            if (!source.substr(i).starts_with(op))
                continue;
            for (std::size_t j = 0; j < op.size(); ++j)
                advance();
            emit(TokenKind::Augment, std::string(op.substr(0, op.size() - 1)), location);
            augmented = true;
            break;
        }
        if (augmented)
            continue;
        const auto pair = source.substr(i, 2);
        const std::pair<std::string_view, TokenKind> doubles[] = {
            {"<<", TokenKind::ShiftLeft},   {">>", TokenKind::ShiftRight},
            {"->", TokenKind::Arrow},       {"=>", TokenKind::FatArrow},
            {"//", TokenKind::FloorDiv},    {"==", TokenKind::EqualEqual},
            {"!=", TokenKind::BangEqual},   {"<=", TokenKind::LessEqual},
            {">=", TokenKind::GreaterEqual}};
        bool matched = false;
        for (const auto& [spelling, kind] : doubles) {
            if (pair == spelling) {
                advance();
                advance();
                emit(kind, std::string(spelling), location);
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        TokenKind kind;
        switch (c) {
        case '(':
            kind = TokenKind::LParen;
            parens.emplace_back('(', location);
            if (parens.size() > 256)
                fail("E1006", location, "parenthesis nesting exceeds 256 levels");
            break;
        case ')':
            kind = TokenKind::RParen;
            if (parens.empty() || parens.back().first != '(')
                fail("E1006", location, "unmatched closing parenthesis");
            parens.pop_back();
            break;
        case '[':
            kind = TokenKind::LBracket;
            parens.emplace_back('[', location);
            if (parens.size() > 256)
                fail("E1006", location, "delimiter nesting exceeds 256 levels");
            break;
        case ']':
            kind = TokenKind::RBracket;
            if (parens.empty() || parens.back().first != '[')
                fail("E1006", location, "unmatched closing bracket");
            parens.pop_back();
            break;
        case '.':
            kind = TokenKind::Dot;
            break;
        case '{':
            kind = TokenKind::LBrace;
            parens.emplace_back('{', location);
            if (parens.size() > 256)
                fail("E1006", location, "delimiter nesting exceeds 256 levels");
            break;
        case '}':
            kind = TokenKind::RBrace;
            if (parens.empty() || parens.back().first != '{')
                fail("E1006", location, "unmatched closing brace");
            parens.pop_back();
            break;
        case ',':
            kind = TokenKind::Comma;
            break;
        case ':':
            kind = TokenKind::Colon;
            break;
        case '+':
            kind = TokenKind::Plus;
            break;
        case '-':
            kind = TokenKind::Minus;
            break;
        case '*':
            kind = TokenKind::Star;
            break;
        case '/':
            kind = TokenKind::Slash;
            break;
        case '%':
            kind = TokenKind::Percent;
            break;
        case '&':
            kind = TokenKind::BitAnd;
            break;
        case '|':
            kind = TokenKind::BitOr;
            break;
        case '^':
            kind = TokenKind::BitXor;
            break;
        case '~':
            kind = TokenKind::BitNot;
            break;
        case '=':
            kind = TokenKind::Equal;
            break;
        case '<':
            kind = TokenKind::Less;
            break;
        case '>':
            kind = TokenKind::Greater;
            break;
        default:
            fail("E1007", location, std::string("unexpected character outside string or comment") +
                 (c == '!' ? "; wood spells this 'not'" :
                  c == ';' ? "; statements end at the end of the line" :
                  c == '?' ? "; wood has no ternary operator, so write 'a if condition else b'" : ""));
        }
        advance();
        emit(kind, std::string(1, c), location);
    }
    if (!parens.empty())
        fail("E1006", parens.back().second, "unclosed parenthesis or bracket");
    if (logical_content)
        emit(TokenKind::Newline, "", at());
    while (indents.size() > 1) {
        indents.pop_back();
        emit(TokenKind::Dedent, "", at());
    }
    emit(TokenKind::Eof, "", at());
    return tokens;
}
} // namespace wood
