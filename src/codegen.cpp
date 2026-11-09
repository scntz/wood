#include "wood/builtins.hpp"
#include "wood/compiler.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <utility>

namespace wood {
namespace {
std::string cpp_type(const Type& type) {
    switch (type.kind) {
    case Type::Int:
        return "wd::Int";
    case Type::Float:
        return "double";
    case Type::Bool:
        return "bool";
    case Type::Str:
        return "std::string";
    case Type::Void:
        return "void";
    case Type::Error:
        return "wd::Failure";
    case Type::List:
        return "wd::List<" + cpp_type(*type.element) + ">";
    case Type::Array:
        return "wd::Array<" + cpp_type(*type.element) + ">";
    case Type::Dict:
        return "wd::Dict<" + cpp_type(*type.key) + ", " + cpp_type(*type.element) + ">";
    case Type::Record:
        return "wd::wd_r" + std::to_string(type.declaration->symbol);
    case Type::Json:
        return "wd::Json";
    case Type::Stream:
        return "wd::Stream";
    case Type::Database:
        return "wd::Database";
    case Type::Statement:
        return "wd::Statement";
    case Type::Transaction:
        return "wd::Transaction";
    case Type::TlsContext:
        return "wd::TlsContext";
    case Type::Task:
        return "wd::Task";
    case Type::None:
        return "wd::None";
    case Type::Optional:
        return "std::optional<" + cpp_type(*type.element) + ">";
    case Type::Tuple: {
        std::string result = "std::tuple<";
        for (std::size_t i = 0; i < type.parameters.size(); ++i) {
            if (i)
                result += ", ";
            result += cpp_type(type.parameters[i]);
        }
        return result + ">";
    }
    case Type::Closure:
        return "wd::wd_c" + std::to_string(type.declaration->symbol);
    case Type::Range:
        return "wd::Range";
    case Type::Step:
        return "wd::Step<" + cpp_type(*type.element) + ">";
    case Type::Cursor:
        return "wd::Cursor<" + cpp_type(*type.element) + ">";
    case Type::Callable:
    case Type::Function: {
        std::string result =
            std::string(type.kind == Type::Callable ? "wd::Callable<" : "wd::Function<") +
            cpp_type(*type.element);
        for (const auto& parameter : type.parameters)
            result += ", " + cpp_type(parameter);
        return result + ">";
    }
    default:
        throw std::logic_error("code generation requires a typed program");
    }
}
std::string cpp_literal(std::string_view text) {
    // Fixed-width octal escapes preserve every byte without C++ escape ambiguity.
    std::string out = "\"";
    for (unsigned char c : text) {
        out += '\\';
        out += static_cast<char>('0' + (c >> 6));
        out += static_cast<char>('0' + ((c >> 3) & 7));
        out += static_cast<char>('0' + (c & 7));
    }
    return out + '"';
}
std::string variable(std::uint64_t id) {
    return "wd_v" + std::to_string(id);
}
std::string function(std::uint64_t id) {
    return "wd_f" + std::to_string(id);
}
class Generator {
    std::ostringstream out;
    std::vector<std::string> files;
    std::uint64_t temporary = 0;
    unsigned indent = 0;
    bool json_runtime;
    std::set<std::uint64_t> written;
    // A finally block being emitted around the current statement. Return, break and continue that
    // leave it record how they left in flow (1, 2, 3), keep a returned value in value, and jump to
    // label, where the finally block runs and then leaves the same way. uses collects the ways seen.
    struct Final {
        std::string label, flow, value;
        unsigned loops;
        unsigned uses = 0;
    };
    std::vector<Final> finals;
    unsigned loops = 0;
    Type returning = Type::Void;
    void leave(unsigned how, const std::string& value) {
        auto& final = finals.back();
        final.uses |= 1u << how;
        line("{ " + (value.empty() ? "" : final.value + ".emplace(" + value + "); ") + final.flow +
             " = " + std::to_string(how) + "; goto " + final.label + "; }");
    }
    bool leaves_loop() const {
        return !finals.empty() && finals.back().loops == loops;
    }
    void line(const std::string& text) {
        out << std::string(indent * 4, ' ') << text << '\n';
    }
    std::string fresh() {
        return "wd_t" + std::to_string(++temporary);
    }
    bool stable_string(const Expr& expr) const {
        // Local strings cannot be reached by another call. Record fields can
        // now change through mut self, so they require argument snapshots.
        return expr.kind == Expr::Kind::Name || expr.kind == Expr::Kind::String;
    }
    bool cheap_callable_copy(const Type& type) const {
        if (type.kind == Type::Function)
            return true;
        if (type.kind != Type::Closure || type.declaration->captures.size() > 2)
            return false;
        return std::all_of(type.declaration->captures.begin(), type.declaration->captures.end(),
                           [](const auto& capture) {
                               const auto kind = capture.type.kind;
                               return kind == Type::Int || kind == Type::Float ||
                                      kind == Type::Bool || kind == Type::None ||
                                      kind == Type::Function;
                           });
    }
    std::string site(Location at) const {
        return "wd::Site{" + cpp_literal(files.at(at.source)) + ", " + std::to_string(at.line) +
               ", " + std::to_string(at.column) + "}";
    }
    // Evaluate index expressions into temporaries before taking any mutable
    // element reference. A later argument may resize the same collection.
    std::string mutation_path(const Expr& expr, std::string& prefix) {
        if (expr.kind == Expr::Kind::Name)
            return variable(expr.symbol);
        auto root = mutation_path(*expr.children[0], prefix);
        if (expr.kind == Expr::Kind::Member)
            return root + ".wd_m" + std::to_string(expr.symbol);
        const auto index = fresh();
        prefix += "auto " + index + " = " + expression(*expr.children[1]) + "; ";
        return root + ".mut_at(" + index + ", " + site(expr.location) + ")";
    }
    bool structural_effect(const Expr& expr) const {
        if (expr.kind == Expr::Kind::Invoke && !expr.children.empty()) {
            const auto& target = expr.children[0]->type;
            if (target.kind == Type::Callable ||
                (target.kind == Type::Closure && target.declaration->mutating))
                return true;
        }
        if (expr.kind == Expr::Kind::Call) {
            if (expr.callee &&
                std::any_of(expr.callee->parameters.begin(), expr.callee->parameters.end(),
                            [](const auto& p) { return p.mutable_borrow; }))
                return true;
            if (expr.callee && expr.callee->method && expr.callee->mutating)
                return true;
            if (!expr.symbol)
                if (const auto* builtin = find_builtin(expr.text); builtin && builtin->mutates)
                    return true;
        }
        return std::any_of(expr.children.begin(), expr.children.end(),
                           [&](const auto& child) { return child && structural_effect(*child); });
    }
    bool stable_path(const Expr& expr) const {
        if (expr.kind == Expr::Kind::Name)
            return true;
        if (expr.kind == Expr::Kind::Member)
            return stable_path(*expr.children[0]);
        return expr.kind == Expr::Kind::Index && expr.children[0]->type.kind == Type::Tuple &&
               stable_path(*expr.children[0]);
    }
    bool borrow_lifetime(const Expr& expr) const {
        // std::get returns a reference: binding that reference does not extend
        // a temporary tuple's lifetime. Own its selected value before the
        // full-expression ends. Other generated read helpers return values.
        if (expr.kind == Expr::Kind::Index && expr.children[0]->type.kind == Type::Tuple)
            return stable_path(*expr.children[0]);
        if (expr.kind == Expr::Kind::Member)
            return borrow_lifetime(*expr.children[0]);
        if (expr.kind == Expr::Kind::Unary && expr.text == "+" && expr.type.kind == Type::Array)
            return borrow_lifetime(*expr.children[0]);
        return true;
    }
    // These operands are read only through a bounded generated expression. Do
    // not borrow across a later mutation, or turn a proven last-use move into
    // a const reference. Prvalues keep ownership for buffer reuse.
    bool borrow_operand(const Expr& expr, bool safe) const {
        if (!safe || expr.move_value || !borrow_lifetime(expr) ||
            (expr.kind != Expr::Kind::Name && expr.kind != Expr::Kind::Member) ||
            !stable_path(expr))
            return false;
        const auto kind = expr.type.kind;
        return kind == Type::Str || kind == Type::List || kind == Type::Dict ||
               kind == Type::Array || kind == Type::Tuple || kind == Type::Optional ||
               kind == Type::Record || kind == Type::Step;
    }
    std::string converted(const Expr& expr, const Type& target) {
        auto value = expression(expr);
        if (target.kind == Type::Callable && expr.type != target)
            return cpp_type(target) + "{" + value + "}";
        if (target.kind == Type::Optional && expr.type != target) {
            if (expr.type == Type::None)
                return cpp_type(target) + "{std::nullopt}";
            if (*target.element == Type::Float && expr.type == Type::Int)
                value = "static_cast<double>(" + value + ")";
            return cpp_type(target) + "{" + value + "}";
        }
        if (target == Type::Float && expr.type == Type::Int)
            return "static_cast<double>(" + value + ")";
        return value;
    }
    std::string operation(const std::string& op, const Type& result, const std::string& left,
                          const std::string& right, Location at) {
        if (result.kind == Type::Array && (op == "+" || op == "-" || op == "*" || op == "/"))
            return std::string("wd::array_") +
                   (op == "+"   ? "add"
                    : op == "-" ? "sub"
                    : op == "*" ? "mul"
                                : "div") +
                   "(std::move(" + left + "), std::move(" + right + "), " + site(at) + ")";
        if (op == "==" || op == "!=" || op == "is" || op == "is not")
            return std::string(op == "!=" || op == "is not" ? "!" : "") + "wd::equal(" + left +
                   ", " + right + ")";
        if (op == "in" || op == "not in")
            return (op == "not in" ? "!" : "") + std::string("wd::contains(") + right + ", " +
                   left + ", " + site(at) + ")";
        if (op == "+" && result == Type::Str)
            return "wd::concat(std::move(" + left + "), " + right + ", " + site(at) + ")";
        if (op == "+" && result.kind == Type::List)
            return "wd::concat(" + left + ", " + right + ", " + site(at) + ")";
        if (op == "*" && (result == Type::Str || result.kind == Type::List))
            return "wd::repeat(" + left + ", " + right + ", " + site(at) + ")";
        if (op == "<<" || op == ">>")
            return std::string(op == "<<" ? "wd::shift_left(" : "wd::shift_right(") + left + ", " +
                   right + ", " + site(at) + ")";
        if (op == "/")
            return "wd::div(static_cast<double>(" + left + "), static_cast<double>(" + right +
                   "), " + site(at) + ")";
        if (op == "//" || op == "%")
            return std::string(op == "//" ? "wd::floor_div(" : "wd::mod(") + left + ", " + right +
                   ", " + site(at) + ")";
        if (result == Type::Int && (op == "+" || op == "-" || op == "*")) {
            const std::string helper = op == "+" ? "add" : op == "-" ? "sub" : "mul";
            return "wd::" + helper + "(" + left + ", " + right + ", " + site(at) + ")";
        }
        return left + " " + op + " " + right;
    }
    // Each subscript is evaluated once. Read snapshots never retain element
    // references across RHS evaluation; stores revalidate the captured path.
    std::pair<std::string, std::string> captured_path(const Expr& expr, std::string& prefix) {
        if (expr.kind == Expr::Kind::Name)
            return {variable(expr.symbol), variable(expr.symbol)};
        auto [read, write] = captured_path(*expr.children[0], prefix);
        if (expr.kind == Expr::Kind::Member) {
            const auto field = ".wd_m" + std::to_string(expr.symbol);
            return {read + field, write + field};
        }
        const auto index = fresh(), value = fresh();
        prefix += "auto " + index + " = " + expression(*expr.children[1]) + "; ";
        prefix += "auto " + value + " = wd::at(" + read + ", " + index + ", " +
                  site(expr.location) + "); ";
        return {value, write + ".mut_at(" + index + ", " + site(expr.location) + ")"};
    }
    // Ordinary loops get a temporary scope. Comprehensions already provide it;
    // use one block per clause to stay within native compiler nesting limits.
    std::string iteration(const Expr& range, std::uint64_t symbol, Type element, Location at,
                          bool scoped = true) {
        std::string prefix = scoped ? "{ " : "";
        if (range.kind == Expr::Kind::Call && range.text == "range") {
            std::vector<std::string> args;
            for (const auto& arg : range.children) {
                auto name = fresh();
                prefix += "auto " + name + " = " + expression(*arg) + "; ";
                args.push_back(name);
            }
            if (args.size() == 1)
                args.insert(args.begin(), "wd::Int{0}");
            if (args.size() == 2)
                args.push_back("wd::Int{1}");
            const auto cursor = fresh();
            int direction = 1;
            if (range.children.size() == 3) {
                const auto& step = *range.children[2];
                direction = step.kind == Expr::Kind::Integer && step.text == "1" ? 1 : 0;
                if (step.kind == Expr::Kind::Unary && step.text == "-" &&
                    step.children[0]->kind == Expr::Kind::Integer && step.children[0]->text == "1")
                    direction = -1;
                prefix += "static_cast<void>(" + args[2] + "); ";
            }
            if (direction != 0) {
                // A unit-stride loop reaches its exclusive bound before an
                // increment could overflow, even at INT64_MIN/MAX. Keep the
                // user's assignable binding separate from native progression.
                // A while keeps one native block per comprehension clause.
                // A for-declaration's extra scope exceeds MSVC's block limit
                // for Wood's supported 64-clause comprehensions.
                prefix += "wd::Int " + cursor + " = " + args[0] + "; while (" + cursor +
                          (direction > 0 ? " < " : " > ") + args[1] + ") { ";
                return prefix + "wd::Int " + variable(symbol) + " = " + cursor + "; " +
                       (direction > 0 ? "++" : "--") + cursor + "; ";
            }
            prefix += "wd::Range " + cursor + "(" + args[0] + ", " + args[1] + ", " + args[2] +
                      ", " + site(at) + "); while (" + cursor + ".has_next()) { ";
            return prefix + "wd::Int " + variable(symbol) + " = " + cursor + ".next(); ";
        }
        if (range.type.kind == Type::Cursor || range.type.kind == Type::Record ||
            range.type == Type::Range) {
            const auto cursor = fresh(), step = fresh();
            prefix += "auto " + cursor + " = " + expression(range) + "; ";
            const auto call =
                range.iteration_next ? function(range.iteration_next->symbol) + "(" + cursor + ")"
                : range.type == Type::Range ? "wd::next(" + cursor + ", " + site(at) + ")"
                                            : cursor + ".next(" + site(at) + ")";
            prefix +=
                "while (true) { auto " + step + " = " + call + "; if (!" + step + ".value) break; ";
            return prefix + cpp_type(element) + " " + variable(symbol) + " = std::move(*" + step +
                   ".value); ";
        }
        const auto snapshot = fresh(), index = fresh();
        prefix += "auto " + snapshot + " = " +
                  (range.type.kind == Type::Dict ? "wd::keys(" + expression(range) + ")"
                                                 : expression(range)) +
                  "; ";
        if (scoped)
            prefix += "for (wd::Int " + index + " = 0; " + index + " < wd::len(" + snapshot + ", " +
                      site(at) + "); ++" + index + ") { ";
        else
            prefix += "wd::Int " + index + " = 0; while (" + index + " < wd::len(" + snapshot +
                      ", " + site(at) + ")) { ";
        const bool read = range.type.kind == Type::List && element == Type::Str &&
                          !written.contains(symbol);
        prefix += (read ? "const auto&" : cpp_type(element)) + " " + variable(symbol) + " = " +
                  (read ? "wd_read_iteration(" + snapshot + ", " + index
                   : range.type.kind == Type::Array ? snapshot + ".flat_checked(" + index
                                                   : "wd::at(" + snapshot + ", " + index) +
                  ", " + site(at) + "); ";
        if (!scoped)
            prefix += "++" + index + "; ";
        return prefix;
    }
    std::string invoke_expression(const Expr& expr) {
        const auto target = fresh();
        const auto& type = expr.children[0]->type;
        const bool mutable_target = type.kind == Type::Callable ||
                                    (type.kind == Type::Closure && type.declaration->mutating);
        const Expr* root = expr.children[0].get();
        while (root->kind == Expr::Kind::Member || root->kind == Expr::Kind::Index)
            root = root->children[0].get();
        const bool rooted = root->kind == Expr::Kind::Name && !root->refers_function;
        const bool borrow =
            // Tiny scalar snapshots avoid introducing an alias into the hot call path.
            // Owned string/container captures benefit from borrowing stable targets instead.
            !mutable_target && !cheap_callable_copy(type) && borrow_lifetime(*expr.children[0]) &&
            std::none_of(expr.children.begin() + 1, expr.children.end(),
                         [&](const auto& child) { return structural_effect(*child); });
        std::string prefix = "([&]() -> " + cpp_type(expr.type) + " { ";
        std::string callable = target;
        if (mutable_target && rooted) {
            callable = mutation_path(*expr.children[0], prefix);
            prefix += "static_cast<void>(" + callable + "); ";
        } else {
            prefix += std::string(borrow ? "const auto& " : "auto ") + target + " = " +
                      expression(*expr.children[0]) + "; ";
        }
        std::vector<std::string> arguments(type.parameters.size());
        for (std::size_t i = 1; i < expr.children.size(); ++i) {
            const auto name = fresh();
            const auto parameter = expr.argument_order.empty() ? i - 1 : expr.argument_order[i - 1];
            prefix += "auto " + name + " = " +
                      converted(*expr.children[i], type.parameters[parameter]) + "; ";
            arguments[parameter] = "std::move(" + name + ")";
        }
        std::string args;
        for (const auto& argument : arguments) {
            if (!args.empty())
                args += ", ";
            args += argument;
        }
        return prefix + "return " + callable + "(" + args + "); }())";
    }
    std::string call_expression(const Expr& expr) {
        if (!expr.symbol && expr.text == "finished")
            return cpp_type(expr.type) + "{}";
        std::vector<std::string> args(expr.callee ? expr.callee->parameters.size() : 0);
        std::string prefix = "([&]() -> " + cpp_type(expr.type) + " { ";
        const auto* builtin = expr.symbol ? nullptr : find_builtin(expr.text);
        std::string target_path;
        std::vector<std::pair<std::size_t, std::string>> borrowed_paths;
        for (std::size_t i = 0; i < expr.children.size(); ++i) {
            const auto& child = expr.children[i];
            if (expr.callee && expr.callee->parameters[expr.argument_order[i]].mutable_borrow) {
                auto path = mutation_path(*child, prefix);
                prefix += "static_cast<void>(" + path + "); ";
                borrowed_paths.emplace_back(expr.argument_order[i], std::move(path));
                continue;
            }
            if (expr.callee && expr.callee->method && expr.callee->mutating && i == 0) {
                target_path = mutation_path(*child, prefix);
                // Validate before arguments, retain no reference across them,
                // and reacquire the captured path immediately before entry.
                prefix += "static_cast<void>(" + target_path + "); ";
                args[0] = target_path;
                continue;
            }
            if (builtin && builtin->mutates && i == 0) {
                target_path = mutation_path(*child, prefix);
                args.push_back(target_path);
                continue;
            }
            auto name = fresh();
            auto value =
                expr.callee
                    ? converted(*child, expr.callee->parameters[expr.argument_order[i]].type)
                    : expression(*child);
            if (builtin && expr.text == "yielded")
                value = cpp_type(*expr.type.element) + "(" + converted(*child, *expr.type.element) +
                        ")";
            if (builtin && expr.text == "append" && i == 1)
                value = cpp_type(*expr.children[0]->type.element) + "(" +
                        converted(*child, *expr.children[0]->type.element) + ")";
            if (builtin && expr.text == "get" && i == 2)
                value = cpp_type(*expr.children[0]->type.element) + "(" +
                        converted(*child, *expr.children[0]->type.element) + ")";
            // Borrow stable locals/literals, and single-argument inputs.
            // Fields must be captured before later mutating method calls.
            const bool consume_step =
                builtin && expr.text == "step_value" &&
                (child->move_value || child->kind == Expr::Kind::Call ||
                 child->kind == Expr::Kind::Invoke || child->kind == Expr::Kind::Conditional);
            prefix += std::string(builtin && !builtin->mutates && !consume_step &&
                                          borrow_lifetime(*child) &&
                                          ((builtin->borrows_input && expr.children.size() == 1) ||
                                           (child->type == Type::Str &&
                                            (stable_string(*child) || expr.children.size() == 1)))
                                      ? "const auto& "
                                      : "auto ") +
                      name + " = " + value + "; ";
            if (expr.callee)
                args[expr.argument_order[i]] = "std::move(" + name + ")";
            else
                args.push_back("std::move(" + name + ")");
        }
        for (const auto& [position, path] : borrowed_paths) {
            const auto name = fresh();
            prefix += "auto& " + name + " = " + path + "; ";
            args[position] = name;
        }
        if (builtin && expr.text == "iter" &&
            (expr.children[0]->type.kind == Type::Cursor || expr.type.kind == Type::Record))
            return prefix + "return " + args[0] + "; }())";
        std::string target;
        if (expr.constructs)
            target = cpp_type(expr.type);
        else if (expr.symbol)
            target = function(expr.symbol);
        else if (builtin) {
            target = "wd::" + std::string(builtin->native_name);
            if (expr.text == "print" || expr.text == "eprint") {
                target += "_checked";
                args.insert(args.begin(), site(expr.location));
            } else if (builtin->site)
                args.push_back(site(expr.location));
        } else
            throw std::logic_error("unknown builtin in typed program");
        std::string call = target + (expr.constructs ? "{" : "(");
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i)
                call += ", ";
            call += args[i];
        }
        return prefix + "return " + call + (expr.constructs ? "}; }())" : "); }())");
    }
    std::string emit_closure(const Expr& expr) {
        std::string result = cpp_type(expr.type) + "{";
        for (std::size_t i = 0; i < expr.function->captures.size(); ++i) {
            if (i)
                result += ", ";
            result += variable(expr.function->captures[i].symbol);
        }
        return result + "}";
    }
    std::string emit_member(const Expr& expr) {
        return "(" + expression(*expr.children[0]) + ")." +
               (expr.children[0]->type.kind == Type::Record ? "wd_m" + std::to_string(expr.symbol)
                                                            : expr.text);
    }
    std::string emit_list(const Expr& expr) {
        std::string result = cpp_type(expr.type) + "{";
        for (std::size_t i = 0; i < expr.children.size(); ++i) {
            if (i)
                result += ", ";
            result += converted(*expr.children[i], *expr.type.element);
        }
        return result + "}";
    }
    std::string emit_tuple(const Expr& expr) {
        std::string prefix = "([&]() -> " + cpp_type(expr.type) + " { ";
        std::string result = cpp_type(expr.type) + "{";
        for (std::size_t i = 0; i < expr.children.size(); ++i) {
            const auto value = fresh();
            prefix += "auto " + value + " = " +
                      converted(*expr.children[i], expr.type.parameters[i]) + "; ";
            if (i)
                result += ", ";
            result += "std::move(" + value + ")";
        }
        return prefix + "return " + result + "}; }())";
    }
    std::string emit_index(const Expr& expr) {
        if (expr.children[0]->type.kind == Type::Tuple)
            return "std::get<" + std::to_string(expr.symbol) + ">(" +
                   expression(*expr.children[0]) + ")";
        const auto base = fresh(), index = fresh();
        const auto& source = *expr.children[0];
        const auto kind = source.type.kind;
        const bool readable =
            kind == Type::Array || kind == Type::List || kind == Type::Dict || kind == Type::Str;
        // Keep the established array rule. Newly borrowed list/dict/string
        // fields must not prolong a temporary record's unrelated resource owners.
        const bool owner =
            kind == Type::Array || source.kind != Expr::Kind::Member || stable_path(source);
        const bool borrow = borrow_lifetime(source) &&
                            ((source.type == Type::Str && stable_string(source)) ||
                             (readable && owner && !structural_effect(*expr.children[1])));
        return "([&]() -> " + cpp_type(expr.type) + " { " + (borrow ? "const auto& " : "auto ") +
               base + " = " + expression(*expr.children[0]) + "; auto " + index + " = " +
               expression(*expr.children[1]) + "; return wd::at(" + base + ", " + index + ", " +
               site(expr.location) + "); }())";
    }
    std::string emit_slice(const Expr& expr) {
        const auto base = fresh();
        const bool safe =
            std::none_of(expr.children.begin() + 1, expr.children.end(),
                         [&](const auto& bound) { return bound && structural_effect(*bound); });
        std::string prefix = "([&]() -> " + cpp_type(expr.type) + " { " +
                             (borrow_operand(*expr.children[0], safe) ? "const auto& " : "auto ") +
                             base + " = " + expression(*expr.children[0]) + "; ";
        std::vector<std::string> args{base};
        for (std::size_t i = 1; i < 4; ++i) {
            const auto value = fresh();
            prefix += (i == 3 ? "wd::Int " : "std::optional<wd::Int> ") + value + " = " +
                      (expr.children[i] ? expression(*expr.children[i])
                                        : (i == 3 ? "wd::Int{1}" : "std::nullopt")) +
                      "; ";
            args.push_back(value);
        }
        return prefix + "return wd::slice(" + args[0] + ", " + args[1] + ", " + args[2] + ", " +
               args[3] + ", " + site(expr.location) + "); }())";
    }
    std::string emit_conditional(const Expr& expr) {
        return "([&]() -> " + cpp_type(expr.type) + " { if (" + expression(*expr.children[1]) +
               ") return " + converted(*expr.children[0], expr.type) + "; return " +
               converted(*expr.children[2], expr.type) + "; }())";
    }
    std::string emit_comparechain(const Expr& expr) {
        auto previous = fresh();
        std::vector<bool> effects(expr.children.size() + 1, false);
        for (std::size_t i = expr.children.size(); i > 0; --i)
            effects[i - 1] = effects[i] || structural_effect(*expr.children[i - 1]);
        std::string prefix =
            "([&]() -> bool { " +
            std::string(borrow_operand(*expr.children[0], !effects[1]) ? "const auto& " : "auto ") +
            previous + " = " + expression(*expr.children[0]) + "; ";
        for (std::size_t i = 0; i < expr.operators.size(); ++i) {
            const auto current = fresh();
            prefix += (borrow_operand(*expr.children[i + 1], !effects[i + 2]) ? "const auto& "
                                                                              : "auto ") +
                      current + " = " + expression(*expr.children[i + 1]) + "; ";
            prefix += "if (!(" +
                      operation(expr.operators[i], Type::Bool, previous, current,
                                expr.children[i]->location) +
                      ")) return false; ";
            previous = current;
        }
        return prefix + "return true; }())";
    }
    std::string emit_dictcomprehension(const Expr& expr) {
        const auto result = fresh();
        const bool dictionary = expr.kind == Expr::Kind::DictComprehension;
        const std::size_t head = dictionary ? 2 : 1;
        std::string prefix =
            "([&]() -> " + cpp_type(expr.type) + " { " + cpp_type(expr.type) + " " + result + "; ";
        std::size_t braces = 0;
        for (std::size_t i = 0; i < expr.clauses.size(); ++i) {
            const auto& clause = expr.clauses[i];
            if (clause.name.empty()) {
                prefix += "if (" + expression(*expr.children[head + i]) + ") { ";
                ++braces;
            } else {
                prefix += iteration(*expr.children[head + i], clause.symbol, clause.type,
                                    clause.location, false);
                ++braces;
            }
        }
        if (dictionary) {
            const auto key = fresh(), value = fresh();
            prefix += "auto " + key + " = " + expression(*expr.children[0]) + "; ";
            prefix +=
                "auto " + value + " = " + converted(*expr.children[1], *expr.type.element) + "; ";
            prefix += result + ".set(std::move(" + key + "), std::move(" + value + "), " +
                      site(expr.location) + "); ";
        } else
            prefix += result + ".push(" + converted(*expr.children[0], *expr.type.element) + ", " +
                      site(expr.location) + "); ";
        for (std::size_t i = 0; i < braces; ++i)
            prefix += "} ";
        return prefix + "return " + result + "; }())";
    }
    std::string emit_dict(const Expr& expr) {
        const auto result = fresh();
        std::string prefix =
            "([&]() -> " + cpp_type(expr.type) + " { " + cpp_type(expr.type) + " " + result + "; ";
        for (std::size_t i = 0; i < expr.children.size(); i += 2) {
            const auto key = fresh(), value = fresh();
            prefix += "auto " + key + " = " + expression(*expr.children[i]) + "; ";
            prefix += "auto " + value + " = " +
                      converted(*expr.children[i + 1], *expr.type.element) + "; ";
            prefix += result + ".set(std::move(" + key + "), std::move(" + value + "), " +
                      site(expr.location) + "); ";
        }
        return prefix + "return " + result + "; }())";
    }
    std::string emit_unary(const Expr& expr) {
        std::vector<const Expr*> operations;
        const Expr* leaf = &expr;
        while (leaf->kind == Expr::Kind::Unary && leaf->symbol != 1) {
            operations.push_back(leaf);
            leaf = leaf->children[0].get();
        }
        std::string value = leaf->kind == Expr::Kind::Unary ? "wd::min_int" : expression(*leaf);
        for (auto item = operations.rbegin(); item != operations.rend(); ++item) {
            const auto& operation = **item;
            if (operation.text == "not")
                value = "(!(" + value + "))";
            else if (operation.type.kind == Type::Array)
                value = operation.text == "-" ? "wd::array_mul(" + value + ", wd::Int{-1}, " +
                                                    site(operation.location) + ")"
                                              : value;
            else if (operation.text == "-" && operation.type == Type::Int)
                value = "wd::neg(" + value + ", " + site(operation.location) + ")";
            else
                value = "(" + operation.text + "(" + value + "))";
        }
        return value;
    }
    std::string emit_binary(const Expr& expr) {
        const auto a = expression(*expr.children[0]), b = expression(*expr.children[1]);
        if (expr.text == "and" || expr.text == "or")
            return "((" + a + ") " + (expr.text == "and" ? "&&" : "||") + " (" + b + "))";
        const auto left = fresh(), right = fresh();
        const auto body = operation(expr.text, expr.type, left, right, expr.location);
        // Array kernels consume owned temporaries; readonly comparisons do not.
        const bool read = expr.type.kind != Type::Array;
        return "([&]() -> " + cpp_type(expr.type) + " { " +
               (borrow_operand(*expr.children[0], read && !structural_effect(*expr.children[1]))
                    ? "const auto& "
                    : "auto ") +
               left + " = " + a + "; " +
               (borrow_operand(*expr.children[1], read) ? "const auto& " : "auto ") + right +
               " = " + b + "; return " + body + "; }())";
    }
    std::string expression(const Expr& expr) {
        if (expr.refers_function)
            return "(&" + function(expr.symbol) + ")";
        switch (expr.kind) {
        case Expr::Kind::Closure:
            return emit_closure(expr);
        case Expr::Kind::TypeApply:
            throw std::logic_error("unresolved type application in typed program");
        case Expr::Kind::Integer:
            return "wd::Int{" + expr.text + "}";
        case Expr::Kind::Float:
            return "double{" + expr.text + "}";
        case Expr::Kind::String:
            return "std::string(" + cpp_literal(expr.text) + ", " +
                   std::to_string(expr.text.size()) + ")";
        case Expr::Kind::Bool:
            return expr.text;
        case Expr::Kind::None:
            return "wd::None{}";
        case Expr::Kind::Name:
            return expr.move_value ? "std::move(" + variable(expr.symbol) + ")"
                                   : variable(expr.symbol);
        case Expr::Kind::Member:
            return emit_member(expr);
        case Expr::Kind::List:
            return emit_list(expr);
        case Expr::Kind::Tuple:
            return emit_tuple(expr);
        case Expr::Kind::Index:
            return emit_index(expr);
        case Expr::Kind::Slice:
            return emit_slice(expr);
        case Expr::Kind::Conditional:
            return emit_conditional(expr);
        case Expr::Kind::CompareChain:
            return emit_comparechain(expr);
        case Expr::Kind::ListComprehension:
        case Expr::Kind::DictComprehension:
            return emit_dictcomprehension(expr);
        case Expr::Kind::Dict:
            return emit_dict(expr);
        case Expr::Kind::Unary:
            return emit_unary(expr);
        case Expr::Kind::Binary:
            return emit_binary(expr);
        case Expr::Kind::Invoke:
            return invoke_expression(expr);
        case Expr::Kind::Call:
            return call_expression(expr);
        }
        throw std::logic_error("unknown expression");
    }
    void block(const Block& statements) {
        for (const auto& stmt : statements)
            if (stmt->kind != Stmt::Kind::Function)
                statement(*stmt);
    }
    void statement(const Stmt& stmt) {
        switch (stmt.kind) {
        case Stmt::Kind::With: {
            line("{");
            ++indent;
            const auto owner = fresh();
            line("wd::ResourceScope " + owner + "(" + expression(*stmt.expressions[0]) + ", " +
                 site(stmt.location) + ");");
            line(cpp_type(stmt.expressions[0]->type) + " " + variable(stmt.symbol) + " = " + owner +
                 ".value;");
            block(stmt.body);
            --indent;
            line("}");
            break;
        }
        case Stmt::Kind::Import:
            break;
        case Stmt::Kind::Raise:
            line("wd::raise(" + expression(*stmt.expressions[0]) + ", " + site(stmt.location) +
                 ");");
            break;
        case Stmt::Kind::Finally: {
            const auto label = fresh(), flow = fresh(), value = fresh(), pending = fresh();
            line("{");
            ++indent;
            line("[[maybe_unused]] int " + flow + " = 0;");
            line("std::exception_ptr " + pending + ";");
            if (returning != Type::Void)
                line("std::optional<" + cpp_type(returning) + "> " + value + ";");
            line("try {");
            ++indent;
            finals.push_back({label, flow, value, loops});
            block(stmt.body);
            const auto uses = finals.back().uses;
            finals.pop_back();
            --indent;
            // Only wood errors run the finally block on the way out; exit and host failures do not.
            line("} catch (const wd::Error&) {");
            line("    " + pending + " = std::current_exception();");
            line("}");
            if (uses)
                line(label + ":;");
            block(stmt.otherwise);
            line("if (" + pending + ") std::rethrow_exception(" + pending + ");");
            const auto moved = returning != Type::Void ? "std::move(*" + value + ")" : "";
            if (uses & 2) {
                line("if (" + flow + " == 1)");
                if (!finals.empty())
                    leave(1, moved);
                else
                    line(moved.empty() ? "    return;" : "    return " + moved + ";");
            }
            for (const unsigned how : {2u, 3u})
                if (uses & (1u << how)) {
                    line("if (" + flow + " == " + std::to_string(how) + ")");
                    if (leaves_loop())
                        leave(how, "");
                    else
                        line(how == 2 ? "    break;" : "    continue;");
                }
            --indent;
            line("}");
            break;
        }
        case Stmt::Kind::Try: {
            const auto caught = fresh();
            line("try {");
            ++indent;
            block(stmt.body);
            --indent;
            line("} catch (const wd::Error& " + caught + ") {");
            ++indent;
            line("wd::Failure " + variable(stmt.symbol) + " = wd::failure(" + caught + ");");
            block(stmt.otherwise);
            --indent;
            line("}");
            break;
        }
        case Stmt::Kind::Assign:
            line((stmt.declares ? cpp_type(stmt.annotation) + " " : "") + variable(stmt.symbol) +
                 " = " + converted(*stmt.expressions[0], stmt.annotation) + ";");
            break;
        case Stmt::Kind::IndexAssign: {
            const auto value = fresh();
            std::string prefix = "{ auto " + value + " = " +
                                 converted(*stmt.expressions[1], stmt.expressions[0]->type) + "; ";
            const auto& target_expr = *stmt.expressions[0];
            if (target_expr.kind == Expr::Kind::Index &&
                target_expr.children[0]->type.kind == Type::Dict) {
                const auto target = mutation_path(*target_expr.children[0], prefix), key = fresh();
                prefix += "auto " + key + " = " + expression(*target_expr.children[1]) + "; ";
                line(prefix + target + ".set(std::move(" + key + "), std::move(" + value + "), " +
                     site(stmt.location) + "); }");
            } else {
                const auto target = mutation_path(target_expr, prefix);
                line(prefix + target + " = std::move(" + value + "); }");
            }
            break;
        }
        case Stmt::Kind::AugAssign: {
            std::string prefix = "{ ";
            const auto [read, write] = captured_path(*stmt.expressions[0], prefix);
            const auto old = fresh(), value = fresh(), result = fresh();
            prefix += "auto " + old + " = " +
                      (stmt.expressions[0]->kind == Expr::Kind::Index ? "std::move(" + read + ")"
                                                                      : read) +
                      "; ";
            prefix += "auto " + value + " = " + expression(*stmt.expressions[1]) + "; ";
            prefix += "auto " + result + " = " +
                      operation(stmt.name, stmt.annotation, old, value, stmt.location) + "; ";
            line(prefix + write + " = std::move(" + result + "); }");
            break;
        }
        case Stmt::Kind::Expression:
            line("(void)(" + expression(*stmt.expressions[0]) + ");");
            break;
        case Stmt::Kind::Return:
            if (!finals.empty())
                leave(1, stmt.expressions.empty() ? ""
                                                  : converted(*stmt.expressions[0], stmt.annotation));
            else
                line(stmt.expressions.empty()
                         ? "return;"
                         : "return " + converted(*stmt.expressions[0], stmt.annotation) + ";");
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            if (leaves_loop())
                leave(stmt.kind == Stmt::Kind::Break ? 2 : 3, "");
            else
                line(stmt.kind == Stmt::Kind::Break ? "break;" : "continue;");
            break;
        case Stmt::Kind::Pass:
            line(";");
            break;
        case Stmt::Kind::If:
            line("if (" + expression(*stmt.expressions[0]) + ") {");
            ++indent;
            block(stmt.body);
            --indent;
            if (!stmt.otherwise.empty()) {
                line("} else {");
                ++indent;
                block(stmt.otherwise);
                --indent;
            }
            line("}");
            break;
        case Stmt::Kind::While:
            line("while (" + expression(*stmt.expressions[0]) + ") {");
            ++indent;
            ++loops;
            block(stmt.body);
            --loops;
            --indent;
            line("}");
            break;
        case Stmt::Kind::For: {
            const auto& range = *stmt.expressions[0];
            line(iteration(range, stmt.symbol, stmt.annotation, stmt.location));
            ++indent;
            ++loops;
            block(stmt.body);
            --loops;
            --indent;
            line("} }");
            break;
        }
        case Stmt::Kind::Function:
        case Stmt::Kind::Record:
            break;
        }
    }
    std::string signature(const Stmt& stmt) {
        std::string result = cpp_type(stmt.return_type) + " " + function(stmt.symbol) + "(";
        for (std::size_t i = 0; i < stmt.parameters.size(); ++i) {
            if (i)
                result += ", ";
            result += cpp_type(stmt.parameters[i].type) +
                      ((stmt.method && stmt.mutating && i == 0) || stmt.parameters[i].mutable_borrow
                           ? "& "
                           : " ") +
                      variable(stmt.parameters[i].symbol);
        }
        return result + ")";
    }

