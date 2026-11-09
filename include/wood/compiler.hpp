#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace wood {
struct Location {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::size_t source = 0;
};

struct Diagnostic : std::runtime_error {
    std::string code;
    Location location;
    std::string file;
    std::string source_text;
    Diagnostic(std::string code, Location location, std::string message);
};
[[noreturn]] void fail(std::string code, Location location, std::string message);
std::string render_diagnostic(const Diagnostic& error, std::string_view file,
                              std::string_view source, bool json = false);

enum class TokenKind {
    Eof,
    Newline,
    Indent,
    Dedent,
    Identifier,
    Integer,
    Float,
    String,
    Fn,
    Import,
    From,
    As,
    Try,
    Except,
    Raise,
    Dot,
    If,
    Elif,
    Else,
    While,
    For,
    In,
    Return,
    Break,
    Continue,
    Pass,
    True,
    False,
    And,
    Or,
    Not,
    LParen,
    RParen,
    LBracket,
    RBracket,
    LBrace,
    RBrace,
    Comma,
    Colon,
    Arrow,
    FatArrow,
    Plus,
    Minus,
    Star,
    Slash,
    FloorDiv,
    Percent,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    ShiftLeft,
    ShiftRight,
    Augment,
    Equal,
    EqualEqual,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual
};
struct Token {
    TokenKind kind;
    std::string text;
    Location location;
};
std::vector<Token> lex(std::string_view source, std::size_t source_id = 0);

// Structural types keep collection element types explicit through every pass.
struct Stmt;
struct Type {
    enum Kind {
        Unknown,
        Int,
        Float,
        Bool,
        Str,
        Void,
        List,
        Error,
        Dict,
        Named,
        Record,
        Json,
        Function,
        Stream,
        Database,
        Statement,
        Transaction,
        TlsContext,
        Task,
        Tuple,
        Optional,
        None,
        Parameter,
        Closure,
        Callable,
        Step,
        Cursor,
        Range,
        Projection,
        Signature,
        Array
    };
    Kind kind;
    std::shared_ptr<const Type> element;
    std::shared_ptr<const Type> key;
    std::string name;
    const Stmt* declaration = nullptr;
    std::vector<Type> parameters;
    Type(Kind kind = Unknown) : kind(kind) {}
    static Type list(Type element);
    static Type dict(Type key, Type value);
    static Type named(std::string name);
    static Type record(const Stmt& declaration);
    static Type callable(std::vector<Type> parameters, Type result);
    static Type tuple(std::vector<Type> elements);
    static Type optional(Type element);
    static Type parameter(std::string name);
    static Type wrapped(Kind kind, Type element);
    static Type closure(const Stmt& declaration);
    bool operator==(const Type& other) const;
    bool operator==(Kind other) const {
        return kind == other;
    }
};
std::string type_name(const Type& type);
bool json_compatible(Type type);
bool equality_compatible(Type type);
struct ComprehensionClause {
    std::string name; // Empty for a filter, otherwise the loop binding.
    Location location;
    Type type = Type::Unknown;
    std::uint64_t symbol = 0;
};
struct Expr {
    enum class Kind {
        Integer,
        Float,
        String,
        Bool,
        Name,
        Unary,
        Binary,
        Call,
        Invoke,
        List,
        Dict,
        Index,
        Member,
        Slice,
        CompareChain,
        Conditional,
        ListComprehension,
        DictComprehension,
        Tuple,
        None,
        TypeApply,
        Closure
    };
    Kind kind;
    Location location;
    std::string text;
    std::vector<std::unique_ptr<Expr>> children;
    std::vector<std::string> operators;
    // Call arguments stay in source order; binding maps each to a parameter.
    std::vector<std::string> keywords;
    std::vector<std::size_t> argument_order;
    std::vector<Type> type_arguments;
    const Stmt* callee = nullptr;
    const Stmt* iteration_next = nullptr;
    std::unique_ptr<Stmt> function;
    // Comprehension children: result (or key/value), then each clause expression.
    std::vector<ComprehensionClause> clauses;
    Type type = Type::Unknown;
    std::uint64_t symbol = 0;
    bool constructs = false;
    bool refers_function = false;
    bool move_value = false;
    Expr(Kind kind, Location location, std::string text);
};
using ExprPtr = std::unique_ptr<Expr>;
struct Stmt;
using Block = std::vector<std::unique_ptr<Stmt>>;
struct Parameter {
    std::string name;
    Type type;
    Location location;
    std::uint64_t symbol = 0;
    enum Mode { Positional, PositionalOnly, KeywordOnly } mode = Positional;
    ExprPtr default_value;
    bool mutable_borrow = false;
};
struct ImportName {
    std::string name, alias;
    Location location;
};
struct Stmt {
    enum class Kind {
        Assign,
        IndexAssign,
        AugAssign,
        Expression,
        If,
        While,
        For,
        Return,
        Break,
        Continue,
        Pass,
        Function,
        Record,
        Import,
        Try,
        Raise,
        With,
        // try ... finally: body is the protected block (a Try when there is an except), otherwise the cleanup.
        Finally
    };
    Kind kind;
    Location location;
    std::string name;
    std::vector<ExprPtr> expressions;
    Block body;
    Block otherwise;
    std::vector<Parameter> parameters;
    std::vector<Parameter> captures;
    std::vector<std::string> mutable_captures;
    std::vector<std::string> type_parameters;
    // Concrete native functions, owned by their generic declaration.
    Block specializations;
    std::vector<Type> type_arguments;
    std::vector<ImportName> imports;
    std::string module;
    std::size_t relative = 0;
    std::size_t resolved_module = 0;
    Type return_type = Type::Void;
    Type annotation = Type::Unknown;
    bool declares = false;
    bool external = false;
    bool method = false;
    bool mutating = false;
    bool inferred_return = false;
    bool expression_body = false;
    bool checked = false;
    bool prepared = false;
    const Stmt* origin = nullptr;
    std::size_t capture_depth = 0;
    std::uint64_t symbol = 0;
    Stmt(Kind kind, Location location);
};
struct Program {
    Block statements;
};
Program parse(const std::vector<Token>& tokens);
void analyze(Program& program);
using FunctionBindings = std::unordered_map<std::string, Stmt*>;
void resolve_signatures(Program& program, const FunctionBindings& bindings);
void validate_records(const std::vector<const Program*>& programs);
void analyze_module(Program& program, FunctionBindings bindings,
                    std::vector<std::string> namespaces, std::uint64_t& next_symbol);
