#include "wood/compiler.hpp"
#include "wood/support.hpp"
#include <algorithm>
#include <sstream>
#include <utility>

namespace wood {
Diagnostic::Diagnostic(std::string id, Location at, std::string message)
    : std::runtime_error(std::move(message)), code(std::move(id)), location(at) {}
[[noreturn]] void fail(std::string code, Location location, std::string message) {
    throw Diagnostic(std::move(code), location, std::move(message));
}
std::string render_diagnostic(const Diagnostic& error, std::string_view file,
                              std::string_view source, bool json) {
    std::ostringstream out;
    if (json) {
        return Json(Json::Object{{"severity", "error"},
                                 {"code", error.code},
                                 {"file", std::string(file)},
                                 {"line", static_cast<std::int64_t>(error.location.line)},
                                 {"column", static_cast<std::int64_t>(error.location.column)},
                                 {"message", error.what()}})
                   .dump() +
               "\n";
    }
    out << file << ':' << error.location.line << ':' << error.location.column << ": error["
        << error.code << "]: " << error.what() << '\n';
    const auto offset = std::min(error.location.offset, source.size());
    const auto preceding = offset == 0 ? std::string_view::npos : source.rfind('\n', offset - 1);
    const auto start = preceding == std::string_view::npos ? 0 : preceding + 1;
    const auto end = source.find_first_of("\r\n", start);
    out << "  "
        << source.substr(start, end == std::string_view::npos ? source.size() - start : end - start)
        << '\n'
        << "  " << std::string(error.location.column - 1, ' ') << "^\n";
    return out.str();
}
Type Type::list(Type value) {
    Type result(List);
    result.element = std::make_shared<const Type>(std::move(value));
    return result;
}
bool Type::operator==(const Type& other) const {
    return kind == other.kind &&
           ((kind != List && kind != Array && kind != Dict && kind != Function &&
             kind != Callable && kind != Closure && kind != Step && kind != Cursor &&
             kind != Projection && kind != Signature && kind != Optional) ||
            *element == *other.element) &&
           ((kind != Function && kind != Callable && kind != Closure && kind != Signature &&
             kind != Tuple && kind != Record && kind != Named) ||
            parameters == other.parameters) &&
           ((kind != Signature && kind != Projection) || name == other.name) &&
           (kind != Dict || *key == *other.key) &&
           ((kind != Closure) || declaration == other.declaration) &&
           (kind != Record ||
            (declaration->origin ? declaration->origin : declaration) ==
                (other.declaration->origin ? other.declaration->origin : other.declaration)) &&
           ((kind != Named && kind != Parameter) || name == other.name);
}
Type Type::wrapped(Kind kind, Type value) {
    Type result(kind);
    result.element = std::make_shared<const Type>(std::move(value));
    return result;
}
Type Type::closure(const Stmt& declaration) {
    auto result = callable({}, declaration.return_type);
    result.kind = Closure;
    result.declaration = &declaration;
    for (const auto& parameter : declaration.parameters)
        result.parameters.push_back(parameter.type);
    return result;
}
Type Type::tuple(std::vector<Type> elements) {
    Type result(Tuple);
    result.parameters = std::move(elements);
    return result;
}
Type Type::optional(Type value) {
    if (value.kind == Optional || value == None)
        return value;
    Type result(Optional);
    result.element = std::make_shared<const Type>(std::move(value));
    return result;
}
Type Type::parameter(std::string name) {
    auto result = named(std::move(name));
    result.kind = Parameter;
    return result;
}
Type Type::named(std::string name) {
    Type result(Named);
    result.name = std::move(name);
    return result;
}
Type Type::record(const Stmt& declaration) {
    Type result(Record);
    result.declaration = &declaration;
    result.parameters = declaration.type_arguments;
    return result;
}
Type Type::dict(Type key, Type value) {
    Type result(Dict);
    result.key = std::make_shared<const Type>(std::move(key));
    result.element = std::make_shared<const Type>(std::move(value));
    return result;
}
Type Type::callable(std::vector<Type> parameters, Type value) {
    Type result(Function);
    result.parameters = std::move(parameters);
    result.element = std::make_shared<const Type>(std::move(value));
    return result;
}
std::string type_name(const Type& type) {
    if (type.kind == Type::Array)
        return "array[" + type_name(*type.element) + "]";
    if (type.kind == Type::Function || type.kind == Type::Callable || type.kind == Type::Closure ||
        type.kind == Type::Signature) {
        std::string result = type.kind == Type::Signature  ? type.name + "("
                             : type.kind == Type::Callable ? "callable("
                             : type.kind == Type::Closure  ? "closure("
                                                           : "fn(";
        for (std::size_t i = 0; i < type.parameters.size(); ++i) {
            if (i)
                result += ", ";
            result += type_name(type.parameters[i]);
        }
        return result + ") -> " + type_name(*type.element);
    }
    if (type.kind == Type::Named || type.kind == Type::Parameter)
        return type.name;
    if (type.kind == Type::Range)
        return "range";
    if (type.kind == Type::Step || type.kind == Type::Cursor)
        return std::string(type.kind == Type::Step ? "step[" : "cursor[") +
               type_name(*type.element) + "]";
    if (type.kind == Type::Projection)
        return type_name(*type.element) + "." + type.name;
    if (type.kind == Type::None)
        return "None";
    if (type.kind == Type::Optional)
        return type_name(*type.element) + " | None";
    if (type.kind == Type::Tuple) {
        if (type.parameters.empty())
            return "tuple[()]";
        std::string result = "tuple[";
        for (const auto& element : type.parameters) {
            if (result.size() > 6)
                result += ", ";
            result += type_name(element);
        }
        return result + "]";
    }
    if (type.kind == Type::Record) {
        std::string result = type.declaration->name;
        if (!type.parameters.empty()) {
            result += "[";
            for (std::size_t i = 0; i < type.parameters.size(); ++i) {
                if (i)
                    result += ", ";
                result += type_name(type.parameters[i]);
            }
            result += "]";
        }
        return result;
    }
    if (type.kind == Type::Dict)
        return "dict[" + type_name(*type.key) + ", " + type_name(*type.element) + "]";
    switch (type.kind) {
    case Type::Int:
        return "int";
    case Type::Float:
        return "float";
    case Type::Bool:
        return "bool";
    case Type::Str:
        return "str";
    case Type::Void:
        return "void";
    case Type::Error:
        return "error";
    case Type::Json:
        return "json";
    case Type::Stream:
        return "stream";
    case Type::Database:
        return "database";
    case Type::Statement:
        return "statement";
    case Type::Transaction:
        return "transaction";
    case Type::TlsContext:
        return "tls_context";
    case Type::Task:
        return "task";
    case Type::List:
        return "list[" + type_name(*type.element) + "]";
    default:
        return "unknown";
    }
}
Expr::Expr(Kind k, Location at, std::string value)
    : kind(k), location(at), text(std::move(value)) {}
Stmt::Stmt(Kind k, Location at) : kind(k), location(at) {}
std::string compile(std::string_view source, std::string_view source_file, bool json_runtime) {
    auto program = parse(lex(source));
    analyze(program);
    return generate(program, source_file, json_runtime);
}
} // namespace wood