  public:
    explicit Generator(std::vector<std::string> source_files, bool json)
        : files(std::move(source_files)), json_runtime(json) {}
    std::string run(const std::vector<const Program*>& modules, std::size_t entry) {
        line("// Generated by Wood. All language checks run before this stage.");
        // Compatibility with older standalone runtime bundles: their value
        // getter is safe too, with the temporary pinned by the loop binding.
        line("namespace { template<class L> decltype(auto) wd_read_iteration(const L& source, "
             "wd::Int index, wd::Site site) { if constexpr(requires { source.get_ref(index, "
             "site); }) return source.get_ref(index, site); else return source.get(index, site); } }");
        std::vector<const Stmt*> records;
        std::vector<const Stmt*> functions, closures;
        std::function<void(const Expr&)> scan_expression;
        std::function<void(const Block&)> scan_block;
        scan_expression = [&](const Expr& expr) {
            if (expr.function) {
                closures.push_back(expr.function.get());
                scan_block(expr.function->body);
            }
            for (const auto& child : expr.children)
                if (child)
                    scan_expression(*child);
        };
        scan_block = [&](const Block& body) {
            for (const auto& stmt : body) {
                if (stmt->kind == Stmt::Kind::Assign)
                    written.insert(stmt->symbol);
                if (stmt->kind == Stmt::Kind::AugAssign) {
                    const Expr* target = stmt->expressions[0].get();
                    while (target->kind == Expr::Kind::Index || target->kind == Expr::Kind::Member)
                        target = target->children[0].get();
                    if (target->kind == Expr::Kind::Name)
                        written.insert(target->symbol);
                }
                for (const auto& expr : stmt->expressions)
                    scan_expression(*expr);
                if (stmt->kind != Stmt::Kind::Function && stmt->kind != Stmt::Kind::Record) {
                    scan_block(stmt->body);
                    scan_block(stmt->otherwise);
                }
            }
        };
        std::function<void(const Stmt&)> collect = [&](const Stmt& stmt) {
            if (stmt.kind != Stmt::Kind::Record && stmt.kind != Stmt::Kind::Function)
                return;
            if (stmt.type_parameters.empty()) {
                if (stmt.kind == Stmt::Kind::Record) {
                    records.push_back(&stmt);
                    for (const auto& method : stmt.body)
                        collect(*method);
                } else if (stmt.checked) {
                    functions.push_back(&stmt);
                    scan_block(stmt.body);
                }
            } else
                for (const auto& instance : stmt.specializations)
                    collect(*instance);
        };
        for (const auto* program : modules)
            for (const auto& stmt : program->statements)
                collect(*stmt);
        for (const auto* program : modules)
            scan_block(program->statements);
        line("namespace wd {");
        for (const auto* closure : closures) {
            const auto name = "wd_c" + std::to_string(closure->symbol);
            line("struct " + name + ";");
            line("std::string to_str(const " + name + "&);");
        }
        for (const auto* record : records) {
            const auto name = "wd_r" + std::to_string(record->symbol);
            line("struct " + name + ";");
            line("std::string to_str(const " + name + "&);");
            if (json_compatible(Type::record(*record)))
                line("Json to_json(const " + name + "&, Site, std::size_t = 0);");
        }
        std::set<const Stmt*> emitted, comparable;
        for (const auto* record : records) {
            const auto type = Type::record(*record);
            if (equality_compatible(type))
                comparable.insert(record);
        }
        std::function<void(const Stmt&)> emit_record, emit_closure;
        std::function<void(const Type&)> dependency = [&](const Type& type) {
            if (type.kind == Type::Record)
                emit_record(*type.declaration);
            else if (type.kind == Type::Closure)
                emit_closure(*type.declaration);
            else if (type.kind == Type::Optional || type.kind == Type::Step)
                dependency(*type.element);
            else if (type.kind == Type::Tuple)
                for (const auto& item : type.parameters)
                    dependency(item);
        };
        emit_closure = [&](const Stmt& closure) {
            if (!emitted.insert(&closure).second)
                return;
            for (const auto& capture : closure.captures)
                dependency(capture.type);
            const auto name = "wd_c" + std::to_string(closure.symbol);
            line("struct " + name + " {");
            for (const auto& capture : closure.captures)
                line(cpp_type(capture.type) + " " + variable(capture.symbol) + ";");
            std::string call = cpp_type(closure.return_type) + " operator()(";
            for (std::size_t i = 0; i < closure.parameters.size(); ++i) {
                if (i)
                    call += ", ";
                call += cpp_type(closure.parameters[i].type) + " " +
                        variable(closure.parameters[i].symbol);
            }
            line(call + (closure.mutating ? ");" : ") const;"));
            line("};");
            line("inline std::string to_str(const " + name + "&) { return \"<closure>\"; }");
        };
        emit_record = [&](const Stmt& record) {
            if (!emitted.insert(&record).second)
                return;
            for (const auto& field : record.parameters)
                dependency(field.type);
            const auto name = "wd_r" + std::to_string(record.symbol);
            line("struct " + name + " {");
            for (const auto& field : record.parameters)
                line(cpp_type(field.type) + " wd_m" + std::to_string(field.symbol) + ";");
            if (comparable.contains(&record))
                line("bool operator==(const " + name + "&) const;");
            line("};");
        };
        for (const auto* record : records)
            emit_record(*record);
        for (const auto* closure : closures)
            emit_closure(*closure);
        for (const auto* record : records) {
            const auto name = "wd_r" + std::to_string(record->symbol);
            if (comparable.contains(record)) {
                line("inline bool " + name + "::operator==(const " + name + "& other) const {");
                std::string comparison = "true";
                for (const auto& field : record->parameters) {
                    const auto member = "wd_m" + std::to_string(field.symbol);
                    comparison += " && " + member + " == other." + member;
                }
                line("return " + comparison + "; }");
            }
            line("inline std::string to_str(const " + name + "& value) {");
            line("std::string result = " + cpp_literal(record->name + "(") + ";");
            for (std::size_t i = 0; i < record->parameters.size(); ++i) {
                const auto& field = record->parameters[i];
                line("result += " + cpp_literal((i ? ", " : "") + field.name + "=") +
                     " + repr(value.wd_m" + std::to_string(field.symbol) + ");");
            }
            line("return result + ')'; }");
            if (json_compatible(Type::record(*record))) {
                line("inline Json to_json(const " + name +
                     "& value, Site site, std::size_t depth) {");
                line("json_depth(depth, site); Json::Object object;");
                for (const auto& field : record->parameters)
                    line("object.emplace(" + cpp_literal(field.name) + ", to_json(value.wd_m" +
                         std::to_string(field.symbol) + ", site, depth + 1));");
                line("return Json(std::move(object)); }");
            }
        }
        line("} // namespace wd");
        line("namespace wd_ffi {");
        for (const auto* program : modules)
            for (const auto& stmt : program->statements) {
                if (!stmt->external)
                    continue;
                std::string declaration =
                    "extern \"C\" " + cpp_type(stmt->return_type) + " " + stmt->name + "(";
                for (std::size_t i = 0; i < stmt->parameters.size(); ++i) {
                    if (i)
                        declaration += ", ";
                    if (stmt->parameters[i].type.kind == Type::Array) {
                        declaration +=
                            std::string(stmt->parameters[i].mutable_borrow ? "" : "const ") +
                            cpp_type(*stmt->parameters[i].type.element) + "*, wd::Int";
                        continue;
                    }
                    declaration += stmt->parameters[i].type == Type::Str
                                       ? "const char*, wd::Int"
                                       : cpp_type(stmt->parameters[i].type);
                }
                line(declaration + ");");
            }
        line("} // namespace wd_ffi");
        for (const auto* stmt : functions)
            line(signature(*stmt) + ";");
        for (const auto* closure : closures) {
            std::string call = cpp_type(closure->return_type) + " wd::wd_c" +
                               std::to_string(closure->symbol) + "::operator()(";
            for (std::size_t i = 0; i < closure->parameters.size(); ++i) {
                if (i)
                    call += ", ";
                call += cpp_type(closure->parameters[i].type) + " " +
                        variable(closure->parameters[i].symbol);
            }
            line(call + (closure->mutating ? ") {" : ") const {"));
            ++indent;
            returning = closure->return_type;
            block(closure->body);
            --indent;
            line("}");
        }
        for (const auto* stmt : functions) {
            line(signature(*stmt) + " {");
            ++indent;
            if (stmt->external) {
                std::string call = "return wd_ffi::" + stmt->name + "(";
                std::vector<std::string> callbacks;
                const auto failures = fresh();
                if (std::any_of(stmt->parameters.begin(), stmt->parameters.end(),
                                [](const auto& p) { return p.type.kind == Type::Function; }))
                    line("wd::CallbackFailure " + failures + ";");
                for (std::size_t i = 0; i < stmt->parameters.size(); ++i) {
                    if (i)
                        call += ", ";
                    const auto& p = stmt->parameters[i];
                    const auto arg = variable(p.symbol);
                    if (p.type == Type::Str)
                        call += arg + ".data(), static_cast<wd::Int>(" + arg + ".size())";
                    else if (p.type.kind == Type::Array) {
                        const auto pointer = fresh();
                        line("auto* " + pointer + " = " + arg +
                             (p.mutable_borrow ? ".foreign_mutable_data(" : ".foreign_data(") +
                             site(stmt->location) + ");");
                        call += pointer + ", static_cast<wd::Int>(" + arg + ".size())";
                    } else if (p.type.kind == Type::Function) {
                        const auto guard = fresh();
                        line("wd::CallbackGuard<" + cpp_type(p.type) + ", " +
                             std::to_string(p.symbol) + "> " + guard + "(" + arg + ", " + failures +
                             ");");
                        call += "&decltype(" + guard + ")::invoke";
                        callbacks.push_back(guard);
                    } else
                        call += arg;
                }
                if (callbacks.empty())
                    line(call + ");");
                else {
                    const auto value = fresh();
                    line((stmt->return_type == Type::Void ? "" : "auto " + value + " = ") +
                         call.substr(7) + ");");
                    for (const auto& guard : callbacks)
                        line(guard + ".check();");
                    if (stmt->return_type != Type::Void)
                        line("return " + value + ";");
                }
            } else {
                returning = stmt->return_type;
                block(stmt->body);
            }
            --indent;
            line("}");
        }
        returning = Type::Void;
        line("#ifdef _WIN32");
        line("int wmain(int argc, wchar_t** argv) {");
        line("#else");
        line("int main(int argc, char** argv) {");
        line("#endif");
        ++indent;
        line("try {");
        ++indent;
        line("wd::init_args(argc, argv);");
        block(modules.at(entry)->statements);
        line("return 0;");
        --indent;
        line("} catch (const wd::Exit& result) { return result.code;");
        line("} catch (const wd::Error& error) {");
        ++indent;
        line(std::string("wd::report(error, ") + (json_runtime ? "true" : "false") + ");");
        line("return 70;");
        --indent;
        line("} catch (const std::exception& error) {");
        ++indent;
        line("wd::report(wd::Error{" + site({0, 0, 0}) + ", \"R4099\", error.what()}, " +
             (json_runtime ? "true" : "false") + ");");
        line("return 70;");
        --indent;
        line("}");
        --indent;
        line("}");
        return std::move(out).str();
    }
};
} // namespace
std::string generate(const Program& program, std::string_view source_file, bool json_runtime) {
    return Generator({std::string(source_file)}, json_runtime).run({&program}, 0);
}
std::string generate(const Compilation& compilation, bool json_runtime) {
    std::vector<std::string> files;
    std::vector<const Program*> modules;
    for (const auto& module : compilation.modules) {
        files.push_back(module.file);
        modules.push_back(&module.program);
    }
    return Generator(std::move(files), json_runtime).run(modules, compilation.entry);
}
} // namespace wood