void analyze_bodies(std::vector<Program*> programs, std::vector<FunctionBindings> environments,
                    std::vector<std::vector<std::string>> namespaces, std::uint64_t& next_symbol);
// Produces a complete translation unit when prefixed with the embedded runtime.
std::string generate(const Program& program, std::string_view source_file,
                     bool json_runtime = false);
std::string compile(std::string_view source, std::string_view source_file,
                    bool json_runtime = false);

struct PackageRoot {
    std::filesystem::path root;
    std::map<std::string, std::size_t> dependencies;
    std::filesystem::path test_root;
};
struct ModuleUnit {
    std::string file, source;
    std::filesystem::path path;
    std::size_t package = 0;
    Program program;
};
struct Compilation {
    std::vector<ModuleUnit> modules;
    std::size_t entry = 0;
};
Compilation load_modules(const std::filesystem::path& entry,
                         std::vector<PackageRoot> packages = {});
// Complete module declaration/import binding, before signature/body analysis.
// Environments borrow AST declaration pointers from the Compilation. Keep its
// declarations alive and structurally unchanged while consuming the result.
struct ModuleBindings {
    std::vector<FunctionBindings> environments;
    std::vector<std::vector<std::string>> namespaces;
    std::uint64_t next_symbol = 1;
};
ModuleBindings bind_modules(Compilation& compilation);
void analyze(Compilation& compilation);
std::string generate(const Compilation& compilation, bool json_runtime = false);
} // namespace wood
