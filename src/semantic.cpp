#include "wood/builtins.hpp"
#include "wood/compiler.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace wood {
namespace {
bool numeric(const Type& type) {
    return type == Type::Int || type == Type::Float;
}
bool callable_type(const Type& type) {
    return type.kind == Type::Function || type.kind == Type::Callable || type.kind == Type::Closure;
}
// Error-path-only spelling aid: one edit or one adjacent transposition, with a
// unique visible match. Bound the work for identifiers from untrusted source.
bool similar_identifier(std::string_view written, std::string_view candidate) {
    const auto dot = written.rfind('.'), other_dot = candidate.rfind('.');
    if ((dot == std::string_view::npos) != (other_dot == std::string_view::npos))
        return false;
    if (dot != std::string_view::npos) {
        if (written.substr(0, dot) != candidate.substr(0, other_dot))
            return false;
        written.remove_prefix(dot + 1);
        candidate.remove_prefix(other_dot + 1);
    }
    if (written.size() < 3 || written.size() > 64 || candidate.size() < 3 ||
        candidate.size() > 64 || written == candidate)
        return false;
    if (written.size() > candidate.size())
        std::swap(written, candidate);
    if (candidate.size() - written.size() > 1)
        return false;
    std::size_t i = 0;
    while (i < written.size() && written[i] == candidate[i])
        ++i;
    if (written.size() != candidate.size())
        return written.substr(i) == candidate.substr(i + 1);
    return written.substr(i + 1) == candidate.substr(i + 1) ||
           (i + 1 < written.size() && written[i] == candidate[i + 1] &&
            written[i + 1] == candidate[i] && written.substr(i + 2) == candidate.substr(i + 2));
}
bool assignable(const Type& target, const Type& value) {
    return target == value ||
           (target.kind == Type::Callable && callable_type(value) &&
            target.parameters == value.parameters && *target.element == *value.element) ||
           (target == Type::Float && value == Type::Int) ||
           (target.kind == Type::Optional &&
            (value == Type::None ||
             (value.kind != Type::Optional && assignable(*target.element, value))));
}
bool reserved(std::string_view name) {
    return reserved_builtin(name) || name == "range" || name == "list" || name == "bool" ||
           name == "void" || name == "error";
}
struct Binding {
    Type type;
    std::uint64_t symbol;
};
using TypeArguments = std::unordered_map<std::string, Type>;
Type substituted(Type type, const TypeArguments& arguments);
Type resolve_type(Type type, const FunctionBindings& bindings, Location at,
                  const TypeArguments& arguments = {}, std::size_t depth = 0) {
    if (depth > 128)
        fail("E3029", at, "recursive or excessively deep associated type");
    if (type.kind == Type::Named || type.kind == Type::Parameter)
        if (const auto found = arguments.find(type.name); found != arguments.end()) {
            if (!type.parameters.empty())
                fail("E3024", at,
                     "type parameter '" + type.name + "' does not accept type arguments");
            return found->second;
        }
    if (type.kind == Type::Signature) {
        if (!arguments.contains(type.name))
            fail("E3028", at,
                 "callable pattern requires a declared type parameter '" + type.name + "'");
        const auto actual = arguments.at(type.name);
        if (actual.kind != Type::Parameter)
            return actual;
        for (auto& parameter : type.parameters)
            parameter = resolve_type(parameter, bindings, at, arguments, depth + 1);
        type.element = std::make_shared<const Type>(
            resolve_type(*type.element, bindings, at, arguments, depth + 1));
        return type;
    }
    if (type.kind == Type::Tuple) {
        for (auto& item : type.parameters)
            item = resolve_type(item, bindings, at, arguments, depth + 1);
        return type;
    }
    if (type.kind == Type::Optional)
        return Type::optional(resolve_type(*type.element, bindings, at, arguments, depth + 1));
    if (callable_type(type)) {
        for (auto& parameter : type.parameters)
            parameter = resolve_type(parameter, bindings, at, arguments, depth + 1);
        type.element = std::make_shared<const Type>(
            resolve_type(*type.element, bindings, at, arguments, depth + 1));
        return type;
    }
    if (type.kind == Type::Named && type.name.find('.') != std::string::npos &&
        !bindings.contains(type.name)) {
        const auto split = type.name.rfind('.');
        const auto prefix = type.name.substr(0, split), member = type.name.substr(split + 1);
        if (arguments.contains(prefix) &&
            (member == "Result" || member == "Item" || member == "Iterator")) {
            type = Type::wrapped(Type::Projection, arguments.at(prefix));
            type.name = member;
        }
    }
    if (type.kind == Type::Step || type.kind == Type::Cursor || type.kind == Type::Array)
        return Type::wrapped(type.kind,
                             resolve_type(*type.element, bindings, at, arguments, depth + 1));
    if (type.kind == Type::Projection) {
        const auto base = resolve_type(*type.element, bindings, at, arguments, depth + 1);
        if (base.kind == Type::Parameter || base.kind == Type::Projection) {
            type.element = std::make_shared<const Type>(base);
            return type;
        }
        if (type.name == "Result" && callable_type(base))
            return *base.element;
        if (type.name == "Iterator") {
            if (base.kind == Type::List || base.kind == Type::Array || base.kind == Type::Dict ||
                base == Type::Str || base == Type::Range)
                return Type::wrapped(Type::Cursor, base);
            if (base.kind == Type::Cursor)
                return base;
            if (base.kind == Type::Record) {
                TypeArguments bound;
                if (!base.declaration->type_parameters.empty())
                    for (std::size_t i = 0; i < base.parameters.size(); ++i)
                        bound.emplace(base.declaration->type_parameters[i], base.parameters[i]);
                for (const auto& method : base.declaration->body)
                    if (method->name == "iter") {
                        if (method->return_type == Type::Unknown) {
                            type.element = std::make_shared<const Type>(base);
                            return type;
                        }
                        return resolve_type(substituted(method->return_type, bound), bindings, at,
                                            arguments, depth + 1);
                    }
                for (const auto& method : base.declaration->body)
                    if (method->name == "next")
                        return base;
            }
        }
        if (type.name == "Item") {
            if (base.kind == Type::List || base.kind == Type::Array)
                return *base.element;
            if (base.kind == Type::Dict)
                return *base.key;
            if (base == Type::Str)
                return Type::Str;
            if (base == Type::Range)
                return Type::Int;
            if (base.kind == Type::Cursor) {
                const auto& source = *base.element;
                if (source.kind == Type::List || source.kind == Type::Array)
                    return *source.element;
                if (source.kind == Type::Dict)
                    return *source.key;
                if (source == Type::Str)
                    return Type::Str;
                if (source == Type::Range)
                    return Type::Int;
            }
            if (base.kind == Type::Record) {
                TypeArguments bound;
                const auto* declaration = base.declaration;
                if (!declaration->type_parameters.empty())
                    for (std::size_t i = 0; i < declaration->type_parameters.size(); ++i)
                        bound.emplace(declaration->type_parameters[i], base.parameters[i]);
                for (const auto& method : declaration->body)
                    if (method->name == "next") {
                        if (method->return_type == Type::Unknown) {
                            type.element = std::make_shared<const Type>(base);
                            return type;
                        }
                        const auto result = resolve_type(substituted(method->return_type, bound),
                                                         bindings, at, arguments, depth + 1);
                        if (result.kind == Type::Step)
                            return *result.element;
                    }
                for (const auto& method : declaration->body)
                    if (method->name == "iter") {
                        if (method->return_type == Type::Unknown) {
                            type.element = std::make_shared<const Type>(base);
                            return type;
                        }
                        auto projection = Type::wrapped(
                            Type::Projection, resolve_type(substituted(method->return_type, bound),
                                                           bindings, at, arguments, depth + 1));
                        projection.name = "Item";
                        return resolve_type(projection, bindings, at, arguments, depth + 1);
                    }
            }
        }
        fail("E3029", at, "type " + type_name(base) + " does not provide ." + type.name);
    }
    if (type.kind == Type::Named) {
        const auto found = bindings.find(type.name);
        // Preserve existing nominal records named stream. The native annotation
        // is used only when no explicit declaration binds that type name.
        if (found == bindings.end() || found->second->kind != Stmt::Kind::Record) {
            if (type.name == "step" || type.name == "cursor" || type.name == "array") {
                if (type.parameters.size() != 1)
                    fail("E3024", at, "native step/cursor annotations require one type argument");
                return Type::wrapped(
                    type.name == "step"    ? Type::Step
                    : type.name == "array" ? Type::Array
                                           : Type::Cursor,
                    resolve_type(type.parameters[0], bindings, at, arguments, depth + 1));
            }
            if (!type.parameters.empty())
                fail("E3024", at, "unknown generic record type '" + type.name + "'");
            if (type.name == "None")
                return Type::None;
            static const std::map<std::string, Type::Kind> native = {
                {"range", Type::Range},
                {"stream", Type::Stream},
                {"database", Type::Database},
                {"statement", Type::Statement},
                {"transaction", Type::Transaction},
                {"tls_context", Type::TlsContext},
                {"task", Type::Task}};
            if (const auto native_type = native.find(type.name); native_type != native.end())
                return native_type->second;
        }
        if (found == bindings.end() || found->second->kind != Stmt::Kind::Record)
            fail("E2003", at, "unknown or invalid type '" + type.name + "'");
        if (type.parameters.size() != found->second->type_parameters.size())
            fail("E3024", at,
                 "record '" + type.name + "' expects " +
                     std::to_string(found->second->type_parameters.size()) + " type arguments");
        auto result = Type::record(*found->second);
        for (const auto& item : type.parameters)
            result.parameters.push_back(resolve_type(item, bindings, at, arguments, depth + 1));
        return result;
    }
    if (type.kind == Type::List)
        return Type::list(resolve_type(*type.element, bindings, at, arguments, depth + 1));
    if (type.kind == Type::Dict) {
        auto key = resolve_type(*type.key, bindings, at, arguments, depth + 1);
        if (key != Type::Int && key != Type::Str && key != Type::Bool &&
            key.kind != Type::Parameter)
            fail("E3020", at, "dictionary keys must be int, str, or bool");
        return Type::dict(key, resolve_type(*type.element, bindings, at, arguments, depth + 1));
    }
    return type;
}
bool abstract_type(const Type& type) {
    if (type.kind == Type::Parameter)
        return true;
    if (type.element && abstract_type(*type.element))
        return true;
    if (type.key && abstract_type(*type.key))
        return true;
    return std::any_of(type.parameters.begin(), type.parameters.end(), abstract_type);
}
void validate_type_depth(const Type& value, Location at) {
    std::vector<std::pair<const Type*, std::size_t>> pending{{&value, 0}};
    while (!pending.empty()) {
        const auto [type, depth] = pending.back();
        pending.pop_back();
        // A nongeneric nominal name is a leaf, as it was before generic records.
        // Only an applied record adds a structural type-argument level.
        if (type->kind == Type::Record && type->parameters.empty())
            continue;
        if (type->kind != Type::List && type->kind != Type::Array && type->kind != Type::Dict &&
            type->kind != Type::Function && type->kind != Type::Tuple &&
            type->kind != Type::Optional && type->kind != Type::Record &&
            type->kind != Type::Callable && type->kind != Type::Closure &&
            type->kind != Type::Step && type->kind != Type::Cursor &&
            type->kind != Type::Projection && type->kind != Type::Signature)
            continue;
        if (depth >= 64)
            fail("E2002", at, "type nesting exceeds 64 levels");
        if (type->element)
            pending.emplace_back(type->element.get(), depth + 1);
        for (const auto& parameter : type->parameters)
            pending.emplace_back(&parameter, depth + 1);
    }
}
Type substituted(Type type, const TypeArguments& arguments) {
    if (type.kind == Type::Parameter || type.kind == Type::Named || type.kind == Type::Signature)
        if (const auto found = arguments.find(type.name); found != arguments.end())
            return found->second;
    if (type.element)
        type.element = std::make_shared<const Type>(substituted(*type.element, arguments));
    if (type.key)
        type.key = std::make_shared<const Type>(substituted(*type.key, arguments));
    for (auto& item : type.parameters)
        item = substituted(item, arguments);
    return type;
}
std::unique_ptr<Stmt> clone_statement(const Stmt& source, const TypeArguments& arguments);
ExprPtr clone_expression(const Expr& source, const TypeArguments& arguments = {}) {
    auto result = std::make_unique<Expr>(source.kind, source.location, source.text);
    result->operators = source.operators;
    result->keywords = source.keywords;
    for (const auto& type : source.type_arguments)
        result->type_arguments.push_back(substituted(type, arguments));
    if (source.function)
        result->function = clone_statement(*source.function, arguments);
    result->clauses = source.clauses;
    for (const auto& child : source.children)
        result->children.push_back(child ? clone_expression(*child, arguments) : nullptr);
    return result;
}
std::unique_ptr<Stmt> clone_statement(const Stmt& source, const TypeArguments& arguments) {
    auto result = std::make_unique<Stmt>(source.kind, source.location);
    result->name = source.name;
    result->method = source.method;
    result->mutating = source.mutating;
    result->inferred_return = source.inferred_return;
    result->expression_body = source.expression_body;
    result->mutable_captures = source.mutable_captures;
    result->type_parameters = source.type_parameters;
    result->return_type = substituted(source.return_type, arguments);
    result->annotation = substituted(source.annotation, arguments);
    for (const auto& parameter : source.parameters) {
        Parameter item{parameter.name, substituted(parameter.type, arguments), parameter.location};
        item.mode = parameter.mode;
        item.mutable_borrow = parameter.mutable_borrow;
        if (parameter.default_value)
            item.default_value = clone_expression(*parameter.default_value, arguments);
        result->parameters.push_back(std::move(item));
    }
    for (const auto& expression : source.expressions)
        result->expressions.push_back(clone_expression(*expression, arguments));
    for (const auto& statement : source.body)
        result->body.push_back(clone_statement(*statement, arguments));
    for (const auto& statement : source.otherwise)
        result->otherwise.push_back(clone_statement(*statement, arguments));
    return result;
}
bool constant_default(const Expr& expr) {
    switch (expr.kind) {
    case Expr::Kind::Integer:
    case Expr::Kind::Float:
    case Expr::Kind::String:
    case Expr::Kind::Bool:
    case Expr::Kind::None:
        return true;
    case Expr::Kind::Name:
        return expr.text == "None" || expr.text == "True" || expr.text == "False";
    case Expr::Kind::Tuple:
    case Expr::Kind::List:
    case Expr::Kind::Dict:
        return std::all_of(expr.children.begin(), expr.children.end(),
                           [](const auto& child) { return constant_default(*child); });
    case Expr::Kind::Unary:
        return (expr.text == "-" || expr.text == "+" || expr.text == "~" || expr.text == "not") &&
               constant_default(*expr.children[0]);
    default:
        return false;
    }
}
struct AnalysisContext {
    std::vector<FunctionBindings> environments;
    std::vector<std::vector<std::string>> namespaces;
    std::unordered_map<const Stmt*, std::size_t> owners;
    std::unordered_set<const Stmt*> inferring_returns;
    std::vector<std::pair<Stmt*, const Stmt*>> pending_methods;
    std::size_t closure_depth = 0;
    std::size_t specialization_count = 0;
    std::size_t specialization_depth = 0;
};
enum Flow : unsigned { Next = 1, Returned = 2, Broken = 4, Continued = 8, Raised = 16 };
class Analyzer {
    AnalysisContext& context;
    const FunctionBindings& declarations;
    const FunctionBindings& functions;
    std::vector<std::unordered_map<std::string, Binding>> scopes;
    std::uint64_t& next_symbol;
    const std::vector<std::string>& namespaces;
    Type return_type = Type::Void;
    bool in_function = false;
    bool inferring_return = false;
    std::vector<Stmt*> return_sites;
    unsigned loop_depth = 0;
    std::size_t expression_depth = 0;
    std::uint64_t readonly_receiver = 0;
    TypeArguments local_types;
    Analyzer* enclosing = nullptr;
    Stmt* closure_function = nullptr;
    std::unordered_set<std::uint64_t> readonly_captures, captured_symbols;
    // Symbols of locals narrowed from an optional, and whether the last if body never falls through.
    // A narrowed local: the binding it hides, and whether that binding was in the same scope.
    struct Narrowing {
        Binding original;
        bool replaced;
    };
    std::unordered_map<std::uint64_t, Narrowing> narrowed;
    bool if_exits = false;
    // Error symbols of the except blocks being checked, innermost last, for a bare raise.
    std::vector<std::uint64_t> handlers;
    // The loop depth where the finally block being checked starts; return, break and continue cannot leave it.
    std::optional<unsigned> finally_loops;
    void ensure_return(Stmt& function, Location at) {
        if (function.return_type != Type::Unknown)
            return;
        if (context.inferring_returns.contains(&function))
            fail("E3030", at,
                 "recursive return inference for '" + function.name +
                     "'; annotate a function in this dependency cycle");
        const auto owner = context.owners.at(&function);
        Analyzer(context.environments[owner], context.namespaces[owner], next_symbol, context)
            .function_body(function);
    }
    Type concrete(Type type, Location at) {
        if (type.element)
            type.element = std::make_shared<const Type>(concrete(*type.element, at));
        if (type.key)
            type.key = std::make_shared<const Type>(concrete(*type.key, at));
        for (auto& item : type.parameters)
            item = concrete(item, at);
        if (type.kind == Type::Array && !abstract_type(*type.element) && !numeric(*type.element))
            fail("E3031", at, "array elements must be int or float");
        if (type.kind == Type::Optional)
            type = Type::optional(*type.element);
        if (type.kind == Type::Projection) {
            if (type.element->kind == Type::Record) {
                const auto& record = *type.element->declaration;
                for (const auto& method : record.body)
                    if (method->name == "iter" || (type.name == "Item" && method->name == "next")) {
                        const auto owner = context.owners.at(method.get());
                        Analyzer analyzer(context.environments[owner], context.namespaces[owner],
                                          next_symbol, context);
                        analyzer.prepare_method(*method, record);
                        analyzer.ensure_return(*method, at);
                    }
            }
            auto result = resolve_type(type, declarations, at, local_types);
            return result == type ? type : concrete(result, at);
        }
        if ((type.kind == Type::List || type.kind == Type::Dict || type.kind == Type::Optional ||
             type.kind == Type::Step || type.kind == Type::Cursor) &&
            *type.element == Type::Void)
            fail("E3002", at, "container items cannot have type void");
        if (type.kind == Type::Tuple || type.kind == Type::Record || callable_type(type))
            for (const auto& parameter : type.parameters)
                if (parameter == Type::Void)
                    fail("E3002", at, "value types and callable parameters cannot be void");
        validate_type_depth(type, at);
        if (type.kind == Type::Cursor && !abstract_type(*type.element) &&
            type.element->kind != Type::List && type.element->kind != Type::Array &&
            type.element->kind != Type::Dict && *type.element != Type::Str &&
            *type.element != Type::Range)
            fail("E3029", at, "cursor source must be a list, dictionary, string or range");
        if (type.kind == Type::Dict && !abstract_type(*type.key) && *type.key != Type::Int &&
            *type.key != Type::Str && *type.key != Type::Bool)
            fail("E3020", at, "dictionary keys must be int, str, or bool");
        if (type.kind != Type::Record || abstract_type(type) ||
            type.declaration->type_parameters.empty())
            return type;
        auto& generic = *const_cast<Stmt*>(type.declaration);
        for (auto& instance : generic.specializations)
            if (instance->type_arguments == type.parameters)
                return Type::record(*instance);
        if (context.specialization_depth >= 64 || context.specialization_count >= 1024)
            fail("E2002", at,
                 "generic instantiation exceeds 64 active levels or 1024 specializations");
        TypeArguments arguments;
        for (std::size_t i = 0; i < type.parameters.size(); ++i)
            arguments.emplace(generic.type_parameters[i], type.parameters[i]);
        for (const auto& field : generic.parameters)
            infer(field.type, substituted(field.type, arguments), arguments, at);
        auto instance = clone_statement(generic, arguments);
        instance->origin = &generic;
        instance->type_parameters.clear();
        instance->type_arguments = type.parameters;
        instance->symbol = next_symbol++;
        auto* result = instance.get();
        generic.specializations.push_back(std::move(instance));
        ++context.specialization_count;
        ++context.specialization_depth;
        const auto owner = context.owners.at(&generic);
        context.owners.emplace(result, owner);
        Analyzer analyzer(context.environments[owner], context.namespaces[owner], next_symbol,
                          context);
        try {
            for (std::size_t i = 0; i < result->parameters.size(); ++i) {
                auto& field = result->parameters[i];
                field.type = analyzer.concrete(field.type, field.location);
                if (field.type == Type::Void)
                    fail("E3002", field.location, "record fields cannot have type void");
                field.symbol = i;
            }
        } catch (const Diagnostic& error) {
            fail(error.code, error.location,
                 std::string(error.what()) + "\n  while instantiating record " +
                     type_name(Type::record(*result)));
        }
        for (auto& method : result->body) {
            method->symbol = next_symbol++;
            context.owners.emplace(method.get(), owner);
            method->origin = &generic;
        }
        --context.specialization_depth;
        return Type::record(*result);
    }
    Type resolved(Type type, Location at) {
        return concrete(resolve_type(std::move(type), declarations, at, local_types), at);
    }
    Binding* lookup(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            auto found = scope->find(name);
            if (found != scope->end())
                return &found->second;
        }
        if (enclosing) {
            if (const auto* outer = enclosing->lookup(name)) {
                const auto binding = *outer;
                if (captured_symbols.insert(binding.symbol).second) {
                    if (std::find(closure_function->mutable_captures.begin(),
                                  closure_function->mutable_captures.end(),
                                  name) == closure_function->mutable_captures.end())
                        readonly_captures.insert(binding.symbol);
                    closure_function->captures.push_back(
                        {name, binding.type, closure_function->location, binding.symbol});
                    scopes.front().emplace(name, binding);
                }
                return &scopes.front().at(name);
            }
        }
        return nullptr;
    }
    void valid_variable(const std::string& name, Location at) {
        if (reserved(name) || functions.contains(name) ||
            std::find(namespaces.begin(), namespaces.end(), name) != namespaces.end())
            fail("E3001", at, "name '" + name + "' is reserved by a type or function");
    }
    static bool standard_module(std::string_view name) {
        static constexpr std::string_view names[] = {"binary", "collections", "csv", "env", "fs", "functional", "geometry", "http", "https", "io", "iter", "linalg", "math", "net", "num", "path", "python", "random", "sqlite", "stats", "task", "time", "tls", "utf8"};
        return std::find(std::begin(names), std::end(names), name) != std::end(names);
    }
    std::string undefined_variable(const std::string& name) const {
        auto hint = name_suggestion(name, false);
        if (name == "null" || name == "nil" || name == "undefined" || name == "none" || name == "NULL")
            hint = "; wood spells this None";
        else if (hint.empty() && standard_module(name))
            hint = "; add 'import std." + name + " as " + name + "'";
        return "undefined variable '" + name + "'" + hint;
    }
    std::string unknown_function(const std::string& name) const {
        const auto suggestion = name_suggestion(name, true);
        const auto first = name.find('.');
        if (suggestion.empty() && first != std::string::npos) {
            const auto root = name.substr(0, first);
            if (std::find(namespaces.begin(), namespaces.end(), root) != namespaces.end()) {
                const auto last = name.rfind('.');
                return "unknown function '" + name + "': module '" + name.substr(0, last) + "' does not export it";
            }
            if (first == name.rfind('.') && standard_module(root))
                return "unknown function '" + name + "'; add 'import std." + root + " as " + root + "'";
        }
        return "unknown function '" + name + "'" + suggestion;
    }
    static std::string builtin_call_hint(const std::string& name) {
        static const std::pair<std::string_view, std::string_view> spellings[] = {
            {"append", "append(items, value)"}, {"pop", "pop(items)"}, {"split", "split(text, separator)"},
            {"join", "join(items, separator)"}, {"strip", "strip(text)"}, {"replace", "replace(text, old, new)"},
            {"startswith", "starts_with(text, prefix)"}, {"starts_with", "starts_with(text, prefix)"},
            {"endswith", "ends_with(text, suffix)"}, {"ends_with", "ends_with(text, suffix)"},
            {"find", "find(text, needle)"}, {"keys", "keys(items)"}, {"values", "values(items)"},
            {"get", "get(items, key, fallback)"}, {"remove", "remove(items, key)"}, {"len", "len(value)"},
            {"length", "len(value)"}, {"size", "len(value)"}, {"count", "len(value)"}};
        for (const auto& [word, spelling] : spellings)
            if (word == name)
                return "; call it as " + std::string(spelling);
        return {};
    }
    std::string record_member_problem(const Type& base, const std::string& name) const {
        std::vector<std::string> names;
        for (const auto& field : base.declaration->parameters)
            names.push_back(field.name);
        for (const auto& item : base.declaration->body) {
            if (item->kind != Stmt::Kind::Function)
                continue;
            if (item->name == name)
                return "method '" + name + "' must be called, as " + name + "(...)";
            if (item->name.empty() || item->name[0] != '_')
                names.push_back(item->name + "()");
        }
        std::vector<std::string> close;
        for (const auto& candidate : names)
            if (similar_identifier(name, candidate.substr(0, candidate.find('('))))
                close.push_back(candidate);
        std::string hint;
        if (close.size() == 1) {
            hint = "; did you mean '" + close[0].substr(0, close[0].find('(')) + "'?";
        } else if (!names.empty() && names.size() <= 12) {
            hint = "; it has ";
            for (std::size_t i = 0; i < names.size(); ++i)
                hint += (i ? ", " : "") + names[i];
        }
        return type_name(base) + " has no field or method '" + name + "'" + hint;
    }
    std::string name_suggestion(std::string_view name, bool call) const {
        std::string_view match;
        bool ambiguous = false;
        auto consider = [&](std::string_view candidate) {
            if (!similar_identifier(name, candidate))
                return;
            if (match.empty())
                match = candidate;
            else if (match != candidate)
                ambiguous = true;
        };
        // Inspect bindings without lookup(), which can create closure captures.
        std::unordered_set<std::string_view> visible;
        for (auto* analyzer = this; analyzer; analyzer = analyzer->enclosing)
            for (auto scope = analyzer->scopes.rbegin(); scope != analyzer->scopes.rend(); ++scope)
                for (const auto& [candidate, binding] : *scope)
                    if (visible.insert(candidate).second && (!call || callable_type(binding.type)))
                        consider(candidate);
        for (const auto& [candidate, declaration] : functions)
            if (!visible.contains(candidate) && (call || declaration->kind == Stmt::Kind::Function))
                consider(candidate);
        if (call)
            for (const auto& builtin : builtins)
                consider(builtin.name);
        return match.empty() || ambiguous ? std::string{}
                                          : "; did you mean '" + std::string(match) + "'?";
    }
    void require(const Type& actual, const Type& expected, Location at,
                 std::string_view detail = {}) {
        if (!assignable(expected, actual)) {
            const auto wanted = type_name(expected), received = type_name(actual);
            fail("E3002", at,
                 "expected " + wanted + ", got " + received +
                     (detail.empty() ? "" : " for " + std::string(detail)) +
                     (wanted == received
                          ? "; these types contain distinct nominal record declarations"
                          : ""));
        }
    }
    void arity(const Expr& expr, std::size_t minimum, std::size_t maximum) {
        if (expr.children.size() < minimum || expr.children.size() > maximum)
            fail("E3003", expr.location,
                 "wrong number of arguments to '" + expr.text + "' (expected " +
                     std::to_string(minimum) +
                     (minimum == maximum ? "" : ".." + std::to_string(maximum)) + ", got " +
                     std::to_string(expr.children.size()) + ")");
    }
    void infer(const Type& pattern, const Type& actual, TypeArguments& arguments, Location at) {
        if (actual == Type::Unknown)
            return;
        if (pattern.kind == Type::Signature) {
            if (!callable_type(actual) || pattern.parameters.size() != actual.parameters.size())
                fail("E3028", at, "argument does not match callable pattern " + type_name(pattern));
            const auto [found, inserted] = arguments.emplace(pattern.name, actual);
            if (!inserted && found->second != actual)
                fail("E3024", at, "conflicting callable types for " + pattern.name);
            for (std::size_t i = 0; i < pattern.parameters.size(); ++i) {
                infer(pattern.parameters[i], actual.parameters[i], arguments, at);
                require(actual.parameters[i],
                        concrete(substituted(pattern.parameters[i], arguments), at), at);
            }
            if (*pattern.element != Type::Unknown) {
                infer(*pattern.element, *actual.element, arguments, at);
                require(*actual.element, concrete(substituted(*pattern.element, arguments), at),
                        at);
            }
            return;
        }
        if (pattern.kind == Type::Parameter) {
            if (actual == Type::Void)
                fail("E3024", at,
                     "cannot infer type parameter '" + pattern.name + "' from " +
                         type_name(actual));
            const auto [found, inserted] = arguments.emplace(pattern.name, actual);
            if (!inserted && found->second != actual)
                fail("E3024", at,
                     "conflicting types for '" + pattern.name + "': " + type_name(found->second) +
                         " and " + type_name(actual));
            return;
        }
        if (pattern.kind == Type::Optional) {
            if (actual != Type::None)
                infer(*pattern.element, actual.kind == Type::Optional ? *actual.element : actual,
                      arguments, at);
            return;
        }
        if (pattern.kind != actual.kind && !(callable_type(pattern) && callable_type(actual)))
            return; // The ordinary assignability check supplies the concrete diagnostic.
        if (pattern.kind == Type::Record &&
            (pattern.declaration->origin ? pattern.declaration->origin : pattern.declaration) !=
                (actual.declaration->origin ? actual.declaration->origin : actual.declaration))
            return;
        if (pattern.element)
            infer(*pattern.element, *actual.element, arguments, at);
        if (pattern.key)
            infer(*pattern.key, *actual.key, arguments, at);
        if (pattern.parameters.size() == actual.parameters.size())
            for (std::size_t i = 0; i < pattern.parameters.size(); ++i)
                infer(pattern.parameters[i], actual.parameters[i], arguments, at);
    }
    Stmt& specialize(Stmt& generic, const TypeArguments& arguments, Location at) {
        std::vector<Type> types;
        for (const auto& parameter : generic.type_parameters) {
            const auto found = arguments.find(parameter);
            if (found == arguments.end())
                fail("E3024", at,
                     "cannot infer type parameter '" + parameter + "' of '" + generic.name + "'");
            types.push_back(found->second);
        }
        for (auto& instance : generic.specializations)
            if (instance->type_arguments == types)
                return *instance;
        if (context.specialization_depth >= 64 || context.specialization_count >= 1024)
            fail("E2002", at,
                 "generic instantiation exceeds 64 active levels or 1024 specializations");
        auto instance = clone_statement(generic, arguments);
        instance->type_parameters.clear();
        instance->type_arguments = std::move(types);
        instance->symbol = next_symbol++;
        auto* result = instance.get();
        // Cache before body analysis, so direct and mutual recursion share symbols.
        generic.specializations.push_back(std::move(instance));
        ++context.specialization_count;
        ++context.specialization_depth;
        const auto owner = context.owners.at(&generic);
        Analyzer analyzer(context.environments[owner], context.namespaces[owner], next_symbol,
                          context);
        analyzer.local_types = arguments;
        try {
            analyzer.function_body(*result);
        } catch (const Diagnostic& error) {
            fail(error.code, error.location,
                 std::string(error.what()) + "\n  while instantiating function '" + generic.name +
                     "'");
        }
        --context.specialization_depth;
        return *result;
    }
    Type function_value(Expr& expr, Stmt& declaration, Type expected) {
        if (std::any_of(declaration.parameters.begin(), declaration.parameters.end(),
                        [](const auto& p) { return p.mutable_borrow; }))
            fail("E3023", expr.location, "mutable native array imports must be called by name");
        auto* target = &declaration;
        if (!declaration.type_parameters.empty()) {
            if (expected.kind != Type::Function && expected.kind != Type::Callable)
                fail("E3024", expr.location,
                     "generic function value requires a concrete fn annotation");
            std::vector<Type> parameters;
            for (const auto& item : declaration.parameters)
                parameters.push_back(item.type);
            TypeArguments arguments;
            infer(Type::callable(std::move(parameters), declaration.return_type), expected,
                  arguments, expr.location);
            target = &specialize(declaration, arguments, expr.location);
        }
        expr.symbol = target->symbol;
        expr.refers_function = true;
        ensure_return(*target, expr.location);
        std::vector<Type> parameters;
        for (const auto& p : target->parameters)
            parameters.push_back(p.type);
        return Type::callable(std::move(parameters), target->return_type);
    }
    std::string call_expectation(const Stmt& function, std::size_t provided) const {
        std::string result = " (expected " + function.name + "(";
        const std::size_t first = function.method ? 1 : 0;
        for (std::size_t i = first; i < function.parameters.size(); ++i) {
            const auto& parameter = function.parameters[i];
            if (i != first)
                result += ", ";
            if (parameter.mode == Parameter::KeywordOnly &&
                (i == first || function.parameters[i - 1].mode != Parameter::KeywordOnly))
                result += "*, ";
            result += parameter.name + ": " + type_name(parameter.type);
            if (parameter.default_value)
                result += " = ...";
            if (parameter.mode == Parameter::PositionalOnly &&
                (i + 1 == function.parameters.size() ||
                 function.parameters[i + 1].mode != Parameter::PositionalOnly))
                result += ", /";
        }
        const auto count = provided - first;
        return result + "); provided " + std::to_string(count) +
               (count == 1 ? " argument)" : " arguments)");
    }
    void bind_arguments(Expr& expr, const Stmt& function) {
        const auto provided = expr.children.size();
        auto argument_error = [&](Location at, std::string message) {
            fail("E3003", at, std::move(message) + call_expectation(function, provided));
        };
        std::vector<bool> supplied(function.parameters.size(), false);
        std::size_t positional = 0;
        for (std::size_t i = 0; i < expr.children.size(); ++i) {
            const auto keyword = expr.keywords.empty() ? std::string{} : expr.keywords[i];
            std::size_t parameter = positional;
            if (keyword.empty()) {
                if (parameter >= function.parameters.size() ||
                    function.parameters[parameter].mode == Parameter::KeywordOnly)
                    argument_error(expr.children[i]->location,
                                   "too many positional arguments to '" + function.name + "'");
                ++positional;
            } else {
                const auto found =
                    std::find_if(function.parameters.begin(), function.parameters.end(),
                                 [&](const auto& p) { return p.name == keyword; });
                if (found == function.parameters.end())
                    argument_error(expr.children[i]->location,
                                   "unknown keyword '" + keyword + "' to '" + function.name + "'");
                if (found->mode == Parameter::PositionalOnly)
                    argument_error(expr.children[i]->location, "parameter '" + keyword +
                                                                   "' is positional-only in '" +
                                                                   function.name + "'");
                parameter = static_cast<std::size_t>(found - function.parameters.begin());
            }
            if (supplied[parameter])
                argument_error(expr.children[i]->location, "multiple values for parameter '" +
                                                               function.parameters[parameter].name +
                                                               "' in '" + function.name + "'");
            supplied[parameter] = true;
            expr.argument_order.push_back(parameter);
        }
        for (std::size_t i = 0; i < supplied.size(); ++i)
            if (!supplied[i]) {
                if (!function.parameters[i].default_value)
                    argument_error(expr.location, "missing argument '" +
                                                      function.parameters[i].name + "' to '" +
                                                      function.name + "'");
                expr.children.push_back(clone_expression(*function.parameters[i].default_value));
                expr.argument_order.push_back(i);
            }
    }
    Type declared_call(Expr& expr, Stmt& declaration, Type expected) {
        bind_arguments(expr, declaration);
        TypeArguments arguments;
        if (!expr.type_arguments.empty()) {
            if (declaration.kind != Stmt::Kind::Record ||
                expr.type_arguments.size() != declaration.type_parameters.size())
                fail("E3024", expr.location, "wrong number of record type arguments");
            for (std::size_t i = 0; i < expr.type_arguments.size(); ++i)
                arguments.emplace(declaration.type_parameters[i],
                                  resolved(expr.type_arguments[i], expr.location));
        }
        if (declaration.kind == Stmt::Kind::Record && expected.kind == Type::Record &&
            (expected.declaration->origin ? expected.declaration->origin : expected.declaration) ==
                &declaration)
            for (std::size_t i = 0; i < declaration.type_parameters.size(); ++i)
                arguments.emplace(declaration.type_parameters[i], expected.parameters[i]);
        const bool fixed_record = declaration.kind == Stmt::Kind::Record && !arguments.empty();
        std::vector<Type> actual(expr.children.size());
        std::vector<std::size_t> deferred;
        auto check_argument = [&](std::size_t i, bool allow_defer) {
            const auto& pattern = declaration.parameters[expr.argument_order[i]].type;
            auto hint = concrete(substituted(pattern, arguments), expr.location);
            if (hint.kind == Type::Signature) {
                hint.kind = Type::Callable;
                hint.name.clear();
            }
            const auto& value = *expr.children[i];
            const auto named =
                value.kind == Expr::Kind::Name ? functions.find(value.text) : functions.end();
            const bool needs_context =
                (value.kind == Expr::Kind::Closure &&
                 std::any_of(value.function->parameters.begin(), value.function->parameters.end(),
                             [](const auto& p) { return p.type == Type::Unknown; })) ||
                (named != functions.end() && !named->second->type_parameters.empty() &&
                 !lookup(value.text));
            if (allow_defer && needs_context && abstract_type(hint)) {
                deferred.push_back(i);
                return;
            }
            if (callable_type(hint)) {
                for (auto& parameter : hint.parameters)
                    if (abstract_type(parameter))
                        parameter = Type::Unknown;
                if (abstract_type(*hint.element))
                    hint.element = std::make_shared<const Type>(Type::Unknown);
            }
            auto type =
                expression(*expr.children[i], abstract_type(hint) ? Type(Type::Unknown) : hint);
            if (!declaration.type_parameters.empty() && !fixed_record)
                infer(pattern, type, arguments, expr.children[i]->location);
            actual[i] = type;
        };
        for (std::size_t i = 0; i < expr.children.size(); ++i)
            check_argument(i, true);
        for (const auto i : deferred)
            check_argument(i, false);
        if (declaration.kind == Stmt::Kind::Function && !declaration.type_parameters.empty() &&
            expected != Type::Unknown)
            infer(declaration.return_type, expected, arguments, expr.location);
        Stmt* selected = &declaration;
        if (!declaration.type_parameters.empty()) {
            if (declaration.kind == Stmt::Kind::Record) {
                auto type = Type::record(declaration);
                for (const auto& name : declaration.type_parameters) {
                    if (!arguments.contains(name))
                        fail("E3024", expr.location,
                             "cannot infer type parameter '" + name + "' of '" + declaration.name +
                                 "'; use a concrete record annotation");
                    type.parameters.push_back(arguments.at(name));
                }
                selected = const_cast<Stmt*>(concrete(type, expr.location).declaration);
            } else
                selected = &specialize(declaration, arguments, expr.location);
        }
        auto& target = *selected;
        if (target.kind == Stmt::Kind::Function)
            ensure_return(target, expr.location);
        for (std::size_t i = 0; i < actual.size(); ++i) {
            const auto& parameter = target.parameters[expr.argument_order[i]];
            if (!assignable(parameter.type, actual[i]))
                require(actual[i], parameter.type, expr.children[i]->location,
                        "argument '" + parameter.name + "' to '" + target.name + "'");
        }
        for (std::size_t i = 0; i < actual.size(); ++i)
            if (target.parameters[expr.argument_order[i]].mutable_borrow)
                lvalue(*expr.children[i]);
        expr.callee = &target;
        expr.symbol = target.symbol;
        expr.constructs = target.kind == Stmt::Kind::Record;
        return expr.constructs ? Type::record(target) : target.return_type;
    }
    Type invoke(Expr& expr, Type expected = Type::Unknown) {
        if (expr.children[0]->kind == Expr::Kind::TypeApply) {
            auto target = std::move(expr.children[0]);
            std::function<std::string(const Expr&)> qualified =
                [&](const Expr& part) -> std::string {
                if (part.kind == Expr::Kind::Name)
                    return part.text;
                if (part.kind == Expr::Kind::Member)
                    return qualified(*part.children[0]) + "." + part.text;
                return {};
            };
            const auto name = qualified(*target->children[0]);
            const auto found = functions.find(name);
            if (found != functions.end() && found->second->kind == Stmt::Kind::Record &&
                !lookup(name.substr(0, name.find('.')))) {
                expr.text = name;
                expr.type_arguments = std::move(target->type_arguments);
                expr.children.erase(expr.children.begin());
                expr.kind = Expr::Kind::Call;
                return declared_call(expr, *found->second, expected);
            }
            if (!target->children[1])
                fail("E3024", expr.location,
                     "explicit type applications require a record constructor");
            target->kind = Expr::Kind::Index;
            target->type_arguments.clear();
            expr.children[0] = std::move(target);
        }
        if (expr.children[0]->kind == Expr::Kind::Member) {
            auto& member = *expr.children[0];
            const auto receiver = expression(*member.children[0]);
            if (receiver.kind == Type::Record) {
                auto& record = *const_cast<Stmt*>(receiver.declaration);
                const auto method =
                    std::find_if(record.body.begin(), record.body.end(),
                                 [&](const auto& item) { return item->name == member.text; });
                if (method != record.body.end()) {
                    if ((*method)->mutating)
                        lvalue(*member.children[0]);
                    const auto owner = context.owners.at(method->get());
                    Analyzer analyzer(context.environments[owner], context.namespaces[owner],
                                      next_symbol, context);
                    try {
                        analyzer.prepare_method(**method, record);
                    } catch (const Diagnostic& error) {
                        fail(error.code, error.location,
                             std::string(error.what()) + "\n  while checking method " +
                                 type_name(receiver) + "." + member.text);
                    }
                    expr.text = member.text;
                    auto receiver_expression = std::move(member.children[0]);
                    expr.children[0] = std::move(receiver_expression);
                    expr.keywords.insert(expr.keywords.begin(), "");
                    expr.kind = Expr::Kind::Call;
                    return declared_call(expr, **method, expected);
                }
            }
        }
        const auto target = expression(*expr.children[0]);
        if (target.kind == Type::Callable ||
            (target.kind == Type::Closure && target.declaration->mutating)) {
            const Expr* root = expr.children[0].get();
            while (root->kind == Expr::Kind::Member || root->kind == Expr::Kind::Index)
                root = root->children[0].get();
            if (root->kind == Expr::Kind::Name && !root->refers_function)
                lvalue(*expr.children[0]);
        }
        if (!callable_type(target))
            fail("E3002", expr.location, type_name(target) + " cannot be called, because it is not a function");
        if (target.kind == Type::Closure) {
            auto callee = std::move(expr.children[0]);
            expr.children.erase(expr.children.begin());
            bind_arguments(expr, *target.declaration);
            expr.children.insert(expr.children.begin(), std::move(callee));
        } else if (std::any_of(expr.keywords.begin(), expr.keywords.end(),
                               [](const auto& keyword) { return !keyword.empty(); }))
            fail("E3003", expr.location,
                 "erased callable and function pointers use positional arguments");
        if (expr.children.size() - 1 != target.parameters.size())
            fail("E3003", expr.location,
                 "wrong number of arguments to function value (expected " +
                     std::to_string(target.parameters.size()) + ", got " +
                     std::to_string(expr.children.size() - 1) + ")");
        for (std::size_t i = 0; i < target.parameters.size(); ++i) {
            const auto& parameter =
                target.parameters[expr.argument_order.empty() ? i : expr.argument_order[i]];
            const auto actual = expression(*expr.children[i + 1], parameter);
            if (!assignable(parameter, actual))
                require(actual, parameter, expr.children[i + 1]->location,
                        "argument " + std::to_string(i + 1) + " to function value");
        }
        return *target.element;
    }
    Stmt* protocol_method(const Type& receiver, const std::string& name, Location at) {
        if (receiver.kind != Type::Record)
            return nullptr;
        for (const auto& method : receiver.declaration->body)
            if (method->name == name) {
                if (method->parameters.size() != 1 || !method->type_parameters.empty() ||
                    (name == "next" ? !method->mutating : method->mutating))
                    fail("E3029", at,
                         "iterator protocol requires fn iter(self) and fn next(mut self) without "
                         "additional parameters");
                const auto owner = context.owners.at(method.get());
                Analyzer analyzer(context.environments[owner], context.namespaces[owner],
                                  next_symbol, context);
                analyzer.prepare_method(*method, *receiver.declaration);
                analyzer.ensure_return(*method, at);
                return method.get();
            }
        return nullptr;
    }
    Type iterator_element(const Type& cursor, Location at, const Stmt*& method) {
        if (cursor == Type::Range)
            return Type::Int;
        if (cursor.kind == Type::Cursor) {
            const auto& source = *cursor.element;
            if (source.kind == Type::List || source.kind == Type::Array)
                return *source.element;
            if (source.kind == Type::Dict)
                return *source.key;
            if (source == Type::Str)
                return Type::Str;
            if (source == Type::Range)
                return Type::Int;
        }
        if (auto* next = protocol_method(cursor, "next", at)) {
            if (next->return_type.kind != Type::Step)
                fail("E3029", at, "iterator next must return step[T]");
            method = next;
            return *next->return_type.element;
        }
        fail("E3029", at, "type " + type_name(cursor) + " is not an iterator");
    }
    Type call(Expr& expr, Type expected_result) {
        const auto found = functions.find(expr.text);
        const auto dot = expr.text.find('.');
        const auto* local = lookup(expr.text.substr(0, dot));
        if (local &&
            (dot != std::string::npos || callable_type(local->type) || !find_builtin(expr.text))) {
            // Resolve a local callable (or callable field) before fallback builtins.
            auto target =
                std::make_unique<Expr>(Expr::Kind::Name, expr.location, expr.text.substr(0, dot));
            auto cursor = dot;
            std::size_t depth = 1;
            while (cursor != std::string::npos) {
                if (++depth >= 256)
                    fail("E2002", expr.location, "expression nesting exceeds 256 levels");
                const auto end = expr.text.find('.', cursor + 1);
                auto member = std::make_unique<Expr>(
                    Expr::Kind::Member, expr.location,
                    expr.text.substr(cursor + 1,
                                     end == std::string::npos ? end : end - cursor - 1));
                member->children.push_back(std::move(target));
                target = std::move(member);
                cursor = end;
            }
            expr.children.insert(expr.children.begin(), std::move(target));
            expr.kind = Expr::Kind::Invoke;
            return invoke(expr, expected_result);
        }
        const Builtin* builtin = found == functions.end() ? find_builtin(expr.text) : nullptr;
        if (!builtin && found == functions.end())
            fail("E3005", expr.location, unknown_function(expr.text));
        if (found != functions.end())
            return declared_call(expr, *found->second, expected_result);
        if (std::any_of(expr.keywords.begin(), expr.keywords.end(),
                        [](const auto& keyword) { return !keyword.empty(); }))
            fail("E3003", expr.location, "builtins use positional arguments");
        if (expr.text == "range") {
            arity(expr, 1, 3);
            for (auto& argument : expr.children)
                require(expression(*argument), Type::Int, argument->location);
            return Type::Range;
        }
        if (expr.text == "finished") {
            arity(expr, 0, 0);
            if (expected_result.kind != Type::Step)
                fail("E3029", expr.location, "finished() requires a step[T] context");
            return expected_result;
        }
        if (expr.text == "yielded") {
            arity(expr, 1, 1);
            const auto hint =
                expected_result.kind == Type::Step ? *expected_result.element : Type(Type::Unknown);
            const auto value = expression(*expr.children[0], hint);
            if (value == Type::Void)
                fail("E3029", expr.location, "iterator items cannot be void");
            if (hint != Type::Unknown)
                require(value, hint, expr.location);
            return Type::wrapped(Type::Step, hint == Type::Unknown ? value : hint);
        }
        if (expr.text == "has_item" || expr.text == "step_value") {
            arity(expr, 1, 1);
            const auto step = expression(*expr.children[0]);
            if (step.kind != Type::Step)
                fail("E3029", expr.location, "expected step[T]");
            return expr.text == "has_item" ? Type(Type::Bool) : *step.element;
        }
        if (expr.text == "iter" || expr.text == "next") {
            arity(expr, 1, 1);
            const auto source = expression(*expr.children[0]);
            if (expr.text == "iter") {
                if (source.kind == Type::List || source.kind == Type::Array ||
                    source.kind == Type::Dict || source == Type::Str || source == Type::Range)
                    return Type::wrapped(Type::Cursor, source);
                if (auto* method = protocol_method(source, "iter", expr.location)) {
                    expr.keywords = {""};
                    auto result = declared_call(expr, *method, expected_result);
                    const Stmt* next = nullptr;
                    iterator_element(result, expr.location, next);
                    return result;
                }
                const Stmt* next = nullptr;
                iterator_element(source, expr.location, next);
                return source; // Value snapshot of an already constructed cursor.
            }
            lvalue(*expr.children[0]);
            const Stmt* next = nullptr;
            const auto item = iterator_element(source, expr.location, next);
            if (next) {
                expr.keywords = {""};
                return declared_call(expr, *const_cast<Stmt*>(next), expected_result);
            }
            return Type::wrapped(Type::Step, item);
        }
        if (expr.text == "append")
            arity(expr, 2, 2);
        if (expr.text == "pop")
            arity(expr, 1, 1);
        std::vector<Type> args;
        for (std::size_t i = 0; i < expr.children.size(); ++i) {
            auto& child = expr.children[i];
            Type expected;
            if (expr.text == "array_full" && i == 0)
                expected = Type::list(Type::Int);
            else if (expr.text == "array_from_list" && i == 0 &&
                     expected_result.kind == Type::Array)
                expected = Type::list(*expected_result.element);
            else if ((expr.text == "array_reshape" || expr.text == "array_transpose") && i == 1)
                expected = Type::list(Type::Int);
            else if (expr.text == "append" && i == 1 && args[0].kind == Type::List)
                expected = *args[0].element;
            else if (expr.text == "join" && i == 0)
                expected = Type::list(Type::Str);
            else if ((expr.text == "get" || expr.text == "remove") && i == 1 &&
                     args[0].kind == Type::Dict)
                expected = *args[0].key;
            else if (expr.text == "get" && i == 2 && args[0].kind == Type::Dict)
                expected = *args[0].element;
            else if ((expr.text == "json" || expr.text == "json_dump") && i == 0 &&
                     child->children.empty()) {
                if (child->kind == Expr::Kind::List)
                    expected = Type::list(Type::Json);
                if (child->kind == Expr::Kind::Dict)
                    expected = Type::dict(Type::Str, Type::Json);
            }
            auto type = expression(*child, expected);
            if (type == Type::Void)
                fail("E3002", child->location, "void expression cannot be used as an argument");
            args.push_back(type);
        }
        return builtin_call(expr, args, builtin);
    }
    // Keep builtin initialization temporaries off the recursive call-analysis
    // stack. Deep user calls must not reserve the entire builtin checker frame.
    Type builtin_call(Expr& expr, const std::vector<Type>& args, const Builtin* builtin) {
        if (builtin && expr.text.starts_with("array_")) {
            const auto name = expr.text;
            if (name == "array_full") {
                arity(expr, 2, 2);
                const auto& shape = args[0];
                if (shape != Type::Int && shape != Type::list(Type::Int) &&
                    !(shape.kind == Type::Tuple && !shape.parameters.empty() &&
                      shape.parameters.size() <= 8 &&
                      std::all_of(shape.parameters.begin(), shape.parameters.end(),
                                  [](const auto& t) { return t == Type::Int; })))
                    fail("E3031", expr.location,
                         "array shape requires an int, integer tuple of rank 1..8, or list[int]");
                if (!numeric(args[1]))
                    fail("E3031", expr.location, "array fill must be int or float");
                return Type::wrapped(Type::Array, args[1]);
            }
            if (name == "array_from_list") {
                arity(expr, 1, 1);
                if (args[0].kind != Type::List || !numeric(*args[0].element))
                    fail("E3031", expr.location,
                         "array construction requires list[int] or list[float]");
                return Type::wrapped(Type::Array, *args[0].element);
            }
            const std::size_t arguments =
                name == "array_slice" ? 5
                : name == "array_dot" || name == "array_matmul" || name == "array_affine" ||
                        name == "array_reshape" || name == "array_transpose"
                    ? 2
                    : 1;
            arity(expr, arguments, arguments);
            if (args[0].kind != Type::Array)
                fail("E3031", expr.location, "numerical operation requires an array");
            if (name == "array_shape")
                return Type::list(Type::Int);
            if (name == "array_rank")
                return Type::Int;
            if (name == "array_contiguous")
                return Type::Bool;
            if (name == "array_sum")
                return *args[0].element;
            if (name == "array_to_list")
                return Type::list(*args[0].element);
            if (name == "array_floats" || name == "array_integers")
                return Type::wrapped(Type::Array, name == "array_floats" ? Type::Float : Type::Int);
            if (name == "array_transpose")
                require(args[1], Type::list(Type::Int), expr.location);
            if (name == "array_reshape") {
                const auto& shape = args[1];
                if (shape != Type::Int && shape != Type::list(Type::Int) &&
                    !(shape.kind == Type::Tuple && !shape.parameters.empty() &&
                      shape.parameters.size() <= 8 &&
                      std::all_of(shape.parameters.begin(), shape.parameters.end(),
                                  [](const auto& t) { return t == Type::Int; })))
                    fail("E3031", expr.location, "invalid reshape dimensions");
            }
            if (name == "array_slice")
                for (std::size_t i = 1; i < args.size(); ++i)
                    require(args[i], Type::Int, expr.location);
            if (name == "array_dot" || name == "array_matmul" || name == "array_affine") {
                require(args[1], args[0], expr.location);
                if (name == "array_dot")
                    return *args[0].element;
                if (*args[0].element != Type::Float)
                    fail("E3031", expr.location,
                         "matrix and affine kernels require array[float]; use num.floats");
            }
            return args[0];
        }
        if (expr.text == "math_sqrt" || expr.text == "math_sin" || expr.text == "math_cos" ||
            expr.text == "math_finite") {
            arity(expr, 1, 1);
            require(args[0], Type::Float, expr.location);
            return expr.text == "math_finite" ? Type::Bool : Type::Float;
        }
        if (expr.text == "char_count" || expr.text == "chars") {
            arity(expr, 1, 1);
            require(args[0], Type::Str, expr.location);
            return expr.text == "chars" ? Type::list(Type::Str) : Type(Type::Int);
        }
        if (expr.text == "char_slice") {
            arity(expr, 3, 3);
            require(args[0], Type::Str, expr.location);
            require(args[1], Type::Int, expr.location);
            require(args[2], Type::Int, expr.location);
            return Type::Str;
        }
        if (expr.text == "error") {
            arity(expr, 2, 2);
            require(args[0], Type::Str, expr.location);
            require(args[1], Type::Str, expr.location);
            return Type::Error;
        }
        if (expr.text == "unwrap") {
            arity(expr, 1, 1);
            auto& argument = *expr.children[0];
            Type value = args[0];
            const auto narrowing = narrowed.find(argument.symbol);
            if (value.kind != Type::Optional && argument.kind == Expr::Kind::Name &&
                narrowing != narrowed.end()) {
                // unwrap(x) on a narrowed x reads the original optional, so code written before narrowing still works.
                argument.symbol = narrowing->second.original.symbol;
                argument.type = narrowing->second.original.type;
                value = argument.type;
            }
            if (value.kind != Type::Optional)
                fail("E3002", expr.location, "unwrap requires an optional value");
            return *value.element;
        }
        if (expr.text == "json_null") {
            arity(expr, 0, 0);
            return Type::Json;
        }
        if (builtin) {
            struct Signature {
                std::vector<Type> parameters;
                Type result;
            };
            static const auto job = Type::callable({Type::Json}, Type::Json);
            static const auto io_job = Type::callable({Type::Stream, Type::Json}, Type::Json);
            static const std::map<std::string_view, Signature> native = {
                {"db_open", {{Type::Str, Type::Int}, Type::Database}},
                {"db_close", {{Type::Database}, Type::Void}},
                {"db_closed", {{Type::Database}, Type::Bool}},
                {"db_changes", {{Type::Database}, Type::Int}},
                {"db_last_id", {{Type::Database}, Type::Int}},
                {"db_prepare", {{Type::Database, Type::Str}, Type::Statement}},
                {"db_begin", {{Type::Database}, Type::Transaction}},
                {"db_commit", {{Type::Transaction}, Type::Void}},
                {"db_rollback", {{Type::Transaction}, Type::Void}},
                {"db_version", {{}, Type::Str}},
                {"stmt_close", {{Type::Statement}, Type::Void}},
                {"stmt_step", {{Type::Statement}, Type::Bool}},
                {"stmt_reset", {{Type::Statement}, Type::Void}},
                {"stmt_bind_int", {{Type::Statement, Type::Int, Type::Int}, Type::Void}},
                {"stmt_bind_float", {{Type::Statement, Type::Int, Type::Float}, Type::Void}},
                {"stmt_bind_text", {{Type::Statement, Type::Int, Type::Str}, Type::Void}},
                {"stmt_bind_blob", {{Type::Statement, Type::Int, Type::Str}, Type::Void}},
                {"stmt_bind_null", {{Type::Statement, Type::Int}, Type::Void}},
                {"stmt_columns", {{Type::Statement}, Type::Int}},
                {"stmt_name", {{Type::Statement, Type::Int}, Type::Str}},
                {"stmt_kind", {{Type::Statement, Type::Int}, Type::Str}},
                {"stmt_int", {{Type::Statement, Type::Int}, Type::Int}},
                {"stmt_float", {{Type::Statement, Type::Int}, Type::Float}},
                {"stmt_text", {{Type::Statement, Type::Int}, Type::Str}},
                {"stmt_blob", {{Type::Statement, Type::Int}, Type::Str}},
                {"task_spawn", {{job, Type::Json}, Type::Task}},
                {"task_spawn_io", {{io_job, Type::Stream, Type::Json}, Type::Task}},
                {"task_wait", {{Type::Task, Type::Int}, Type::Json}},
                {"task_done", {{Type::Task}, Type::Bool}},
                {"task_cancel", {{Type::Task}, Type::Void}},
                {"task_close", {{Type::Task}, Type::Void}},
                {"task_cancelled", {{}, Type::Bool}},
                {"task_checkpoint", {{}, Type::Void}},
                {"random_bytes", {{Type::Int}, Type::Str}},
                {"tls_context_new", {{Type::Str, Type::Str}, Type::TlsContext}},
                {"tls_context_close", {{Type::TlsContext}, Type::Void}},
                {"tls_accept", {{Type::Stream, Type::TlsContext}, Type::Stream}},
                {"tls_connect", {{Type::Str, Type::Int, Type::Str, Type::Int}, Type::Stream}},
                {"tls_version", {{}, Type::Str}}};
            if (const auto signature = native.find(expr.text); signature != native.end()) {
                arity(expr, signature->second.parameters.size(),
                      signature->second.parameters.size());
                for (std::size_t i = 0; i < args.size(); ++i)
                    require(args[i], signature->second.parameters[i], expr.children[i]->location);
                return signature->second.result;
            }
        }
        if (builtin && (expr.text.starts_with("stream_") || expr.text.starts_with("tcp_"))) {
            std::vector<Type> expected;
            Type result = Type::Void;
            if (expr.text == "stream_open") {
                expected = {Type::Str, Type::Str};
                result = Type::Stream;
            } else if (expr.text == "tcp_connect" || expr.text == "tcp_listen") {
                expected = {Type::Str, Type::Int, Type::Int};
                result = Type::Stream;
            } else if (expr.text == "tcp_accept") {
                expected = {Type::Stream};
                result = Type::Stream;
            } else if (expr.text == "stream_read" || expr.text == "stream_read_line") {
                expected = {Type::Stream, Type::Int};
                result = Type::Str;
            } else if (expr.text == "stream_write") {
                expected = {Type::Stream, Type::Str};
            } else if (expr.text == "stream_seek" || expr.text == "stream_timeout") {
                expected = {Type::Stream, Type::Int};
            } else {
                expected = {Type::Stream};
                if (expr.text == "stream_closed")
                    result = Type::Bool;
                if (expr.text == "stream_tell" || expr.text == "tcp_port")
                    result = Type::Int;
            }
            arity(expr, expected.size(), expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i)
                require(args[i], expected[i], expr.children[i]->location);
            return result;
        }
        if (expr.text == "time_sleep") {
            arity(expr, 1, 1);
            require(args[0], Type::Float, expr.location);
            return Type::Void;
        }
        if (expr.text == "env_get") {
            arity(expr, 1, 1);
            require(args[0], Type::Str, expr.children[0]->location);
            return Type::optional(Type::Str);
        }
        if (expr.text == "time_monotonic" || expr.text == "time_monotonic_ns") {
            arity(expr, 0, 0);
            return expr.text == "time_monotonic" ? Type::Float : Type::Int;
        }
        if (expr.text == "fs_list") {
            arity(expr, 2, 2);
            require(args[0], Type::Str, expr.location);
            require(args[1], Type::Int, expr.location);
            return Type::list(Type::Str);
        }
        if (expr.text == "path_resolve") {
            arity(expr, 2, 2);
            require(args[0], Type::Str, expr.location);
            require(args[1], Type::Bool, expr.location);
            return Type::Str;
        }
        if (expr.text == "fs_kind" || expr.text == "fs_size" || expr.text == "fs_mkdirs" ||
            expr.text == "fs_remove" || expr.text == "path_join" || expr.text == "path_name" ||
            expr.text == "path_parent" || expr.text == "path_absolute" ||
            expr.text == "path_normalize" || expr.text == "path_identity" || expr.text == "path_within" || expr.text == "path_is_absolute") {
            const std::size_t count = expr.text == "path_join" || expr.text == "path_within" ? 2 : 1;
            arity(expr, count, count);
            for (const auto& arg : args)
                require(arg, Type::Str, expr.location);
            if (expr.text == "fs_size")
                return Type::Int;
            if (expr.text == "fs_mkdirs" || expr.text == "fs_remove" || expr.text == "path_within" || expr.text == "path_is_absolute")
                return Type::Bool;
            return Type::Str;
        }
        if (expr.text == "json" || expr.text == "json_dump") {
            arity(expr, 1, 1);
            if (!json_compatible(args[0]))
                fail("E3022", expr.location,
                     "JSON conversion requires supported values and string dictionary keys");
            return expr.text == "json" ? Type(Type::Json) : Type(Type::Str);
        }
        if (expr.text == "json_parse") {
            arity(expr, 1, 1);
            require(args[0], Type::Str, expr.location);
            return Type::Json;
        }
        if (builtin && expr.text.starts_with("json_")) {
            arity(expr, 1, 1);
            require(args[0], Type::Json, expr.location);
            if (expr.text == "json_kind" || expr.text == "json_str")
                return Type::Str;
            if (expr.text == "json_int")
                return Type::Int;
            if (expr.text == "json_float")
                return Type::Float;
            if (expr.text == "json_bool")
                return Type::Bool;
            if (expr.text == "json_list")
                return Type::list(Type::Json);
            if (expr.text == "json_dict")
                return Type::dict(Type::Str, Type::Json);
        }
        if (expr.text == "print" || expr.text == "eprint")
            return Type::Void;
        if (expr.text == "exit") {
            arity(expr, 1, 1);
            require(args[0], Type::Int, expr.location);
            return Type::Void;
        }
        if (expr.text == "len") {
            arity(expr, 1, 1);
            if (args[0] != Type::Str && args[0].kind != Type::List && args[0].kind != Type::Array &&
                args[0].kind != Type::Dict && args[0].kind != Type::Tuple)
                fail("E3002", expr.location, "len requires a string, list, or dictionary");
            return Type::Int;
        }
        if (expr.text == "slice") {
            arity(expr, 3, 3);
            if (args[0] != Type::Str && args[0].kind != Type::List)
                fail("E3002", expr.location, "slice requires a string or list");
            require(args[1], Type::Int, expr.location);
            require(args[2], Type::Int, expr.location);
            return args[0];
        }
        if (expr.text == "str") {
            arity(expr, 1, 1);
            return Type::Str;
        }
        if (expr.text == "byte_value" || expr.text == "byte_string") {
            arity(expr, 1, 1);
            require(args[0], expr.text == "byte_value" ? Type::Str : Type::Int, expr.location);
            return expr.text == "byte_value" ? Type::Int : Type::Str;
        }
        if (expr.text == "int" || expr.text == "float") {
            arity(expr, 1, 1);
            if (!numeric(args[0]) && args[0] != Type::Str)
                fail("E3002", expr.location, "numeric conversion requires int, float, or str");
            return expr.text == "int" ? Type::Int : Type::Float;
        }
        if (expr.text == "assert") {
            arity(expr, 1, 2);
            require(args[0], Type::Bool, expr.location);
            if (args.size() == 2)
                require(args[1], Type::Str, expr.location);
            return Type::Void;
        }
        if (expr.text == "append" || expr.text == "pop") {
            if (args[0].kind != Type::List)
                fail("E3002", expr.location, "list mutation requires a list");
            lvalue(*expr.children[0]);
            if (expr.text == "append") {
                require(args[1], *args[0].element, expr.location);
                return Type::Void;
            }
            return *args[0].element;
        }
        if (expr.text == "args") {
            arity(expr, 0, 0);
            return Type::list(Type::Str);
        }
        if (expr.text == "keys" || expr.text == "values" || expr.text == "get" ||
            expr.text == "remove") {
            arity(expr,
                  expr.text == "get"      ? 3
                  : expr.text == "remove" ? 2
                                          : 1,
                  expr.text == "get"      ? 3
                  : expr.text == "remove" ? 2
                                          : 1);
            if (args[0].kind != Type::Dict)
                fail("E3002", expr.location, "dictionary operation requires a dict");
            if (expr.text == "keys")
                return Type::list(*args[0].key);
            if (expr.text == "values")
                return Type::list(*args[0].element);
            require(args[1], *args[0].key, expr.children[1]->location);
            if (expr.text == "remove") {
                lvalue(*expr.children[0]);
                return Type::Bool;
            }
            require(args[2], *args[0].element, expr.children[2]->location);
            return *args[0].element;
        }
        if (expr.text == "eof") {
            arity(expr, 0, 0);
            return Type::Bool;
        }
        if (expr.text == "read_line") {
            arity(expr, 0, 0);
            return Type::Str;
        }
        if (expr.text == "strip" || expr.text == "read_text" || expr.text == "path_exists") {
            arity(expr, 1, 1);
            require(args[0], Type::Str, expr.location);
            return expr.text == "path_exists" ? Type::Bool : Type::Str;
        }
        if (expr.text == "split" || expr.text == "find" || expr.text == "starts_with" ||
            expr.text == "ends_with" || expr.text == "write_text" || expr.text == "replace" ||
            expr.text == "join") {
            arity(expr, expr.text == "replace" ? 3 : 2, expr.text == "replace" ? 3 : 2);
            require(args[0], expr.text == "join" ? Type::list(Type::Str) : Type(Type::Str),
                    expr.location);
            for (std::size_t i = 1; i < args.size(); ++i)
                require(args[i], Type::Str, expr.location);
            if (expr.text == "split")
                return Type::list(Type::Str);
            if (expr.text == "find")
                return Type::Int;
            if (expr.text == "starts_with" || expr.text == "ends_with")
                return Type::Bool;
            return expr.text == "write_text" ? Type::Void : Type::Str;
        }
        throw std::logic_error("builtin lacks a semantic type rule");
    }
    void lvalue(const Expr& expr) {
        if (expr.kind == Expr::Kind::Name) {
            if (narrowed.contains(expr.symbol))
                fail("E3015", expr.location,
                     "'" + expr.text + "' was narrowed from an optional here, so it cannot be changed in place; assign it with " +
                         expr.text + " = ...");
            if (readonly_captures.contains(expr.symbol))
                fail("E3028", expr.location,
                     "captured values are immutable snapshots; mutate a local copy instead");
            if (expr.symbol == readonly_receiver)
                fail("E3027", expr.location,
                     "self is read-only; declare mut self to mutate the receiver");
            return;
        }
        if (expr.kind == Expr::Kind::Member && expr.children[0]->type.kind == Type::Record) {
            lvalue(*expr.children[0]);
            return;
        }
        if (expr.kind == Expr::Kind::Index && (expr.children[0]->type.kind == Type::List ||
                                               expr.children[0]->type.kind == Type::Array ||
                                               expr.children[0]->type.kind == Type::Dict)) {
            lvalue(*expr.children[0]);
            return;
        }
        fail("E3015", expr.location,
             "mutation target must be a variable or a rooted record/list/dictionary path");
    }
    Type float_expression(Expr& expr) {
        Type result = Type::Unknown;
        double value = 0;
        // Avoid requiring the recent floating-point from_chars in libc++.
        std::istringstream input(expr.text);
        input.imbue(std::locale::classic());
        input >> std::noskipws >> value;
        const auto exponent = expr.text.find_first_of("eE");
        const auto mantissa = expr.text.substr(0, exponent);
        const bool nonzero = mantissa.find_first_of("123456789") != std::string::npos;
        if (input.fail() || input.peek() != std::char_traits<char>::eof() ||
            !std::isfinite(value) || (value == 0.0 && nonzero))
            fail("E3006", expr.location, "float literal is outside the finite binary64 range");
        result = Type::Float;
        return result;
    }
    Type member_expression(Expr& expr, Type expected) {
        std::function<std::string(const Expr&)> qualified = [&](const Expr& part) -> std::string {
            if (part.kind == Expr::Kind::Name)
                return part.text;
            if (part.kind != Expr::Kind::Member)
                return {};
            const auto base = qualified(*part.children[0]);
            return base.empty() ? std::string{} : base + "." + part.text;
        };
        const auto found = functions.find(qualified(expr));
        if (found != functions.end() && found->second->kind == Stmt::Kind::Function) {
            return function_value(expr, *found->second, expected);
        }
        Type result = Type::Unknown;
        const auto base = expression(*expr.children[0]);
        if (base.kind == Type::Record) {
            const auto& fields = base.declaration->parameters;
            const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto& value) {
                return value.name == expr.text;
            });
            if (field == fields.end())
                fail("E3016", expr.location, record_member_problem(base, expr.text));
            result = field->type;
            expr.symbol = field->symbol;
            return result;
        }
        if (base.kind != Type::Error)
            fail("E3016", expr.location, type_name(base) + " has no field or method '" + expr.text + "'" + builtin_call_hint(expr.text));
        if (expr.text == "code" || expr.text == "message" || expr.text == "file")
            result = Type::Str;
        else if (expr.text == "line" || expr.text == "column")
            result = Type::Int;
        else
            fail("E3016", expr.location, "unknown error field '" + expr.text + "'");
        return result;
    }
    Type list_expression(Expr& expr, const Type& expected) {
        Type result = Type::Unknown;
        Type element = expected.kind == Type::List ? *expected.element : Type(Type::Unknown);
        if (expr.children.empty() && element == Type::Unknown)
            fail("E3014", expr.location,
                 "empty list requires an element type; use an annotation such as list[int]");
        for (auto& child : expr.children) {
            const auto value = expression(*child, element);
            if (value == Type::Void)
                fail("E3002", child->location, "list elements cannot be void");
            if (element == Type::Unknown)
                element = value;
            else if (expected.kind != Type::List && element == Type::Int && value == Type::Float)
                element = Type::Float;
            else
                require(value, element, child->location);
        }
        result = Type::list(element);
        return result;
    }
    // name = unwrap(name) from the parser: declares a narrowed local when name is optional, else nothing.
    unsigned narrow(Stmt& stmt) {
        const auto* binding = lookup(stmt.name);
        const bool optional = binding && binding->type.kind == Type::Optional;
        if (!optional || (stmt.relative > 0 && !if_exits)) {
            stmt.kind = Stmt::Kind::Pass;
            return Next;
        }
        const Type value = expression(*stmt.expressions[0]);
        stmt.symbol = next_symbol++;
        stmt.declares = true;
        stmt.annotation = value;
        narrowed.emplace(stmt.symbol, Narrowing{*binding, scopes.back().contains(stmt.name)});
        scopes.back().insert_or_assign(stmt.name, Binding{value, stmt.symbol});
        return Next;
    }
    // Assigning a narrowed name writes the original variable, and the narrowing ends.
    void end_narrowing(const std::string& name) {
        for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
            auto found = scope->find(name);
            if (found == scope->end())
                continue;
            const auto narrowing = narrowed.find(found->second.symbol);
            if (narrowing != narrowed.end()) {
                if (narrowing->second.replaced)
                    found->second = narrowing->second.original;
                else
                    scope->erase(found);
                end_narrowing(name);
            }
            return;
        }
    }
    // A loop that assigns a narrowed name may run its body again after the assignment.
    void end_loop_narrowing(const Block& body) {
        for (const auto& item : body) {
            if (item->kind == Stmt::Kind::Assign && !item->expression_body)
                end_narrowing(item->name);
            if (item->kind != Stmt::Kind::Function && item->kind != Stmt::Kind::Record) {
                end_loop_narrowing(item->body);
                end_loop_narrowing(item->otherwise);
            }
        }
    }
    // The parser marks an unpacking's hidden variable with the number of names (relative > 0).
    void unpackable(const Type& value, std::size_t count, const Location& at) const {
        if (count == 0)
            return;
        if (value.kind != Type::Tuple)
            fail("E3025", at, "cannot unpack " + type_name(value) + "; unpacking needs a tuple");
        if (value.parameters.size() != count)
            fail("E3025", at,
                 "cannot unpack a tuple of " + std::to_string(value.parameters.size()) + " values into " +
                     std::to_string(count) + " names");
    }
    Type index_expression(Expr& expr) {
        Type result = Type::Unknown;
        auto base = expression(*expr.children[0]);
        if (base.kind == Type::Tuple) {
            auto& index = *expr.children[1];
            require(expression(index), Type::Int, index.location);
            const auto* literal = &index;
            bool negative = false;
            if (index.kind == Expr::Kind::Unary && (index.text == "-" || index.text == "+")) {
                negative = index.text == "-";
                literal = index.children[0].get();
            }
            if (literal->kind != Expr::Kind::Integer)
                fail("E3025", index.location,
                     "heterogeneous tuple index must be an integer literal");
            std::uint64_t magnitude = 0;
            const auto parsed = std::from_chars(
                literal->text.data(), literal->text.data() + literal->text.size(), magnitude);
            const auto size = base.parameters.size();
            if (parsed.ec != std::errc{} ||
                (negative ? magnitude > size || magnitude == 0 && size == 0 : magnitude >= size))
                fail("E3025", index.location, "tuple index is out of bounds");
            const auto position = negative && magnitude ? size - static_cast<std::size_t>(magnitude)
                                                        : static_cast<std::size_t>(magnitude);
            if (position >= size)
                fail("E3025", index.location, "tuple index is out of bounds");
            expr.symbol = position;
            return base.parameters[position];
        }
        if (base.kind == Type::Array) {
            const auto index = expression(*expr.children[1]);
            if (index != Type::Int &&
                !(index.kind == Type::Tuple && !index.parameters.empty() &&
                  index.parameters.size() <= 8 &&
                  std::all_of(index.parameters.begin(), index.parameters.end(),
                              [](const auto& t) { return t == Type::Int; })))
                fail("E3031", expr.location, "array indexes require integers, one for each axis");
            return *base.element;
        }
        require(expression(*expr.children[1]),
                base.kind == Type::Dict ? *base.key : Type(Type::Int), expr.children[1]->location);
        if (base.kind == Type::List || base.kind == Type::Dict)
            result = *base.element;
        else if (base == Type::Str)
            result = Type::Str;
        else
            fail("E3002", expr.location, "indexing requires a list or string");
        return result;
    }
    Type dict_expression(Expr& expr, const Type& expected) {
        Type result = Type::Unknown;
        Type key = expected.kind == Type::Dict ? *expected.key : Type(Type::Unknown);
        Type value = expected.kind == Type::Dict ? *expected.element : Type(Type::Unknown);
        if (expr.children.empty() && key == Type::Unknown)
            fail("E3014", expr.location, "empty dictionary requires contextual dict[K, V] types");
        for (std::size_t i = 0; i < expr.children.size(); i += 2) {
            auto k = expression(*expr.children[i], key);
            auto v = expression(*expr.children[i + 1], value);
            if (key == Type::Unknown)
                key = k;
            else
                require(k, key, expr.children[i]->location);
            if (value == Type::Unknown)
                value = v;
            else if (expected.kind != Type::Dict && value == Type::Int && v == Type::Float)
                value = Type::Float;
            else
                require(v, value, expr.children[i + 1]->location);
            if (v == Type::Void)
                fail("E3002", expr.children[i + 1]->location, "dictionary values cannot be void");
        }
        if (key != Type::Int && key != Type::Str && key != Type::Bool)
            fail("E3020", expr.location, "dictionary keys must be int, str, or bool");
        result = Type::dict(key, value);
        return result;
    }
    Type unary_expression(Expr& expr) {
        Type result = Type::Unknown;
        auto& operand = *expr.children[0];
        // The magnitude of INT64_MIN is only legal as the operand of unary minus.
        if (expr.text == "-" && operand.kind == Expr::Kind::Integer) {
            std::uint64_t magnitude = 0;
            auto parsed = std::from_chars(operand.text.data(),
                                          operand.text.data() + operand.text.size(), magnitude);
            if (parsed.ec == std::errc{} && magnitude == (std::uint64_t{1} << 63)) {
                expr.symbol = 1;
                operand.type = Type::Int;
                result = Type::Int;
                return result;
            }
        }
        const Type type = expression(operand);
        if (expr.text == "~") {
            require(type, Type::Int, expr.location);
            result = Type::Int;
        } else if (expr.text == "not") {
            require(type, Type::Bool, expr.location);
            result = Type::Bool;
        } else {
            if (!numeric(type) && type.kind != Type::Array)
                fail("E3002", expr.location, "unary '" + expr.text + "' requires a number");
            result = type;
        }
        return result;
    }
    static bool empty_literal(const Expr& expr) {
        return (expr.kind == Expr::Kind::List || expr.kind == Expr::Kind::Dict) &&
               expr.children.empty();
    }
    std::pair<Type, Type> operands(Expr& left, Expr& right, const std::string& op,
                                   Type expected = Type::Unknown) {
        Type a, b;
        if (op == "in" || op == "not in") {
            b = expression(right);
            a = expression(left, b.kind == Type::Dict   ? *b.key
                                 : b.kind == Type::List ? *b.element
                                                        : Type(Type::Unknown));
        } else if (empty_literal(left) && expected == Type::Unknown && op != "*") {
            b = expression(right);
            a = expression(left, b);
        } else {
            const auto hint = expected.kind == Type::List || expected.kind == Type::Dict ||
                                      expected.kind == Type::Tuple
                                  ? expected
                                  : Type(Type::Unknown);
            a = expression(left, hint);
            b = expression(
                right,
                a.kind == Type::List || a.kind == Type::Dict || a.kind == Type::Tuple ? a : hint);
        }
        return {a, b};
    }
    Type operator_type(const std::string& op, Type a, Type b, Location at) {
        if ((a.kind == Type::Array || b.kind == Type::Array) &&
            (op == "+" || op == "-" || op == "*" || op == "/")) {
            const auto left = a.kind == Type::Array ? *a.element : a;
            const auto right = b.kind == Type::Array ? *b.element : b;
            if (!numeric(left) || !numeric(right))
                fail("E3031", at, "array arithmetic requires numeric arrays or scalars");
            return Type::wrapped(
                Type::Array,
                op == "/" || left == Type::Float || right == Type::Float ? Type::Float : Type::Int);
        }
        if ((op == "==" || op == "!=") && ((a == Type::None && b.kind == Type::Optional) ||
                                           (b == Type::None && a.kind == Type::Optional)))
            return Type::Bool; // Presence checks do not compare the payload.
        if ((op == "==" || op == "!=") && (!equality_compatible(a) || !equality_compatible(b)))
            fail("E3028", at, "closure, callable and cursor values do not support equality");

        Type result = Type::Unknown;
        if (op == "is" || op == "is not") {
            if (!((a == Type::None && (b == Type::None || b.kind == Type::Optional)) ||
                  (b == Type::None && a.kind == Type::Optional)))
                fail("E3002", at, "identity tests support only optional values and None");
            return Type::Bool;
        }
        if ((op == "==" || op == "!=") && (a.kind == Type::Optional || b.kind == Type::Optional ||
                                           a == Type::None || b == Type::None)) {
            if (!assignable(a, b) && !assignable(b, a))
                fail("E3002", at, "optional equality requires compatible value types");
            return Type::Bool;
        }
        if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>") {
            require(a, Type::Int, at);
            require(b, Type::Int, at);
            result = Type::Int;
        } else if (op == "in" || op == "not in") {
            if (b.kind == Type::Dict)
                require(a, *b.key, at);
            else if (b.kind == Type::List) {
                if (!equality_compatible(*b.element))
                    fail("E3028", at, "membership requires comparable list items");
                require(a, *b.element, at);
            } else if (b == Type::Str)
                require(a, Type::Str, at);
            else
                fail("E3002", at, "membership requires a dictionary, list, or string on the right");
            result = Type::Bool;
        } else if (op == "and" || op == "or") {
            require(a, Type::Bool, at);
            require(b, Type::Bool, at);
            result = Type::Bool;
        } else if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (a == Type::Void || b == Type::Void || !((numeric(a) && numeric(b)) || a == b))
                fail("E3002", at, "comparison requires compatible value types");
            if ((a == Type::Bool || a == Type::Error || a == Type::Json || a.kind == Type::List ||
                 a.kind == Type::Array || a.kind == Type::Dict || a.kind == Type::Record ||
                 a.kind == Type::Function || a.kind == Type::Tuple || a == Type::None ||
                 a.kind == Type::Optional || a.kind == Type::Stream || a.kind == Type::Database ||
                 a.kind == Type::Statement || a.kind == Type::Transaction ||
                 a.kind == Type::TlsContext || a.kind == Type::Task) &&
                op != "==" && op != "!=")
                fail("E3002", at, "this value type supports only == and != comparisons");
            result = Type::Bool;
        } else if (op == "+" && a == Type::Str && b == Type::Str)
            result = Type::Str;
        else if (op == "+" && a.kind == Type::List && a == b)
            result = a;
        else if (op == "*" && (a == Type::Str || a.kind == Type::List) && b == Type::Int)
            result = a;
        else if (op == "*" && a == Type::Int && (b == Type::Str || b.kind == Type::List))
            result = b;
        else {
            if (!numeric(a) || !numeric(b))
                fail("E3002", at, "operator '" + op + "' cannot combine " + type_name(a) + " and " + type_name(b) +
                     (op == "+" && (a == Type::Str || b == Type::Str) ? "; convert with str(value) first" : ""));
            if (op == "//" || op == "%") {
                require(a, Type::Int, at);
                require(b, Type::Int, at);
                result = Type::Int;
            } else
                result =
                    op == "/" || a == Type::Float || b == Type::Float ? Type::Float : Type::Int;
        }
        return result;
    }
    Type binary_expression(Expr& expr, Type expected) {
        const auto [a, b] = operands(*expr.children[0], *expr.children[1], expr.text, expected);
        return operator_type(expr.text, a, b, expr.location);
    }
    Type comparison_chain(Expr& expr) {
        auto [a, b] = operands(*expr.children[0], *expr.children[1], expr.operators[0]);
        operator_type(expr.operators[0], a, b, expr.location);
        for (std::size_t i = 1; i < expr.operators.size(); ++i) {
            a = b;
            b = expression(*expr.children[i + 1],
                           a.kind == Type::List || a.kind == Type::Dict || a.kind == Type::Tuple
                               ? a
                               : Type(Type::Unknown));
            operator_type(expr.operators[i], a, b, expr.children[i]->location);
        }
        return Type::Bool;
    }
    Type conditional_expression(Expr& expr, Type expected) {
        require(expression(*expr.children[1]), Type::Bool, expr.children[1]->location);
        const auto [a, b] = operands(*expr.children[0], *expr.children[2], "conditional", expected);
        if (a == Type::Void || b == Type::Void)
            fail("E3002", expr.location, "conditional branches must produce values");
        if (numeric(a) && numeric(b))
            return a == Type::Float || b == Type::Float ? Type::Float : Type::Int;
        if (a == Type::None && b != Type::None)
            return Type::optional(b);
        if (b == Type::None && a != Type::None)
            return Type::optional(a);
        if (a.kind == Type::Optional && assignable(a, b))
            return a;
        if (b.kind == Type::Optional && assignable(b, a))
            return b;
        if (a != b)
            fail("E3002", expr.location, "conditional branches require compatible value types");
        return a;
    }
    Type iterable(Expr& value) {
        if (value.kind == Expr::Kind::Call && value.text == "range") {
            if (std::any_of(value.keywords.begin(), value.keywords.end(),
                            [](const auto& keyword) { return !keyword.empty(); }))
                fail("E3003", value.location, "range uses positional arguments");
            arity(value, 1, 3);
            for (auto& arg : value.children)
                require(expression(*arg), Type::Int, arg->location);
            return Type::Int;
        }
        const auto type = expression(value);
        if (type.kind == Type::List || type.kind == Type::Array)
            return *type.element;
        if (type.kind == Type::Dict)
            return *type.key;
        if (type == Type::Str)
            return Type::Str;
        if (type.kind == Type::Record) {
            if (auto* method = protocol_method(type, "iter", value.location)) {
                auto receiver = std::make_unique<Expr>(std::move(value));
                value = Expr(Expr::Kind::Call, receiver->location, "iter");
                value.children.push_back(std::move(receiver));
                value.keywords = {""};
                value.type = declared_call(value, *method, Type::Unknown);
            }
        }
        if (value.type.kind == Type::Record || value.type.kind == Type::Cursor)
            return iterator_element(value.type, value.location, value.iteration_next);
        if (type == Type::Range)
            return Type::Int;
        fail("E3010", value.location,
             "cannot iterate over " + type_name(type) + "; iteration requires a collection, range or iterator protocol" +
             (type == Type::Int ? "; use range(value) to count" : ""));
    }
    Type comprehension_expression(Expr& expr, Type expected) {
        const bool dictionary = expr.kind == Expr::Kind::DictComprehension;
        const std::size_t head = dictionary ? 2 : 1;
        std::size_t bindings = 0;
        for (std::size_t i = 0; i < expr.clauses.size(); ++i) {
            auto& clause = expr.clauses[i];
            auto& value = *expr.children[head + i];
            if (clause.name.empty()) {
                require(expression(value), Type::Bool, value.location);
                continue;
            }
            clause.type = iterable(value);
            valid_variable(clause.name, clause.location);
            scopes.emplace_back();
            ++bindings;
            clause.symbol = next_symbol++;
            scopes.back().emplace(clause.name, Binding{clause.type, clause.symbol});
        }
        const Type value_hint = expected.kind == (dictionary ? Type::Dict : Type::List)
                                    ? *expected.element
                                    : Type(Type::Unknown);
        Type key = Type::Unknown;
        if (dictionary) {
            key = expression(*expr.children[0],
                             expected.kind == Type::Dict ? *expected.key : Type(Type::Unknown));
            if (key != Type::Int && key != Type::Str && key != Type::Bool)
                fail("E3020", expr.location, "dictionary keys must be int, str, or bool");
        }
        const auto value = expression(*expr.children[head - 1], value_hint);
        if (value == Type::Void)
            fail("E3002", expr.location, "comprehension results cannot be void");
        for (std::size_t i = 0; i < bindings; ++i)
            scopes.pop_back();
        if (value_hint != Type::Unknown)
            require(value, value_hint, expr.location);
        const auto element = value_hint == Type::Unknown ? value : value_hint;
        return dictionary ? Type::dict(key, element) : Type::list(element);
    }
    Type closure_expression(Expr& expr, Type expected) {
        if (++context.closure_depth > 64)
            fail("E2002", expr.location, "closure nesting exceeds 64 levels");
        auto& function = *expr.function;
        function.symbol = next_symbol++;
        function.mutating = !function.mutable_captures.empty();
        std::unordered_set<std::string> mutable_names;
        for (const auto& name : function.mutable_captures) {
            if (!mutable_names.insert(name).second || !lookup(name))
                fail("E3028", expr.location,
                     "duplicate or undefined mutable capture '" + name + "'");
            if (narrowed.contains(lookup(name)->symbol))
                fail("E3028", expr.location,
                     "'" + name + "' was narrowed from an optional here, so it cannot be captured with mut");
            for (const auto& parameter : function.parameters)
                if (parameter.name == name)
                    fail("E3028", parameter.location,
                         "parameter shadows explicit capture '" + name + "'");
        }
        Analyzer inner(declarations, namespaces, next_symbol, context);
        inner.enclosing = this;
        inner.closure_function = &function;
        inner.local_types = local_types;
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            auto& parameter = function.parameters[i];
            if (parameter.default_value) {
                if (!constant_default(*parameter.default_value))
                    fail("E3026", parameter.location, "default arguments require literal values");
                std::function<void(Expr&)> normalize = [&](Expr& value) {
                    if (value.kind == Expr::Kind::Name) {
                        if (lookup(value.text) || functions.contains(value.text))
                            fail("E3026", value.location, "default literal name is shadowed");
                        value.kind = value.text == "None" ? Expr::Kind::None : Expr::Kind::Bool;
                        if (value.text != "None")
                            value.text = value.text == "True" ? "true" : "false";
                    }
                    for (auto& child : value.children)
                        normalize(*child);
                };
                normalize(*parameter.default_value);
            }
            if (parameter.type == Type::Unknown) {
                if (!callable_type(expected) ||
                    expected.parameters.size() != function.parameters.size())
                    fail("E3028", parameter.location,
                         "closure parameter needs a type or concrete callable context");
                parameter.type = expected.parameters[i];
            }
        }
        if (function.return_type == Type::Unknown && function.expression_body) {
            inner.scopes.emplace_back();
            inner.in_function = true;
            for (auto& parameter : function.parameters) {
                inner.valid_variable(parameter.name, parameter.location);
                parameter.type = inner.resolved(parameter.type, parameter.location);
                parameter.symbol = next_symbol++;
                if (!inner.scopes.back()
                         .emplace(parameter.name, Binding{parameter.type, parameter.symbol})
                         .second)
                    fail("E3001", parameter.location, "duplicate closure parameter");
            }
            auto& result = *function.body[0];
            function.return_type =
                inner.expression(*result.expressions[0],
                                 callable_type(expected) ? *expected.element : Type(Type::Unknown));
            if (callable_type(expected) && *expected.element != Type::Unknown) {
                inner.require(function.return_type, *expected.element, expr.location);
                function.return_type = *expected.element;
            }
            result.annotation = function.return_type;
            function.checked = true;
        } else {
            inner.function_body(function);
        }
        for (const auto& name : function.mutable_captures)
            inner.lookup(name);
        if (function.captures.size() > 255)
            fail("E2002", expr.location, "closures support at most 255 captures");
        std::vector<std::pair<Type, std::size_t>> pending;
        for (const auto& capture : function.captures)
            pending.emplace_back(capture.type, 1);
        std::unordered_set<const Stmt*> seen;
        while (!pending.empty()) {
            auto [type, depth] = pending.back();
            pending.pop_back();
            if (depth > 256)
                fail("E2002", expr.location, "closure capture containment exceeds 256 levels");
            function.capture_depth = std::max(function.capture_depth, depth);
            if (type.kind == Type::Closure) {
                function.capture_depth =
                    std::max(function.capture_depth, depth + type.declaration->capture_depth);
                if (function.capture_depth > 256)
                    fail("E2002", expr.location, "closure capture containment exceeds 256 levels");
            } else if (type.kind == Type::Record && seen.insert(type.declaration).second) {
                for (const auto& field : type.declaration->parameters)
                    pending.emplace_back(field.type, depth + 1);
            } else if (type.kind == Type::Tuple) {
                for (const auto& item : type.parameters)
                    pending.emplace_back(item, depth + 1);
            } else if (type.kind == Type::Optional || type.kind == Type::Step)
                pending.emplace_back(*type.element, depth + 1);
        }
        --context.closure_depth;
        return Type::closure(function);
    }
    Type expression(Expr& expr, Type expected = Type::Unknown) {
        if (expr.type != Type::Unknown)
            return expr.type;
        if (++expression_depth > 256)
            fail("E2002", expr.location, "expression nesting exceeds 256 levels");
        Type result = Type::Unknown;
        // Context unwraps only the expected type, never the actual optional value.
        if (expected.kind == Type::Optional)
            expected = *expected.element;
        switch (expr.kind) {
        case Expr::Kind::Integer: {
            std::int64_t value = 0;
            auto parsed =
                std::from_chars(expr.text.data(), expr.text.data() + expr.text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr != expr.text.data() + expr.text.size())
                fail("E3006", expr.location, "integer literal is outside the signed 64-bit range");
            // Canonical decimal form avoids C++'s leading-zero octal syntax.
            expr.text = std::to_string(value);
            result = Type::Int;
            break;
        }
        case Expr::Kind::Float:
            result = float_expression(expr);
            break;
        case Expr::Kind::String:
            result = Type::Str;
            break;
        case Expr::Kind::Bool:
            result = Type::Bool;
            break;
        case Expr::Kind::None:
            result = Type::None;
            break;
        case Expr::Kind::Tuple: {
            std::vector<Type> elements;
            if (expected.kind == Type::Tuple && expected.parameters.size() != expr.children.size())
                fail("E3002", expr.location, "tuple annotation has a different arity");
            for (std::size_t i = 0; i < expr.children.size(); ++i) {
                const auto hint =
                    expected.kind == Type::Tuple ? expected.parameters[i] : Type(Type::Unknown);
                const auto type = expression(*expr.children[i], hint);
                if (type == Type::Void)
                    fail("E3002", expr.children[i]->location, "tuple elements cannot be void");
                if (hint != Type::Unknown)
                    require(type, hint, expr.children[i]->location);
                elements.push_back(hint == Type::Unknown ? type : hint);
            }
            result = Type::tuple(std::move(elements));
            break;
        }
        case Expr::Kind::Name: {
            auto binding = lookup(expr.text);
            const auto found = functions.find(expr.text);
            if (!binding && found != functions.end() &&
                found->second->kind == Stmt::Kind::Function) {
                result = function_value(expr, *found->second, expected);
                break;
            }
            if (!binding && found == functions.end() && expr.text == "None") {
                expr.kind = Expr::Kind::None;
                result = Type::None;
                break;
            }
            if (!binding && found == functions.end() &&
                (expr.text == "True" || expr.text == "False")) {
                expr.kind = Expr::Kind::Bool;
                expr.text = expr.text == "True" ? "true" : "false";
                result = Type::Bool;
                break;
            }
            if (!binding)
                fail("E3007", expr.location, undefined_variable(expr.text));
            result = binding->type;
            expr.symbol = binding->symbol;
            break;
        }
        case Expr::Kind::Call:
            result = call(expr, expected);
            break;
        case Expr::Kind::Invoke:
            result = invoke(expr, expected);
            break;
        case Expr::Kind::Closure:
            result = closure_expression(expr, expected);
            break;
        case Expr::Kind::TypeApply:
            fail("E3024", expr.location, "type applications require a record constructor call");
        case Expr::Kind::Member:
            result = member_expression(expr, expected);
            break;
        case Expr::Kind::List:
            result = list_expression(expr, expected);
            break;
        case Expr::Kind::Index:
            result = index_expression(expr);
            break;
        case Expr::Kind::Slice:
            result = expression(*expr.children[0]);
            if (result != Type::Str && result.kind != Type::List && result.kind != Type::Array)
                fail("E3002", expr.location, "slicing requires a list or string");
            for (std::size_t i = 1; i < expr.children.size(); ++i)
                if (expr.children[i])
                    require(expression(*expr.children[i]), Type::Int, expr.children[i]->location);
            break;
        case Expr::Kind::ListComprehension:
        case Expr::Kind::DictComprehension:
            result = comprehension_expression(expr, expected);
            break;
        case Expr::Kind::CompareChain:
            result = comparison_chain(expr);
            break;
        case Expr::Kind::Conditional:
            result = conditional_expression(expr, expected);
            break;
        case Expr::Kind::Dict:
            result = dict_expression(expr, expected);
            break;
        case Expr::Kind::Unary:
            result = unary_expression(expr);
            break;
        case Expr::Kind::Binary:
            result = binary_expression(expr, expected);
            break;
        }
        validate_type_depth(result, expr.location);
        expr.type = result;
        --expression_depth;
        return result;
    }
    unsigned block(Block& statements, bool nested = true) {
        if (nested)
            scopes.emplace_back();
        unsigned flow = Next;
        for (auto& stmt : statements) {
            if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record ||
                stmt->kind == Stmt::Kind::Import)
                continue;
            if ((flow & Next) == 0)
                fail("E3008", stmt->location, "unreachable statement");
            flow = (flow & ~Next) | statement(*stmt);
        }
        if (nested)
            scopes.pop_back();
        return flow;
    }
    unsigned statement(Stmt& stmt) {
        switch (stmt.kind) {
        case Stmt::Kind::With: {
            const auto resource = expression(*stmt.expressions[0]);
            if (resource != Type::Stream && resource != Type::Database &&
                resource != Type::Statement && resource != Type::Transaction &&
                resource != Type::TlsContext && resource != Type::Task)
                fail("E3002", stmt.location, "with requires a native resource");
            valid_variable(stmt.name, stmt.location);
            scopes.emplace_back();
            stmt.symbol = next_symbol++;
            scopes.back().emplace(stmt.name, Binding{resource, stmt.symbol});
            const auto flow = block(stmt.body, false);
            scopes.pop_back();
            return flow;
        }
        case Stmt::Kind::Try: {
            const auto body = block(stmt.body);
            valid_variable(stmt.name, stmt.location);
            scopes.emplace_back();
            stmt.symbol = next_symbol++;
            scopes.back().emplace(stmt.name, Binding{Type::Error, stmt.symbol});
            handlers.push_back(stmt.symbol);
            const auto handler = block(stmt.otherwise, false);
            handlers.pop_back();
            scopes.pop_back();
            return (body & ~Raised) | handler;
        }
        case Stmt::Kind::Finally: {
            const auto body = block(stmt.body);
            const auto outer = std::exchange(finally_loops, loop_depth);
            const auto cleanup = block(stmt.otherwise);
            finally_loops = outer;
            // A finally block that always raises ends every path; otherwise the paths are the body's.
            return (cleanup & Next) ? body | (cleanup & ~Next) : cleanup;
        }
        case Stmt::Kind::Raise: {
            if (stmt.expressions.empty() || (stmt.relative && handlers.empty()))
                fail("E3017", stmt.location,
                     "raise without a value re-raises the error being handled, so it belongs in an "
                     "except block");
            if (stmt.relative) {
                auto& name = *stmt.expressions[0];
                name.symbol = handlers.back();
                name.type = Type::Error;
                return Raised;
            }
            const auto value = expression(*stmt.expressions[0]);
            if (value != Type::Str && value != Type::Error)
                fail("E3017", stmt.location, "raise requires a string or error value");
            return Raised;
        }
        case Stmt::Kind::Import:
            return Next;
        case Stmt::Kind::Assign: {
            if (stmt.expression_body)
                return narrow(stmt);
            end_narrowing(stmt.name);
            valid_variable(stmt.name, stmt.location);
            stmt.annotation = resolved(stmt.annotation, stmt.location);
            validate_type_depth(stmt.annotation, stmt.location);
            // Comprehensions push scopes while checking the RHS. On libraries
            // whose unordered_map move can throw, vector growth copies maps
            // and invalidates pointers into the old scopes. Retain a value.
            const auto* found = lookup(stmt.name);
            const auto binding = found ? std::optional<Binding>(*found) : std::nullopt;
            const Type value =
                expression(*stmt.expressions[0], binding ? binding->type : stmt.annotation);
            if (value == Type::Void)
                fail("E3002", stmt.location, "cannot assign a void expression");
            unpackable(value, stmt.relative, stmt.location);
            if (binding) {
                if (readonly_captures.contains(binding->symbol))
                    fail("E3028", stmt.location,
                         "capture is an immutable snapshot; use an explicit mut capture");
                if (binding->symbol == readonly_receiver)
                    fail("E3027", stmt.location,
                         "self is read-only; declare mut self to mutate the receiver");
                if (stmt.annotation != Type::Unknown)
                    fail("E3009", stmt.location,
                         "type annotations are only allowed on a variable's first assignment");
                require(value, binding->type, stmt.expressions[0]->location);
                stmt.symbol = binding->symbol;
                stmt.annotation = binding->type;
            } else {
                const Type type = stmt.annotation == Type::Unknown ? value : stmt.annotation;
                require(value, type, stmt.expressions[0]->location);
                stmt.symbol = next_symbol++;
                stmt.declares = true;
                stmt.annotation = type;
                scopes.back().emplace(stmt.name, Binding{type, stmt.symbol});
            }
            return Next;
        }
        case Stmt::Kind::IndexAssign: {
            const auto target = expression(*stmt.expressions[0]);
            lvalue(*stmt.expressions[0]);
            require(expression(*stmt.expressions[1], target), target,
                    stmt.expressions[1]->location);
            return Next;
        }
        case Stmt::Kind::AugAssign: {
            const auto target = expression(*stmt.expressions[0]);
            lvalue(*stmt.expressions[0]);
            const auto value = expression(*stmt.expressions[1], target);
            stmt.annotation = operator_type(stmt.name, target, value, stmt.location);
            require(stmt.annotation, target, stmt.location);
            return Next;
        }
        case Stmt::Kind::Expression:
            expression(*stmt.expressions[0]);
            if (stmt.expressions[0]->kind == Expr::Kind::Call &&
                stmt.expressions[0]->text == "exit")
                return Raised;
            return Next;
        case Stmt::Kind::If: {
            require(expression(*stmt.expressions[0]), Type::Bool, stmt.expressions[0]->location);
            const auto body = block(stmt.body);
            const auto result = body | (stmt.otherwise.empty() ? Next : block(stmt.otherwise));
            if_exits = stmt.otherwise.empty() && !(body & Next);
            return result;
        }
        case Stmt::Kind::While: {
            end_loop_narrowing(stmt.body);
            require(expression(*stmt.expressions[0]), Type::Bool, stmt.expressions[0]->location);
            ++loop_depth;
            const auto flow = block(stmt.body);
            --loop_depth;
            return Next | (flow & Returned);
        }
        case Stmt::Kind::For: {
            end_loop_narrowing(stmt.body);
            valid_variable(stmt.name, stmt.location);
            auto& range = *stmt.expressions[0];
            const Type element = iterable(range);
            unpackable(element, stmt.relative, stmt.location);
            scopes.emplace_back();
            stmt.symbol = next_symbol++;
            stmt.annotation = element;
            scopes.back().emplace(stmt.name, Binding{element, stmt.symbol});
            ++loop_depth;
            const auto flow = block(stmt.body, false);
            --loop_depth;
            scopes.pop_back();
            return Next | (flow & Returned);
        }
        case Stmt::Kind::Return: {
            if (!in_function)
                fail("E3011", stmt.location, "return is only allowed inside a function");
            if (finally_loops)
                fail("E3011", stmt.location, "return cannot leave a finally block");
            const auto value =
                stmt.expressions.empty()
                    ? Type(Type::Void)
                    : expression(*stmt.expressions[0],
                                 inferring_return ? Type(Type::Unknown) : return_type);
            if (inferring_return) {
                stmt.annotation = value;
                return_sites.push_back(&stmt);
            } else {
                require(value, return_type,
                        stmt.expressions.empty() ? stmt.location : stmt.expressions[0]->location);
                stmt.annotation = return_type;
            }
            return Returned;
        }
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            if (loop_depth == 0)
                fail("E3012", stmt.location, "break and continue require an enclosing loop");
            if (finally_loops == loop_depth)
                fail("E3012", stmt.location, "break and continue cannot leave a finally block");
            return stmt.kind == Stmt::Kind::Break ? Broken : Continued;
        case Stmt::Kind::Pass:
            return Next;
        case Stmt::Kind::Function:
        case Stmt::Kind::Record:
            return Next;
        }
        return Next;
    }

  public:
    Analyzer(const FunctionBindings& initial, const std::vector<std::string>& names,
             std::uint64_t& counter, AnalysisContext& shared)
        : context(shared), declarations(initial), functions(initial), next_symbol(counter),
          namespaces(names) {}
    void function_body(Stmt& stmt) {
        if (stmt.checked)
            return;
        stmt.checked = true;
        readonly_receiver = 0;
        scopes.clear();
        scopes.emplace_back();
        in_function = true;
        inferring_return = stmt.return_type == Type::Unknown;
        return_sites.clear();
        if (inferring_return) {
            if (context.inferring_returns.size() >= 64)
                fail("E2002", stmt.location, "return inference exceeds 64 active dependencies");
            context.inferring_returns.insert(&stmt);
        }
        return_type = resolved(stmt.return_type, stmt.location);
        stmt.return_type = return_type;
        validate_type_depth(return_type, stmt.location);
        for (auto& parameter : stmt.parameters) {
            parameter.type = resolved(parameter.type, parameter.location);
            if (parameter.type == Type::Void)
                fail("E3002", parameter.location, "parameters cannot have type void");
            validate_type_depth(parameter.type, parameter.location);
            if (parameter.default_value)
                require(expression(*parameter.default_value, parameter.type), parameter.type,
                        parameter.default_value->location);
        }
        for (auto& parameter : stmt.parameters) {
            valid_variable(parameter.name, parameter.location);
            if (scopes.back().contains(parameter.name))
                fail("E3001", parameter.location, "duplicate parameter '" + parameter.name + "'");
            parameter.symbol = next_symbol++;
            scopes.back().emplace(parameter.name, Binding{parameter.type, parameter.symbol});
        }
        if (stmt.method && !stmt.mutating)
            readonly_receiver = stmt.parameters[0].symbol;
        if (stmt.external)
            return;
        const auto flow = block(stmt.body, false);
        if (inferring_return) {
            Type result = Type::Unknown;
            bool optional = false;
            for (const auto* site : return_sites) {
                auto value = site->annotation;
                if (value == Type::None) {
                    optional = true;
                    continue;
                }
                if (value.kind == Type::Optional) {
                    optional = true;
                    value = *value.element;
                }
                if (result == Type::Unknown)
                    result = value;
                else if (numeric(result) && numeric(value))
                    result =
                        result == Type::Float || value == Type::Float ? Type::Float : Type::Int;
                else if (result != value)
                    fail("E3030", site->location,
                         "incompatible inferred returns " + type_name(result) + " and " +
                             type_name(value) + "; add an explicit return annotation");
            }
            if (result == Type::Unknown)
                result = optional ? Type::None : Type::Void;
            else if (optional) {
                if (result == Type::Void)
                    fail("E3030", stmt.location, "cannot mix bare and value returns");
                result = Type::optional(result);
            }
            for (auto* site : return_sites) {
                if (!assignable(result, site->annotation))
                    fail("E3030", site->location,
                         "return " + type_name(site->annotation) + " cannot be inferred as " +
                             type_name(result) + "; add an explicit return annotation");
                site->annotation = result;
            }
            return_type = stmt.return_type = result;
            validate_type_depth(result, stmt.location);
            context.inferring_returns.erase(&stmt);
        }
        if (return_type != Type::Void && (flow & Next))
            fail("E3013", stmt.location,
                 "function '" + stmt.name + "' does not return a value on every path");
        // A final top-level return has no local handler that could observe a
        // consumed local. Captures are excluded: they outlive this invocation.
        if (!stmt.body.empty() && stmt.body.back()->kind == Stmt::Kind::Return &&
            !stmt.body.back()->expressions.empty()) {
            auto& result = *stmt.body.back()->expressions[0];
            const auto consumable = [](const Type& type) {
                if (type.kind == Type::Array || type.kind == Type::Str)
                    return true;
                if (type.kind != Type::Step)
                    return false;
                // Dropping the last resource owner can be observable by another
                // return operand. Do not move handles or aggregates that might
                // contain them merely because their step is read once.
                const auto payload = type.element->kind;
                return payload == Type::Int || payload == Type::Float || payload == Type::Bool ||
                       payload == Type::None || payload == Type::Str || payload == Type::Array;
            };
            std::unordered_set<std::uint64_t> owned;
            for (const auto& parameter : stmt.parameters)
                if (!parameter.mutable_borrow && consumable(parameter.type))
                    owned.insert(parameter.symbol);
            const auto locals = [&](const auto& visit, const Block& body) -> void {
                for (const auto& item : body) {
                    if (item->kind == Stmt::Kind::Function)
                        continue;
                    if (item->kind == Stmt::Kind::Assign && item->declares &&
                        consumable(item->annotation))
                        owned.insert(item->symbol);
                    visit(visit, item->body);
                    visit(visit, item->otherwise);
                }
            };
            locals(locals, stmt.body);
            if (owned.empty())
                return;
            std::unordered_map<std::uint64_t, std::size_t> uses;
            bool repeats_or_captures = false;
            const auto count = [&](const auto& visit, Expr& value) -> void {
                if (value.function || !value.clauses.empty())
                    repeats_or_captures = true;
                if (value.kind == Expr::Kind::Name)
                    ++uses[value.symbol];
                for (auto& child : value.children)
                    if (child)
                        visit(visit, *child);
            };
            count(count, result);
            const auto mark = [&](const auto& visit, Expr& value) -> void {
                if (value.kind == Expr::Kind::Name && consumable(value.type) &&
                    owned.contains(value.symbol) && uses[value.symbol] == 1)
                    value.move_value = true;
                for (auto& child : value.children)
                    if (child)
                        visit(visit, *child);
            };
            if (!repeats_or_captures && result.kind != Expr::Kind::Name)
                mark(mark, result);
        }
    }
    void prepare_method(Stmt& method, const Stmt& record) {
        if (method.prepared)
            return;
        method.prepared = true;
        const auto saved_types = local_types;
        for (const auto& name : method.type_parameters)
            local_types.emplace(name, Type::parameter(name));
        method.parameters[0].type = Type::record(record);
        method.return_type = resolved(method.return_type, method.location);
        for (auto& parameter : method.parameters)
            parameter.type = resolved(parameter.type, parameter.location);
        local_types = saved_types;
        if (method.type_parameters.empty())
            context.pending_methods.emplace_back(&method, &record);
    }
    void prepare_records(Program& program) {
        for (auto& stmt : program.statements)
            if (stmt->kind == Stmt::Kind::Record && stmt->type_parameters.empty()) {
                for (auto& field : stmt->parameters)
                    field.type = concrete(field.type, field.location);
            }
    }
    void run(Program& program) {
        for (auto& stmt : program.statements) {
            if (stmt->kind != Stmt::Kind::Function && stmt->kind != Stmt::Kind::Record)
                continue;
            const auto existing = functions.find(stmt->name);
            if (reserved(stmt->name) ||
                (stmt->kind == Stmt::Kind::Record &&
                 (stmt->name == "dict" || stmt->name == "json")) ||
                (existing != functions.end() && existing->second != stmt.get()))
                fail("E3001", stmt->location,
                     "duplicate or reserved function name '" + stmt->name + "'");
            if (stmt->symbol == 0)
                stmt->symbol = next_symbol++;
        }
        // Function bodies see their parameters and locals, never mutable script variables.
        for (auto& stmt : program.statements) {
            if (stmt->kind == Stmt::Kind::Record && stmt->type_parameters.empty()) {
                for (auto& method : stmt->body) {
                    Analyzer analyzer(declarations, namespaces, next_symbol, context);
                    analyzer.prepare_method(*method, *stmt);
                }
            }
            if (stmt->kind != Stmt::Kind::Function)
                continue;
            std::unordered_set<std::string> names;
            for (auto& parameter : stmt->parameters) {
                valid_variable(parameter.name, parameter.location);
                if (!names.insert(parameter.name).second)
                    fail("E3001", parameter.location,
                         "duplicate parameter '" + parameter.name + "'");
            }
            if (stmt->type_parameters.empty())
                function_body(*stmt);
        }
        in_function = false;
        scopes.clear();
        scopes.emplace_back();
        block(program.statements, false);
    }
};
} // namespace
bool equality_compatible(Type type) {
    std::unordered_set<const Stmt*> seen;
    std::function<bool(Type)> visit = [&](Type value) {
        if (value.kind == Type::Closure || value.kind == Type::Callable ||
            value.kind == Type::Cursor || value.kind == Type::Range)
            return false;
        if (value.element && !visit(*value.element))
            return false;
        if (value.kind == Type::Tuple)
            for (const auto& item : value.parameters)
                if (!visit(item))
                    return false;
        if (value.kind == Type::Record && seen.insert(value.declaration).second)
            for (const auto& field : value.declaration->parameters)
                if (!visit(field.type))
                    return false;
        return true;
    };
    return visit(type);
}
bool json_compatible(Type type) {
    std::unordered_set<const Stmt*> seen;
    std::function<bool(Type)> visit = [&](Type value) {
        if (value.kind == Type::List || value.kind == Type::Optional)
            return visit(*value.element);
        if (value.kind == Type::Tuple)
            return std::all_of(value.parameters.begin(), value.parameters.end(), visit);
        if (value.kind == Type::Dict)
            return *value.key == Type::Str && visit(*value.element);
        if (value.kind == Type::Record) {
            if (!seen.insert(value.declaration).second)
                return true;
            for (const auto& field : value.declaration->parameters)
                if (!visit(field.type))
                    return false;
            return true;
        }
        return value.kind != Type::Array && value != Type::Void && value != Type::Unknown &&
               value.kind != Type::Named && value.kind != Type::Function &&
               value.kind != Type::Stream && value.kind != Type::Database &&
               value.kind != Type::Statement && value.kind != Type::Transaction &&
               value.kind != Type::TlsContext && value.kind != Type::Task &&
               value.kind != Type::Parameter && value.kind != Type::Closure &&
               value.kind != Type::Callable && value.kind != Type::Step &&
               value.kind != Type::Cursor && value.kind != Type::Range &&
               value.kind != Type::Projection && value.kind != Type::Signature;
    };
    return visit(type);
}
void resolve_signatures(Program& program, const FunctionBindings& bindings) {
    std::function<void(Stmt*, TypeArguments)> resolve = [&](Stmt* stmt, TypeArguments arguments) {
        if (stmt->kind != Stmt::Kind::Function && stmt->kind != Stmt::Kind::Record)
            return;
        for (const auto& name : stmt->type_parameters) {
            if (reserved(name) || name == "int" || name == "float" || name == "str" ||
                name == "dict" || name == "tuple" || name == "None" || name == "json" ||
                bindings.contains(name) || !arguments.emplace(name, Type::parameter(name)).second)
                fail("E3024", stmt->location,
                     "duplicate or reserved type parameter '" + name + "'");
        }
        stmt->return_type = resolve_type(stmt->return_type, bindings, stmt->location, arguments);
        validate_type_depth(stmt->return_type, stmt->location);
        std::unordered_set<std::string> fields;
        for (std::size_t i = 0; i < stmt->parameters.size(); ++i) {
            auto& parameter = stmt->parameters[i];
            if (!fields.insert(parameter.name).second)
                fail("E3001", parameter.location,
                     "duplicate parameter or field '" + parameter.name + "'");
            if (parameter.default_value) {
                if (!constant_default(*parameter.default_value))
                    fail("E3026", parameter.location,
                         "default arguments require literal values, containers of literals, or "
                         "unary literal expressions");
                std::function<void(Expr&)> normalize = [&](Expr& value) {
                    if (value.kind == Expr::Kind::Name) {
                        if (bindings.contains(value.text))
                            fail("E3026", value.location,
                                 "default literal name is shadowed by a declaration");
                        value.kind = value.text == "None" ? Expr::Kind::None : Expr::Kind::Bool;
                        if (value.text != "None")
                            value.text = value.text == "True" ? "true" : "false";
                    }
                    for (auto& child : value.children)
                        normalize(*child);
                };
                normalize(*parameter.default_value);
            }
            parameter.type = resolve_type(parameter.type, bindings, parameter.location, arguments);
            validate_type_depth(parameter.type, parameter.location);
            if (stmt->kind == Stmt::Kind::Record) {
                parameter.symbol = i;
            }
        }
        if (stmt->kind == Stmt::Kind::Record) {
            for (auto& method : stmt->body) {
                if (!fields.insert(method->name).second)
                    fail("E3001", method->location,
                         "duplicate record member '" + method->name + "'");
                auto receiver = Type::record(*stmt);
                for (const auto& name : stmt->type_parameters)
                    receiver.parameters.push_back(arguments.at(name));
                method->parameters[0].type = receiver;
                resolve(method.get(), arguments);
            }
        }
        if (stmt->external) {
            if (stmt->return_type != Type::Void && stmt->return_type != Type::Int &&
                stmt->return_type != Type::Float)
                fail("E3023", stmt->location, "external functions return only int, float, or void");
            for (const auto& parameter : stmt->parameters) {
                if (parameter.mutable_borrow &&
                    (parameter.type.kind != Type::Array || parameter.default_value))
                    fail("E3023", parameter.location,
                         "mut native parameters require an array and cannot have defaults");
                if (parameter.type != Type::Int && parameter.type != Type::Float &&
                    parameter.type != Type::Str &&
                    !(parameter.type.kind == Type::Array && numeric(*parameter.type.element))) {
                    const auto& type = parameter.type;
                    if (type.kind != Type::Function ||
                        (*type.element != Type::Int && *type.element != Type::Float &&
                         *type.element != Type::Void) ||
                        std::any_of(
                            type.parameters.begin(), type.parameters.end(),
                            [](const Type& p) { return p != Type::Int && p != Type::Float; }))
                        fail("E3023", parameter.location,
                             "external parameters require scalars, borrowed bytes or scalar "
                             "callbacks");
                }
            }
            // External identifiers become C linkage names, unlike ordinary Wood
            // functions whose names are always replaced with generated symbols.
            static const std::unordered_set<std::string> keywords = {"alignas",
                                                                     "alignof",
                                                                     "and_eq",
                                                                     "asm",
                                                                     "atomic_cancel",
                                                                     "atomic_commit",
                                                                     "atomic_noexcept",
                                                                     "auto",
                                                                     "bitand",
                                                                     "bitor",
                                                                     "bool",
                                                                     "catch",
                                                                     "char",
                                                                     "char8_t",
                                                                     "char16_t",
                                                                     "char32_t",
                                                                     "class",
                                                                     "co_await",
                                                                     "co_return",
                                                                     "co_yield",
                                                                     "compl",
                                                                     "concept",
                                                                     "const",
                                                                     "consteval",
                                                                     "constexpr",
                                                                     "constinit",
                                                                     "const_cast",
                                                                     "decltype",
                                                                     "default",
                                                                     "delete",
                                                                     "do",
                                                                     "double",
                                                                     "dynamic_cast",
                                                                     "enum",
                                                                     "explicit",
                                                                     "export",
                                                                     "extern",
                                                                     "false",
                                                                     "float",
                                                                     "friend",
                                                                     "goto",
                                                                     "inline",
                                                                     "int",
                                                                     "long",
                                                                     "mutable",
                                                                     "namespace",
                                                                     "new",
                                                                     "noexcept",
                                                                     "not_eq",
                                                                     "nullptr",
                                                                     "operator",
                                                                     "private",
                                                                     "protected",
                                                                     "public",
                                                                     "register",
                                                                     "reinterpret_cast",
                                                                     "requires",
                                                                     "short",
                                                                     "signed",
                                                                     "sizeof",
                                                                     "static",
                                                                     "static_assert",
                                                                     "static_cast",
                                                                     "struct",
                                                                     "switch",
                                                                     "synchronized",
                                                                     "template",
                                                                     "this",
                                                                     "thread_local",
                                                                     "throw",
                                                                     "true",
                                                                     "typedef",
                                                                     "typeid",
                                                                     "typename",
                                                                     "union",
                                                                     "unsigned",
                                                                     "using",
                                                                     "virtual",
                                                                     "void",
                                                                     "volatile",
                                                                     "wchar_t",
                                                                     "xor",
                                                                     "xor_eq"};
            if (keywords.contains(stmt->name) || stmt->name.starts_with('_') ||
                stmt->name.find("__") != std::string::npos)
                fail("E3023", stmt->location,
                     "external symbol must be a non-reserved C identifier");
        }
    };
    for (auto& stmt : program.statements)
        resolve(stmt.get(), {});
}
void validate_records(const std::vector<const Program*>& programs) {
    std::unordered_map<std::string, const Stmt*> externals;
    for (const auto* program : programs)
        for (const auto& stmt : program->statements) {
            if (!stmt->external)
                continue;
            const auto [found, inserted] = externals.emplace(stmt->name, stmt.get());
            if (!inserted) {
                const auto* previous = found->second;
                bool same = previous->return_type == stmt->return_type &&
                            previous->parameters.size() == stmt->parameters.size();
                for (std::size_t i = 0; same && i < stmt->parameters.size(); ++i)
                    same = previous->parameters[i].type == stmt->parameters[i].type &&
                           previous->parameters[i].mutable_borrow ==
                               stmt->parameters[i].mutable_borrow;
                if (!same)
                    fail("E3023", stmt->location,
                         "conflicting external declarations for '" + stmt->name + "'");
            }
        }
    std::unordered_map<const Stmt*, unsigned> state;
    std::unordered_map<const Stmt*, unsigned> heights;
    std::unordered_map<const Stmt*, std::vector<const Stmt*>> edges;
    std::function<void(const Stmt*, const Type&)> collect = [&](const Stmt* record,
                                                                const Type& type) {
        if (type.kind == Type::Record)
            edges[record].push_back(type.declaration);
        else if (type.kind == Type::Optional)
            collect(record, *type.element);
        else if (type.kind == Type::Tuple)
            for (const auto& element : type.parameters)
                collect(record, element);
    };
    std::vector<const Stmt*> records;
    for (const auto* program : programs)
        for (const auto& stmt : program->statements)
            if (stmt->kind == Stmt::Kind::Record) {
                if (stmt->type_parameters.empty())
                    records.push_back(stmt.get());
                else
                    for (const auto& instance : stmt->specializations)
                        records.push_back(instance.get());
            }
    for (const auto* record : records)
        for (const auto& field : record->parameters)
            collect(record, field.type);
    struct Frame {
        const Stmt* record;
        std::size_t field = 0;
        unsigned height = 0;
    };
    for (const auto* record : records)
        if (state[record] != 2) {
            std::vector<Frame> stack{{record}};
            state[record] = 1;
            while (!stack.empty()) {
                auto& frame = stack.back();
                if (frame.field == edges[frame.record].size()) {
                    heights[frame.record] = frame.height;
                    state[frame.record] = 2;
                    stack.pop_back();
                    continue;
                }
                const auto* child = edges[frame.record][frame.field];
                if (state[child] == 1)
                    fail("E3021", frame.record->location,
                         "recursive record requires a list or dictionary indirection");
                if (state[child] != 2) {
                    state[child] = 1;
                    stack.push_back({child});
                    continue;
                }
                frame.height = std::max(frame.height, heights.at(child) + 1);
                if (frame.height > 256)
                    fail("E2002", frame.record->location, "record containment exceeds 256 levels");
                ++frame.field;
            }
        }
}
void analyze(Program& program) {
    for (const auto& stmt : program.statements)
        if (stmt->kind == Stmt::Kind::Import)
            fail("E3100", stmt->location, "imports require a filesystem compilation context");
    std::uint64_t symbol = 1;
    FunctionBindings bindings;
    for (auto& stmt : program.statements)
        if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record) {
            if (!bindings.emplace(stmt->name, stmt.get()).second)
                fail("E3001", stmt->location, "duplicate declaration '" + stmt->name + "'");
            stmt->symbol = symbol++;
        }
    resolve_signatures(program, bindings);
    analyze_module(program, std::move(bindings), {}, symbol);
}
void analyze_module(Program& program, FunctionBindings bindings,
                    std::vector<std::string> namespaces, std::uint64_t& symbol) {
    analyze_bodies({&program}, {std::move(bindings)}, {std::move(namespaces)}, symbol);
}
void analyze_bodies(std::vector<Program*> programs, std::vector<FunctionBindings> environments,
                    std::vector<std::vector<std::string>> namespaces, std::uint64_t& symbol) {
    AnalysisContext context{std::move(environments), std::move(namespaces), {}};
    for (std::size_t i = 0; i < programs.size(); ++i)
        for (auto& stmt : programs[i]->statements)
            if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record) {
                const auto [found, inserted] =
                    context.environments[i].emplace(stmt->name, stmt.get());
                if (!inserted && found->second != stmt.get())
                    fail("E3001", stmt->location, "duplicate declaration '" + stmt->name + "'");
                if (stmt->symbol == 0)
                    stmt->symbol = symbol++;
                context.owners.emplace(stmt.get(), i);
                if (stmt->kind == Stmt::Kind::Record)
                    for (auto& method : stmt->body) {
                        method->symbol = symbol++;
                        context.owners.emplace(method.get(), i);
                    }
            }
    for (std::size_t i = 0; i < programs.size(); ++i)
        Analyzer(context.environments[i], context.namespaces[i], symbol, context)
            .prepare_records(*programs[i]);
    for (std::size_t i = 0; i < programs.size(); ++i)
        Analyzer(context.environments[i], context.namespaces[i], symbol, context).run(*programs[i]);
    // Check concrete methods as a worklist, avoiding compiler stack growth for
    // long chains of mutually calling methods. Signatures are available first.
    for (std::size_t i = 0; i < context.pending_methods.size(); ++i) {
        const auto [method, record] = context.pending_methods[i];
        const auto owner = context.owners.at(method);
        try {
            Analyzer(context.environments[owner], context.namespaces[owner], symbol, context)
                .function_body(*method);
        } catch (const Diagnostic& error) {
            fail(error.code, error.location,
                 std::string(error.what()) + "\n  while checking method " +
                     type_name(Type::record(*record)) + "." + method->name);
        }
    }
    std::vector<const Program*> records(programs.begin(), programs.end());
    validate_records(records);
}
} // namespace wood
