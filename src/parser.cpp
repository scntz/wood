#include "wood/compiler.hpp"
#include <algorithm>
#include <optional>
#include <utility>

namespace wood {
namespace {
class Parser {
    const std::vector<Token>& tokens;
    std::size_t pos = 0;
    std::size_t recursion = 0;
    std::size_t conditional_depth = 0;
    std::size_t type_depth = 0;
    std::unordered_map<const Expr*, std::size_t> depths;
    // Statements an unpacking assignment adds after itself, collected by the enclosing block.
    Block pending;
    std::size_t hidden = 0;
    // Error bindings of the except blocks around the current statement, innermost last.
    std::vector<std::string> handlers;
    const Token& peek() const {
        return tokens.at(pos);
    }
    // Spellings from other languages. Only called on an error path, and it only adds advice.
    std::string habit_hint(const Location& at) const {
        std::size_t index = pos;
        while (index > 0 && tokens.at(index - 1).location.offset >= at.offset)
            --index;
        if (index >= tokens.size() || index == 0)
            return {};
        const auto& token = tokens.at(index);
        const auto& previous = tokens.at(index - 1);
        const auto before = index > 1 ? tokens.at(index - 2).kind : token.kind;
        if (token.kind == TokenKind::BitAnd && previous.kind == TokenKind::BitAnd)
            return "; wood spells this 'and'";
        if (token.kind == TokenKind::BitOr && previous.kind == TokenKind::BitOr)
            return "; wood spells this 'or'";
        if (previous.kind == TokenKind::Plus && before == TokenKind::Plus)
            return "; wood has no ++, so write x += 1";
        if (previous.kind == TokenKind::Minus && before == TokenKind::Minus)
            return "; wood has no --, so write x -= 1";
        if (token.kind == TokenKind::String && previous.kind == TokenKind::Identifier && previous.text == "f")
            return "; wood has no f-strings, so join with + and str(value)";
        if (previous.kind == TokenKind::Identifier) {
            const auto& word = previous.text;
            if (word == "let" || word == "var" || word == "const" || word == "val" || word == "auto")
                return "; declare a variable by assigning to it, as in x = 1";
            if (word == "class" || word == "struct" || word == "interface" || word == "type")
                return "; declare a type with 'record'";
            if (word == "function" || word == "func" || word == "fun" || word == "def")
                return "; declare a function with 'fn'";
            if (word == "print" && (token.kind == TokenKind::String || token.kind == TokenKind::Integer || token.kind == TokenKind::Identifier))
                return "; print is a function, so write print(value)";
        }
        return {};
    }
    [[noreturn]] void fail(std::string code, Location at, std::string message) const {
        if (code == "E2001" || code == "E2004")
            message += habit_hint(at);
        ::wood::fail(std::move(code), at, std::move(message));
    }
    bool is(TokenKind kind) const {
        return peek().kind == kind;
    }
    Token take() {
        return tokens.at(pos++);
    }
    bool match(TokenKind kind) {
        if (!is(kind))
            return false;
        ++pos;
        return true;
    }
    Token expect(TokenKind kind, std::string message) {
        if (!is(kind))
            fail("E2001", peek().location, std::move(message));
        return take();
    }
    ExprPtr node(Expr::Kind kind, const Token& token, std::vector<ExprPtr> children = {}) {
        auto result = std::make_unique<Expr>(kind, token.location, token.text);
        std::size_t depth = 1;
        for (const auto& child : children)
            if (child)
                depth = std::max(depth, depths.at(child.get()) + 1);
        if (depth > 256)
            fail("E2002", token.location, "expression nesting exceeds 256 levels");
        result->children = std::move(children);
        depths[result.get()] = depth;
        return result;
    }
    Type annotation(bool allow_void = false) {
        auto result = primary_annotation(allow_void);
        if (match(TokenKind::BitOr)) {
            auto none = expect(TokenKind::Identifier, "expected None after '|' in optional type");
            if (none.text != "None" || result == Type::Void)
                fail("E2003", none.location, "only T | None optional unions are supported");
            result = Type::optional(std::move(result));
        }
        return result;
    }
    Type primary_annotation(bool allow_void = false) {
        if (match(TokenKind::LParen)) {
            if (++type_depth > 64)
                fail("E2002", peek().location, "type nesting exceeds 64 levels");
            auto result = annotation(allow_void);
            expect(TokenKind::RParen, "expected ')' after grouped type");
            --type_depth;
            return result;
        }
        const bool erased = is(TokenKind::Identifier) && peek().text == "callable" &&
                            tokens.at(pos + 1).kind == TokenKind::LParen;
        if (match(TokenKind::Fn) || (erased && (take(), true))) {
            if (++type_depth > 64)
                fail("E2002", tokens.at(pos - 1).location, "type nesting exceeds 64 levels");
            expect(TokenKind::LParen, "expected '(' in function type");
            std::vector<Type> parameters;
            if (!is(TokenKind::RParen)) {
                do {
                    parameters.push_back(annotation());
                    if (parameters.size() > 255)
                        fail("E2002", peek().location,
                             "function types support at most 255 parameters");
                } while (match(TokenKind::Comma) && !is(TokenKind::RParen));
            }
            expect(TokenKind::RParen, "expected ')' in function type");
            expect(TokenKind::Arrow, "expected '->' and function result type");
            auto result = annotation(true);
            --type_depth;
            auto type = Type::callable(std::move(parameters), std::move(result));
            if (erased)
                type.kind = Type::Callable;
            return type;
        }
        auto token = expect(TokenKind::Identifier, "expected a type name");
        if (token.text == "tuple" && is(TokenKind::LBracket)) {
            take();
            if (++type_depth > 64)
                fail("E2002", token.location, "type nesting exceeds 64 levels");
            std::vector<Type> elements;
            if (match(TokenKind::LParen))
                expect(TokenKind::RParen, "empty tuple type is tuple[()]");
            else {
                do {
                    elements.push_back(annotation());
                    if (elements.size() > 255)
                        fail("E2002", token.location, "tuples support at most 255 elements");
                } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
            }
            expect(TokenKind::RBracket, "expected ']' after tuple types");
            --type_depth;
            return Type::tuple(std::move(elements));
        }
        if ((token.text == "list" || token.text == "dict") && !is(TokenKind::Dot)) {
            if (++type_depth > 64)
                fail("E2002", token.location, "type nesting exceeds 64 levels");
            expect(TokenKind::LBracket, "expected '[' and list element type");
            auto element = annotation();
            if (token.text == "dict") {
                expect(TokenKind::Comma, "expected ',' between dictionary key and value types");
                auto value = annotation();
                expect(TokenKind::RBracket, "expected ']' after dictionary types");
                --type_depth;
                if (element != Type::Int && element != Type::Str && element != Type::Bool &&
                    element.kind != Type::Named)
                    fail("E3020", token.location, "dictionary keys must be int, str, or bool");
                return Type::dict(element, value);
            }
            expect(TokenKind::RBracket, "expected ']' after list element type");
            --type_depth;
            return Type::list(element);
        }
        if (is(TokenKind::LParen)) {
            take();
            if (++type_depth > 64)
                fail("E2002", token.location, "type nesting exceeds 64 levels");
            auto signature = Type::callable({}, Type::Unknown);
            signature.kind = Type::Signature;
            signature.name = token.text;
            if (!is(TokenKind::RParen))
                do {
                    signature.parameters.push_back(annotation());
                    if (signature.parameters.size() > 255)
                        fail("E2002", token.location,
                             "callable patterns support at most 255 parameters");
                } while (match(TokenKind::Comma) && !is(TokenKind::RParen));
            expect(TokenKind::RParen, "expected ')' after callable pattern");
            if (match(TokenKind::Arrow))
                signature.element = std::make_shared<const Type>(annotation(true));
            --type_depth;
            return signature;
        }
        if (token.text == "int")
            return Type::Int;
        if (token.text == "float")
            return Type::Float;
        if (token.text == "bool")
            return Type::Bool;
        if (token.text == "str")
            return Type::Str;
        if (token.text == "error")
            return Type::Error;
        if (token.text == "json" && !is(TokenKind::Dot))
            return Type::Json;
        if (allow_void && token.text == "void")
            return Type::Void;
        if (token.text == "void")
            fail("E2003", token.location, "void is only a function return type");
        while (match(TokenKind::Dot))
            token.text += "." + expect(TokenKind::Identifier, "expected type name after '.'").text;
        auto result = Type::named(token.text);
        if (match(TokenKind::LBracket)) {
            if (++type_depth > 64)
                fail("E2002", token.location, "type nesting exceeds 64 levels");
            do {
                result.parameters.push_back(annotation());
                if (result.parameters.size() > 32)
                    fail("E2002", token.location, "records support at most 32 type arguments");
            } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
            expect(TokenKind::RBracket, "expected ']' after record type arguments");
            --type_depth;
        }
        return result;
    }
    static int precedence(TokenKind kind) {
        switch (kind) {
        case TokenKind::Or:
            return 1;
        case TokenKind::And:
            return 2;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::Less:
        case TokenKind::LessEqual:
        case TokenKind::Greater:
        case TokenKind::GreaterEqual:
        case TokenKind::In:
        case TokenKind::Not:
            return 4;
        case TokenKind::Plus:
        case TokenKind::Minus:
            return 9;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::FloorDiv:
        case TokenKind::Percent:
            return 10;
        case TokenKind::BitOr:
            return 5;
        case TokenKind::BitXor:
            return 6;
        case TokenKind::BitAnd:
            return 7;
        case TokenKind::ShiftLeft:
        case TokenKind::ShiftRight:
            return 8;
        default:
            return 0;
        }
    }
    int current_precedence() const {
        return is(TokenKind::Identifier) && peek().text == "is" ? 4 : precedence(peek().kind);
    }
    ExprPtr call_node(Expr::Kind kind, const Token& token, std::vector<ExprPtr> args = {}) {
        std::vector<std::string> keywords;
        bool named = false;
        if (!is(TokenKind::RParen)) {
            do {
                std::string keyword;
                if (is(TokenKind::Identifier) && tokens.at(pos + 1).kind == TokenKind::Equal) {
                    keyword = take().text;
                    take();
                    named = true;
                } else if (named)
                    fail("E2001", peek().location, "positional argument follows keyword argument");
                keywords.push_back(std::move(keyword));
                args.push_back(expression());
                if (keywords.size() > 255)
                    fail("E2002", token.location, "calls support at most 255 arguments");
            } while (match(TokenKind::Comma) && !is(TokenKind::RParen));
        }
        expect(TokenKind::RParen, "expected ')' after arguments");
        auto result = node(kind, token, std::move(args));
        result->keywords = std::move(keywords);
        return result;
    }
    ExprPtr comprehension(Expr::Kind kind, const Token& token, std::vector<ExprPtr> children,
                          TokenKind closing) {
        std::vector<ComprehensionClause> clauses;
        while (is(TokenKind::For) || is(TokenKind::If)) {
            const auto clause = take();
            std::string name;
            if (clause.kind == TokenKind::For) {
                name = expect(TokenKind::Identifier, "expected comprehension binding").text;
                expect(TokenKind::In, "expected 'in' after comprehension binding");
            }
            children.push_back(expression(1, false));
            clauses.push_back({std::move(name), clause.location});
            if (clauses.size() > 64)
                fail("E2002", clause.location, "comprehensions support at most 64 clauses");
        }
        expect(closing, "expected closing delimiter after comprehension");
        auto result = node(kind, token, std::move(children));
        result->clauses = std::move(clauses);
        return result;
    }
    ExprPtr expression(int minimum = 1, bool conditional = true) {
        if (++recursion > 256)
            fail("E2002", peek().location, "expression nesting exceeds 256 levels");
        ExprPtr left;
        auto token = take();
        switch (token.kind) {
        case TokenKind::Integer:
            left = node(Expr::Kind::Integer, token);
            break;
        case TokenKind::Float:
            left = node(Expr::Kind::Float, token);
            break;
        case TokenKind::String:
            left = node(Expr::Kind::String, token);
            break;
        case TokenKind::True:
        case TokenKind::False:
            left = node(Expr::Kind::Bool, token);
            break;
        case TokenKind::Fn: {
            auto function = std::make_unique<Stmt>(Stmt::Kind::Function, token.location);
            function->name = "<closure>";
            function->return_type = Type::Unknown;
            function->inferred_return = true;
            function->expression_body = true;
            if (match(TokenKind::LBracket)) {
                do {
                    auto mut = expect(TokenKind::Identifier, "expected mut capture");
                    if (mut.text != "mut")
                        fail("E3028", mut.location,
                             "explicit captures use mut name; other names capture immutable "
                             "snapshots automatically");
                    function->mutable_captures.push_back(
                        expect(TokenKind::Identifier, "expected capture name").text);
                } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
                expect(TokenKind::RBracket, "expected ']' after captures");
            }
            expect(TokenKind::LParen, "expected '(' after fn in a closure expression");
            if (!is(TokenKind::RParen))
                do {
                    const auto parameter =
                        expect(TokenKind::Identifier, "expected closure parameter");
                    auto type = match(TokenKind::Colon) ? annotation() : Type(Type::Unknown);
                    function->parameters.push_back({parameter.text, type, parameter.location});
                    if (function->parameters.size() > 255)
                        fail("E2002", token.location, "closures support at most 255 parameters");
                } while (match(TokenKind::Comma) && !is(TokenKind::RParen));
            expect(TokenKind::RParen, "expected ')' after closure parameters");
            if (match(TokenKind::Arrow)) {
                function->return_type = annotation(true);
                function->inferred_return = false;
            }
            expect(TokenKind::FatArrow, "expected '=>' and closure result expression");
            auto result = std::make_unique<Stmt>(Stmt::Kind::Return, token.location);
            result->expressions.push_back(expression());
            function->body.push_back(std::move(result));
            left = node(Expr::Kind::Closure, token);
            left->function = std::move(function);
            break;
        }
        case TokenKind::Identifier:
            while (match(TokenKind::Dot))
                token.text += "." + expect(TokenKind::Identifier, "expected a name after '.'").text;
            if (match(TokenKind::LParen)) {
                left = call_node(Expr::Kind::Call, token);
            } else {
                // Non-call dotted names denote fields; module names are only
                // resolved as the target of a named call in this release.
                const auto full = token.text;
                const auto first = full.find('.');
                token.text = full.substr(0, first);
                left = node(Expr::Kind::Name, token);
                auto cursor = first;
                while (cursor != std::string::npos) {
                    const auto end = full.find('.', cursor + 1);
                    token.text =
                        full.substr(cursor + 1, end == std::string::npos ? end : end - cursor - 1);
                    std::vector<ExprPtr> base;
                    base.push_back(std::move(left));
                    left = node(Expr::Kind::Member, token, std::move(base));
                    cursor = end;
                }
            }
            if (is(TokenKind::LBracket)) {
                // A named bracket target can be either a type application or
                // an ordinary indexed callable. Preserve the latter parse until
                // semantic name resolution can distinguish them.
                const auto start = pos;
                const auto depth = type_depth;
                std::vector<Type> types;
                std::size_t end = start;
                try {
                    take();
                    do {
                        types.push_back(annotation());
                        if (types.size() > 32)
                            fail("E2002", token.location,
                                 "records support at most 32 type arguments");
                    } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
                    expect(TokenKind::RBracket, "expected ']' after type arguments");
                    if (is(TokenKind::LParen))
                        end = pos;
                } catch (const Diagnostic&) {
                    types.clear();
                }
                pos = start;
                type_depth = depth;
                if (end != start) {
                    ExprPtr index;
                    const auto saved_recursion = recursion;
                    try {
                        take();
                        index = expression();
                        expect(TokenKind::RBracket, "expected ']' after index");
                        if (pos != end)
                            index.reset();
                    } catch (const Diagnostic&) {
                        index.reset();
                    }
                    recursion = saved_recursion;
                    pos = end;
                    std::vector<ExprPtr> operands;
                    operands.push_back(std::move(left));
                    operands.push_back(std::move(index));
                    left = node(Expr::Kind::TypeApply, token, std::move(operands));
                    left->type_arguments = std::move(types);
                }
            }
            break;
        case TokenKind::LParen: {
            std::vector<ExprPtr> elements;
            if (!is(TokenKind::RParen)) {
                elements.push_back(expression());
                if (!match(TokenKind::Comma)) {
                    expect(TokenKind::RParen, "expected ')' after expression");
                    left = std::move(elements[0]);
                    break;
                }
                while (!is(TokenKind::RParen)) {
                    elements.push_back(expression());
                    if (elements.size() > 255)
                        fail("E2002", token.location, "tuples support at most 255 elements");
                    if (!match(TokenKind::Comma))
                        break;
                }
            }
            expect(TokenKind::RParen, "expected ')' after tuple");
            left = node(Expr::Kind::Tuple, token, std::move(elements));
            break;
        }
        case TokenKind::LBracket: {
            std::vector<ExprPtr> elements;
            if (!is(TokenKind::RBracket)) {
                elements.push_back(expression());
                if (is(TokenKind::For)) {
                    left = comprehension(Expr::Kind::ListComprehension, token, std::move(elements),
                                         TokenKind::RBracket);
                    break;
                }
                while (match(TokenKind::Comma) && !is(TokenKind::RBracket))
                    elements.push_back(expression());
            }
            expect(TokenKind::RBracket, "expected ']' after list elements");
            left = node(Expr::Kind::List, token, std::move(elements));
            break;
        }
        case TokenKind::LBrace: {
            std::vector<ExprPtr> elements;
            if (!is(TokenKind::RBrace)) {
                do {
                    elements.push_back(expression());
                    expect(TokenKind::Colon, "expected ':' between dictionary key and value");
                    elements.push_back(expression());
                    if (elements.size() == 2 && is(TokenKind::For)) {
                        left = comprehension(Expr::Kind::DictComprehension, token,
                                             std::move(elements), TokenKind::RBrace);
                        break;
                    }
                } while (match(TokenKind::Comma) && !is(TokenKind::RBrace));
            }
            if (left)
                break;
            expect(TokenKind::RBrace, "expected '}' after dictionary elements");
            left = node(Expr::Kind::Dict, token, std::move(elements));
            break;
        }
        case TokenKind::Minus:
        case TokenKind::Plus:
        case TokenKind::BitNot:
        case TokenKind::Not: {
            if (token.kind == TokenKind::Not && minimum > 3)
                fail("E2004", token.location, "parenthesize 'not' when used as an operand");
            std::vector<ExprPtr> operand;
            operand.push_back(expression(token.kind == TokenKind::Not ? 3 : 11));
            left = node(Expr::Kind::Unary, token, std::move(operand));
            break;
        }
        default:
            fail("E2004", token.location, "expected an expression");
        }
        while (is(TokenKind::LBracket) || is(TokenKind::Dot) || is(TokenKind::LParen)) {
            if (is(TokenKind::LParen)) {
                auto paren = take();
                std::vector<ExprPtr> operands;
                operands.push_back(std::move(left));
                left = call_node(Expr::Kind::Invoke, paren, std::move(operands));
                continue;
            }
            if (match(TokenKind::Dot)) {
                auto field = expect(TokenKind::Identifier, "expected field name");
                std::vector<ExprPtr> base;
                base.push_back(std::move(left));
                left = node(Expr::Kind::Member, field, std::move(base));
                continue;
            }
            auto bracket = take();
            std::vector<ExprPtr> operands;
            operands.push_back(std::move(left));
            operands.push_back(is(TokenKind::Colon) ? nullptr : expression());
            if (match(TokenKind::Comma)) {
                std::vector<ExprPtr> indices;
                indices.push_back(std::move(operands[1]));
                while (!is(TokenKind::RBracket)) {
                    indices.push_back(expression());
                    if (indices.size() > 8)
                        fail("E2002", bracket.location, "array indexes support at most 8 axes");
                    if (!match(TokenKind::Comma))
                        break;
                }
                operands[1] = node(Expr::Kind::Tuple, bracket, std::move(indices));
            }
            if (match(TokenKind::Colon)) {
                operands.push_back(is(TokenKind::Colon) || is(TokenKind::RBracket) ? nullptr
                                                                                   : expression());
                operands.push_back(match(TokenKind::Colon)
                                       ? (is(TokenKind::RBracket) ? nullptr : expression())
                                       : nullptr);
                expect(TokenKind::RBracket, "expected ']' after slice");
                left = node(Expr::Kind::Slice, bracket, std::move(operands));
                continue;
            }
            expect(TokenKind::RBracket, "expected ']' after index");
            left = node(Expr::Kind::Index, bracket, std::move(operands));
        }
        while (current_precedence() >= minimum) {
            auto op = take();
            if (op.kind == TokenKind::Identifier && op.text == "is" && match(TokenKind::Not))
                op.text = "is not";
            if (op.kind == TokenKind::Not) {
                expect(TokenKind::In, "expected 'in' after comparison 'not'");
                op.text = "not in";
            }
            const int level = op.kind == TokenKind::Identifier ? 4 : precedence(op.kind);
            if (level == 4) {
                std::vector<ExprPtr> operands;
                std::vector<std::string> operators;
                operands.push_back(std::move(left));
                const auto first = op;
                for (;;) {
                    operators.push_back(op.text);
                    operands.push_back(expression(5));
                    if (current_precedence() != 4)
                        break;
                    op = take();
                    if (op.kind == TokenKind::Identifier && op.text == "is" &&
                        match(TokenKind::Not))
                        op.text = "is not";
                    if (op.kind == TokenKind::Not) {
                        expect(TokenKind::In, "expected 'in' after comparison 'not'");
                        op.text = "not in";
                    }
                    if (operators.size() >= 255)
                        fail("E2002", op.location,
                             "comparison chains support at most 255 operators");
                }
                left = node(Expr::Kind::CompareChain, first, std::move(operands));
                left->operators = std::move(operators);
                continue;
            }
            std::vector<ExprPtr> pair;
            pair.push_back(std::move(left));
            pair.push_back(expression(level + 1));
            left = node(Expr::Kind::Binary, op, std::move(pair));
        }
        if (minimum == 1 && conditional && match(TokenKind::If)) {
            const auto keyword = tokens.at(pos - 1);
            std::vector<ExprPtr> branches;
            branches.push_back(std::move(left));
            branches.push_back(expression(1, false));
            expect(TokenKind::Else, "expected 'else' in conditional expression");
            branches.push_back(expression());
            left = node(Expr::Kind::Conditional, keyword, std::move(branches));
        }
        --recursion;
        return left;
    }
    Block suite() {
        if (is(TokenKind::Equal))
            fail("E2001", peek().location, "expected ':' before indented block; use '==' to compare");
        if (is(TokenKind::If))
            fail("E2001", peek().location, "expected ':' before indented block; write 'elif' rather than 'else if'");
        expect(TokenKind::Colon, "expected ':' before indented block");
        expect(TokenKind::Newline, "expected a newline after ':'");
        expect(TokenKind::Indent, "expected an indented block (use 'pass' for an empty block)");
        Block result;
        while (!is(TokenKind::Dedent) && !is(TokenKind::Eof)) {
            result.push_back(statement(false));
            take_pending(result);
        }
        expect(TokenKind::Dedent, "expected end of indented block");
        return result;
    }
    void take_pending(Block& into) {
        for (auto& item : pending)
            into.push_back(std::move(item));
        pending.clear();
    }
    // A local name for the unpacked value that no identifier in the file can collide with.
    std::string hidden_name() {
        while (true) {
            auto name = "wood_unpack_" + std::to_string(hidden++);
            auto taken = std::any_of(tokens.begin(), tokens.end(), [&](const Token& token) {
                return token.kind == TokenKind::Identifier && token.text == name;
            });
            if (!taken)
                return name;
        }
    }
    std::vector<Token> unpack_names(Token first, TokenKind stop) {
        std::vector<Token> names{std::move(first)};
        while (match(TokenKind::Comma) && !is(stop)) {
            auto name = expect(TokenKind::Identifier, "expected a name to unpack into");
            for (const auto& seen : names)
                if (seen.text == name.text)
                    fail("E2001", name.location, "'" + name.text + "' appears twice in the names to unpack into");
            names.push_back(std::move(name));
            if (names.size() > 255)
                fail("E2002", names.back().location, "unpacking supports at most 255 names");
        }
        return names;
    }
    // name = hidden[i] for each target, positioned at the target so errors point at it.
    Block unpack_into(const std::vector<Token>& names, const std::string& hidden_variable) {
        Block result;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto& name = names[i];
            std::vector<ExprPtr> operands;
            operands.push_back(node(Expr::Kind::Name, Token{TokenKind::Identifier, hidden_variable, name.location}));
            operands.push_back(node(Expr::Kind::Integer, Token{TokenKind::Integer, std::to_string(i), name.location}));
            auto assignment = std::make_unique<Stmt>(Stmt::Kind::Assign, name.location);
            assignment->name = name.text;
            assignment->expressions.push_back(node(Expr::Kind::Index, name, std::move(operands)));
            result.push_back(std::move(assignment));
        }
        return result;
    }
    std::unique_ptr<Stmt> conditional(Token token, bool chained = false) {
        if (++conditional_depth > 256)
            fail("E2002", token.location, "conditional nesting exceeds 256 levels");
        auto result = std::make_unique<Stmt>(Stmt::Kind::If, token.location);
        result->expressions.push_back(expression());
        auto body = suite();
        const auto present = none_check(*result->expressions[0], true);
        if (present)
            body.insert(body.begin(), narrowing(*present, false));
        result->body = std::move(body);
        const auto absent = none_check(*result->expressions[0], false);
        if (is(TokenKind::Elif))
            result->otherwise.push_back(conditional(take(), true));
        else if (match(TokenKind::Else)) {
            auto otherwise = suite();
            if (absent)
                otherwise.insert(otherwise.begin(), narrowing(*absent, false));
            result->otherwise = std::move(otherwise);
        } else if (absent && !chained)
            pending.push_back(narrowing(*absent, true));
        --conditional_depth;
        return result;
    }
    // The name in `name is not None` (positive, also as the first operand of `and`) or `name is None`.
    std::optional<Token> none_check(const Expr& expr, bool positive) const {
        if (positive && expr.kind == Expr::Kind::Binary && expr.text == "and")
            return none_check(*expr.children[0], true);
        if (expr.kind != Expr::Kind::CompareChain || expr.operators.size() != 1 ||
            expr.operators[0] != (positive ? "is not" : "is"))
            return std::nullopt;
        const auto& left = *expr.children[0];
        const auto& right = *expr.children[1];
        if (left.kind != Expr::Kind::Name || left.text == "None" || right.kind != Expr::Kind::Name ||
            right.text != "None")
            return std::nullopt;
        return Token{TokenKind::Identifier, left.text, left.location};
    }
    // name = unwrap(name), marked with expression_body so the checker narrows name to its payload,
    // or turns this into pass when name is not optional. relative 1 means it follows an if whose
    // body must not fall through, as in: if name is None: return
    std::unique_ptr<Stmt> narrowing(const Token& name, bool after) {
        std::vector<ExprPtr> arguments;
        arguments.push_back(node(Expr::Kind::Name, name));
        auto call = node(Expr::Kind::Call, Token{TokenKind::Identifier, "unwrap", name.location},
                         std::move(arguments));
        call->keywords = {""};
        auto result = std::make_unique<Stmt>(Stmt::Kind::Assign, name.location);
        result->name = name.text;
        result->expression_body = true;
        result->relative = after ? 1 : 0;
        result->expressions.push_back(std::move(call));
        return result;
    }
    std::unique_ptr<Stmt> statement(bool top_level, bool method = false) {
        auto token = peek();
        if (is(TokenKind::Identifier) && token.text == "record" &&
            tokens.at(pos + 1).kind == TokenKind::Identifier) {
            take();
            if (!top_level)
                fail("E2012", token.location, "records must be declared at file scope");
            auto result = std::make_unique<Stmt>(Stmt::Kind::Record, token.location);
            result->name = expect(TokenKind::Identifier, "expected record name").text;
            if (match(TokenKind::LBracket)) {
                do {
                    result->type_parameters.push_back(
                        expect(TokenKind::Identifier, "expected type parameter").text);
                    if (result->type_parameters.size() > 32)
                        fail("E2002", token.location, "records support at most 32 type parameters");
                } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
                expect(TokenKind::RBracket, "expected ']' after type parameters");
            }
            expect(TokenKind::Colon, "expected ':' after record name");
            expect(TokenKind::Newline, "expected newline after record name");
            expect(TokenKind::Indent, "expected record fields or pass");
            if (match(TokenKind::Pass))
                expect(TokenKind::Newline, "expected newline after pass");
            else
                while (!is(TokenKind::Dedent) && !is(TokenKind::Eof)) {
                    if (is(TokenKind::Fn) || (is(TokenKind::Identifier) && peek().text == "def" &&
                                              tokens.at(pos + 1).kind == TokenKind::Identifier)) {
                        result->body.push_back(statement(true, true));
                        continue;
                    }
                    auto field = expect(TokenKind::Identifier, "expected field name");
                    expect(TokenKind::Colon, "expected ':' and field type");
                    auto type = annotation();
                    result->parameters.push_back({field.text, type, field.location});
                    if (result->parameters.size() > 255)
                        fail("E2002", token.location, "records support at most 255 fields");
                    expect(TokenKind::Newline, "expected end of field declaration");
                }
            expect(TokenKind::Dedent, "expected end of record");
            return result;
        }
        if (is(TokenKind::Import) || is(TokenKind::From)) {
            const bool from = take().kind == TokenKind::From;
            if (!top_level)
                fail("E2011", token.location, "imports must be at file scope");
            auto result = std::make_unique<Stmt>(Stmt::Kind::Import, token.location);
            if (from)
                while (match(TokenKind::Dot))
                    ++result->relative;
            result->module = expect(TokenKind::Identifier, "expected module name").text;
            while (match(TokenKind::Dot))
                result->module +=
                    "." + expect(TokenKind::Identifier, "expected module name after '.'").text;
            if (from) {
                expect(TokenKind::Import, "expected 'import' after module");
                do {
                    auto name = expect(TokenKind::Identifier, "expected imported function name");
                    auto alias = name.text;
                    if (match(TokenKind::As))
                        alias = expect(TokenKind::Identifier, "expected import alias").text;
                    result->imports.push_back({name.text, alias, name.location});
                } while (match(TokenKind::Comma));
            } else {
                result->name = result->module;
                if (match(TokenKind::As))
                    result->name = expect(TokenKind::Identifier, "expected module alias").text;
            }
            expect(TokenKind::Newline, "expected end of import");
            return result;
        }
        // Contextual: existing variables and functions named `with` still work.
        bool scoped = false;
        if (is(TokenKind::Identifier) && token.text == "with")
            for (auto cursor = pos + 1; tokens.at(cursor).kind != TokenKind::Newline &&
                                        tokens.at(cursor).kind != TokenKind::Eof;
                 ++cursor)
                if (tokens.at(cursor).kind == TokenKind::As) {
                    scoped = true;
                    break;
                }
        if (scoped) {
            take();
            auto result = std::make_unique<Stmt>(Stmt::Kind::With, token.location);
            result->expressions.push_back(expression());
            expect(TokenKind::As, "expected 'as' and stream binding");
            result->name = expect(TokenKind::Identifier, "expected stream binding").text;
            result->body = suite();
            return result;
        }
        if (match(TokenKind::Try)) {
            auto result = std::make_unique<Stmt>(Stmt::Kind::Try, token.location);
            result->body = suite();
            // 'finally' is a word only here, followed by ':' and a block, so it stays a valid name.
            const auto finally_clause = [&] {
                return is(TokenKind::Identifier) && peek().text == "finally" &&
                       pos + 2 < tokens.size() && tokens.at(pos + 1).kind == TokenKind::Colon &&
                       tokens.at(pos + 2).kind == TokenKind::Newline;
            };
            if (!finally_clause()) {
                expect(TokenKind::Except, "expected 'except name:' or 'finally:' after try block");
                result->name =
                    expect(TokenKind::Identifier, "expected error binding after 'except'").text;
                handlers.push_back(result->name);
                result->otherwise = suite();
                handlers.pop_back();
                if (!finally_clause())
                    return result;
            }
            auto wrapper = std::make_unique<Stmt>(Stmt::Kind::Finally, token.location);
            take();
            wrapper->otherwise = suite();
            if (result->name.empty())
                wrapper->body = std::move(result->body);
            else
                wrapper->body.push_back(std::move(result));
            return wrapper;
        }
        const bool external = is(TokenKind::Identifier) && token.text == "extern" &&
                              (tokens.at(pos + 1).kind == TokenKind::Fn ||
                               (tokens.at(pos + 1).kind == TokenKind::Identifier &&
                                tokens.at(pos + 1).text == "def" &&
                                tokens.at(pos + 2).kind == TokenKind::Identifier));
        if (external)
            take();
        const bool python_function = is(TokenKind::Identifier) && peek().text == "def" &&
                                     tokens.at(pos + 1).kind == TokenKind::Identifier;
        if (match(TokenKind::Fn) || (python_function && (take(), true))) {
            if (!top_level && external)
                fail("E2006", token.location, "external functions must be declared at file scope");
            auto result = std::make_unique<Stmt>(Stmt::Kind::Function, token.location);
            result->external = external;
            result->method = method;
            result->name = expect(TokenKind::Identifier, "expected function name").text;
            if (is(TokenKind::LBracket) && tokens.at(pos + 1).kind == TokenKind::Identifier &&
                tokens.at(pos + 1).text == "mut") {
                take();
                if (top_level)
                    fail("E3028", token.location,
                         "owned mutable captures belong to local functions");
                do {
                    auto mut = expect(TokenKind::Identifier, "expected mut capture");
                    if (mut.text != "mut")
                        fail("E3028", mut.location, "each mutable capture requires mut");
                    result->mutable_captures.push_back(
                        expect(TokenKind::Identifier, "expected capture name").text);
                } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
                expect(TokenKind::RBracket, "expected ']' after captures");
            }
            if (match(TokenKind::LBracket)) {
                do {
                    result->type_parameters.push_back(
                        expect(TokenKind::Identifier, "expected type parameter").text);
                    if (result->type_parameters.size() > 32)
                        fail("E2002", token.location,
                             "functions support at most 32 type parameters");
                } while (match(TokenKind::Comma) && !is(TokenKind::RBracket));
                expect(TokenKind::RBracket, "expected ']' after type parameters");
                if (external)
                    fail("E3023", token.location, "external functions cannot be generic");
            }
            expect(TokenKind::LParen, "expected '(' after function name");
            if (method) {
                if (is(TokenKind::Identifier) && peek().text == "mut") {
                    take();
                    result->mutating = true;
                }
                const auto receiver = expect(TokenKind::Identifier,
                                             "method requires self or mut self as first parameter");
                if (receiver.text != "self")
                    fail("E3027", receiver.location,
                         "method requires self or mut self as first parameter");
                Parameter parameter{"self", Type::Unknown, receiver.location};
                parameter.mode = Parameter::PositionalOnly;
                result->parameters.push_back(std::move(parameter));
                if (!is(TokenKind::RParen))
                    expect(TokenKind::Comma, "expected ',' after self");
            }
            bool keyword_only = false, slash = false, default_seen = false, star_pending = false;
            while (!is(TokenKind::RParen)) {
                if (match(TokenKind::Slash)) {
                    if (slash || keyword_only || result->parameters.empty())
                        fail("E2001", peek().location, "invalid positional-only marker");
                    slash = true;
                    for (auto& parameter : result->parameters)
                        parameter.mode = Parameter::PositionalOnly;
                } else if (match(TokenKind::Star)) {
                    if (keyword_only)
                        fail("E2001", peek().location, "duplicate keyword-only marker");
                    keyword_only = true;
                    star_pending = true;
                } else {
                    bool mutable_borrow = false;
                    if (is(TokenKind::Identifier) && peek().text == "mut" &&
                        tokens.at(pos + 1).kind == TokenKind::Identifier) {
                        if (!external)
                            fail("E3023", peek().location,
                                 "mut parameters are limited to native array imports");
                        take();
                        mutable_borrow = true;
                    }
                    auto name = expect(TokenKind::Identifier, "expected parameter name");
                    expect(TokenKind::Colon, "expected ':' and parameter type");
                    Parameter parameter{name.text, annotation(), name.location};
                    parameter.mutable_borrow = mutable_borrow;
                    parameter.mode = keyword_only ? Parameter::KeywordOnly : Parameter::Positional;
                    if (match(TokenKind::Equal)) {
                        parameter.default_value = expression();
                        if (!keyword_only)
                            default_seen = true;
                    } else if (!keyword_only && default_seen)
                        fail("E2001", name.location,
                             "required positional parameter follows default");
                    result->parameters.push_back(std::move(parameter));
                    star_pending = false;
                    if (result->parameters.size() > 255)
                        fail("E2002", name.location, "functions support at most 255 parameters");
                }
                if (!match(TokenKind::Comma))
                    break;
            }
            if (star_pending)
                fail("E2001", peek().location, "'*' requires a keyword-only parameter");
            expect(TokenKind::RParen, "expected ')' after parameters");
            if (match(TokenKind::Arrow))
                result->return_type = annotation(true);
            else if (!external)
                result->inferred_return = true;
            if (external)
                expect(TokenKind::Newline, "expected end of external function declaration");
            else {
                // A bare raise never reaches an except block outside its own function.
                auto outer = std::exchange(handlers, {});
                result->body = suite();
                handlers = std::move(outer);
            }
            if (result->inferred_return) {
                // Nested declarations/closure expressions own their returns.
                const auto returns_value = [](const auto& visit, const Block& body) -> bool {
                    for (const auto& stmt : body) {
                        if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record)
                            continue;
                        if ((stmt->kind == Stmt::Kind::Return && !stmt->expressions.empty()) ||
                            visit(visit, stmt->body) || visit(visit, stmt->otherwise))
                            return true;
                    }
                    return false;
                };
                if (returns_value(returns_value, result->body))
                    result->return_type = Type::Unknown;
            }
            if (!top_level) {
                if (!result->type_parameters.empty())
                    fail("E3028", token.location, "local functions cannot declare type parameters");
                auto closure = node(Expr::Kind::Closure, token);
                auto assignment = std::make_unique<Stmt>(Stmt::Kind::Assign, token.location);
                assignment->name = result->name;
                closure->function = std::move(result);
                assignment->expressions.push_back(std::move(closure));
                return assignment;
            }
            return result;
        }
        if (match(TokenKind::If))
            return conditional(token);
        if (match(TokenKind::While)) {
            auto result = std::make_unique<Stmt>(Stmt::Kind::While, token.location);
            result->expressions.push_back(expression());
            result->body = suite();
            return result;
        }
        if (match(TokenKind::For)) {
            auto result = std::make_unique<Stmt>(Stmt::Kind::For, token.location);
            auto first = expect(TokenKind::Identifier, "expected loop variable");
            result->name = first.text;
            Block prelude;
            if (is(TokenKind::Comma)) {
                auto names = unpack_names(first, TokenKind::In);
                result->name = hidden_name();
                result->relative = names.size();
                prelude = unpack_into(names, result->name);
            }
            expect(TokenKind::In, "expected 'in' after loop variable");
            result->expressions.push_back(expression());
            for (auto& item : suite())
                prelude.push_back(std::move(item));
            result->body = std::move(prelude);
            return result;
        }
        auto result = std::make_unique<Stmt>(Stmt::Kind::Expression, token.location);
        if (match(TokenKind::Raise)) {
            result->kind = Stmt::Kind::Raise;
            if (!is(TokenKind::Newline))
                result->expressions.push_back(expression());
            else if (!handlers.empty()) {
                // A bare raise re-raises the error of the innermost except block. The checker binds
                // the name to that handler's error even where another variable shadows it.
                result->expressions.push_back(
                    node(Expr::Kind::Name, Token{TokenKind::Identifier, handlers.back(), token.location}));
                result->relative = 1;
            }
        } else if (match(TokenKind::Return)) {
            result->kind = Stmt::Kind::Return;
            if (!is(TokenKind::Newline))
                result->expressions.push_back(expression());
        } else if (match(TokenKind::Break))
            result->kind = Stmt::Kind::Break;
        else if (match(TokenKind::Continue))
            result->kind = Stmt::Kind::Continue;
        else if (match(TokenKind::Pass))
            result->kind = Stmt::Kind::Pass;
        else if (is(TokenKind::Identifier) && tokens.at(pos + 1).kind == TokenKind::Comma) {
            auto names = unpack_names(take(), TokenKind::Equal);
            expect(TokenKind::Equal, "expected '=' after the names to unpack into");
            auto value = expression();
            if (is(TokenKind::Comma)) {
                std::vector<ExprPtr> elements;
                elements.push_back(std::move(value));
                while (match(TokenKind::Comma) && !is(TokenKind::Newline)) {
                    elements.push_back(expression());
                    if (elements.size() > 255)
                        fail("E2002", token.location, "tuples support at most 255 elements");
                }
                value = node(Expr::Kind::Tuple, token, std::move(elements));
            }
            // relative carries the required tuple size for the checker; it is otherwise only used by imports.
            result->kind = Stmt::Kind::Assign;
            result->name = hidden_name();
            result->relative = names.size();
            result->expressions.push_back(std::move(value));
            for (auto& item : unpack_into(names, result->name))
                pending.push_back(std::move(item));
        } else if (is(TokenKind::Identifier) && (tokens.at(pos + 1).kind == TokenKind::Equal ||
                                               tokens.at(pos + 1).kind == TokenKind::Colon)) {
            result->kind = Stmt::Kind::Assign;
            result->name = take().text;
            if (match(TokenKind::Colon))
                result->annotation = annotation();
            expect(TokenKind::Equal, "expected '=' and initializer");
            result->expressions.push_back(expression());
        } else {
            result->expressions.push_back(expression());
            if (is(TokenKind::Augment)) {
                result->kind = Stmt::Kind::AugAssign;
                result->name = take().text;
                result->expressions.push_back(expression());
            } else if (match(TokenKind::Equal)) {
                if (result->expressions[0]->kind != Expr::Kind::Index &&
                    result->expressions[0]->kind != Expr::Kind::Member)
                    fail("E2010", token.location, "assignment requires a variable or list index");
                result->kind = Stmt::Kind::IndexAssign;
                result->expressions.push_back(expression());
            }
        }
        expect(TokenKind::Newline, "expected end of statement");
        return result;
    }

  public:
    explicit Parser(const std::vector<Token>& input) : tokens(input) {}
    Program run() {
        Program program;
        while (!is(TokenKind::Eof)) {
            program.statements.push_back(statement(true));
            take_pending(program.statements);
        }
        return program;
    }
};
} // namespace
Program parse(const std::vector<Token>& tokens) {
    return Parser(tokens).run();
}
} // namespace wood
