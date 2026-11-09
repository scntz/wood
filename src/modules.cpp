#include "wood/builtins.hpp"
#include "wood/compiler.hpp"
#include "wood/standard_sources.hpp"
#include "wood/support.hpp"
#include <algorithm>
#include <set>

namespace wood {
namespace {
std::string identity(const fs::path& path) {
    auto text = path_text(resolve_path(path));
#ifdef _WIN32
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
#endif
    return text;
}
bool reserved_alias(const std::string& name) {
    return reserved_builtin(name) || name == "range" || name == "list" || name == "bool" ||
           name == "void" || name == "error";
}
class Loader {
    Compilation result;
    std::vector<PackageRoot> packages;
    std::map<std::string, std::size_t> known;
    std::set<std::size_t> active;
    std::vector<std::string> chain;
    std::string importing;
    std::size_t bytes = 0;
    std::size_t load(fs::path path, std::size_t package, std::string standard, Location at) {
        std::string key, text, file;
        if (!standard.empty()) {
            key = "@" + standard;
            file = "<" + standard + ">";
            for (const auto& [name, source] : standard_sources)
                if (name == standard)
                    text = source;
            if (text.empty())
                fail("E3101", at, "unknown standard module '" + standard + "'");
        } else {
            try {
                path = resolve_path(path);
            } catch (const std::exception& error) {
                if (chain.empty())
                    fail("E3101", at, "cannot resolve module: " + std::string(error.what()));
                fail("E3101", at, "cannot find module '" + importing + "': " + std::string(error.what()));
            }
            if (!fs::is_regular_file(path))
                fail("E3101", at, "module file is not regular");
            const auto& roots = packages.at(package);
            if (!contained(path, roots.root) &&
                (roots.test_root.empty() || !contained(path, roots.test_root))) {
                if (chain.empty())
                    fail("E3106", at, "'" + path_text(path.filename()) + "' is outside this project's source directory. Move it there, or run it from a folder that has no wood.json");
                fail("E3106", at, "module escapes its declared source root");
            }
            key = std::to_string(package) + ":" + identity(path);
            file = path_text(path);
        }
        if (auto found = known.find(key); found != known.end()) {
            if (active.contains(found->second)) {
                std::string cycle;
                for (const auto& part : chain)
                    cycle += part + " -> ";
                fail("E3102", at, "cyclic import: " + cycle + file);
            }
            return found->second;
        }
        if (result.modules.size() >= 1024 || chain.size() >= 128)
            fail("E3106", at, "module graph exceeds 1024 modules or 128 import levels");
        if (standard.empty())
            text = read_file(path);
        bytes += text.size();
        if (bytes > 128 * 1024 * 1024)
            fail("E3106", at, "module sources exceed 128 MiB");
        const auto id = result.modules.size();
        known.emplace(key, id);
        result.modules.push_back({file, text, path, package, {}});
        result.modules[id].program = parse(lex(text, id));
        active.insert(id);
        chain.push_back(file);
        for (auto& stmt : result.modules[id].program.statements) {
            if (stmt->kind != Stmt::Kind::Import) {
                if (id != 0 && stmt->kind != Stmt::Kind::Function &&
                    stmt->kind != Stmt::Kind::Record)
                    fail("E3103", stmt->location,
                         "imported modules may contain only functions and imports");
                continue;
            }
            auto target_package = package;
            auto module = stmt->module;
            if (module.starts_with("std.") && stmt->relative == 0) {
                stmt->resolved_module = load({}, package, module, stmt->location);
                continue;
            }
            if (!standard.empty())
                fail("E3106", stmt->location,
                     "standard modules can only import explicit std modules");
            fs::path base;
            if (stmt->relative) {
                base = path.parent_path();
                for (std::size_t n = 1; n < stmt->relative; ++n)
                    base = base.parent_path();
            } else {
                const auto dot = module.find('.');
                const auto first = module.substr(0, dot);
                const auto dependency = packages[package].dependencies.find(first);
                if (dependency != packages[package].dependencies.end()) {
                    if (dot == std::string::npos)
                        fail("E3101", stmt->location,
                             "import a module inside dependency '" + first + "'");
                    target_package = dependency->second;
                    module = module.substr(dot + 1);
                }
                base = packages[target_package].root;
            }
            std::replace(module.begin(), module.end(), '.', '/');
            const auto target = base / path_from(module + ".wd");
            // Canonical containment also blocks source symlinks escaping a package.
            importing = stmt->module;
            stmt->resolved_module = load(target, target_package, "", stmt->location);
        }
        chain.pop_back();
        active.erase(id);
        return id;
    }

  public:
    explicit Loader(std::vector<PackageRoot> roots) : packages(std::move(roots)) {}
    Compilation run(const fs::path& entry) {
        if (packages.empty())
            packages.push_back({resolve_path(entry).parent_path(), {}});
        try {
            result.entry = load(entry, 0, "", {});
        } catch (Diagnostic& error) {
            if (error.location.source < result.modules.size()) {
                error.file = result.modules[error.location.source].file;
                error.source_text = result.modules[error.location.source].source;
            }
            throw;
        }
        return std::move(result);
    }
};
} // namespace
Compilation load_modules(const fs::path& entry, std::vector<PackageRoot> packages) {
    return Loader(std::move(packages)).run(entry);
}
ModuleBindings bind_modules(Compilation& compilation) {
    std::uint64_t symbol = 1;
    try {
        for (auto& module : compilation.modules)
            for (auto& stmt : module.program.statements)
                if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record)
                    stmt->symbol = symbol++;
        std::vector<FunctionBindings> environments;
        std::vector<std::vector<std::string>> namespace_sets;
        for (auto& module : compilation.modules) {
            FunctionBindings bindings;
            std::vector<std::string> namespaces;
            std::set<std::string> import_paths;
            for (auto& stmt : module.program.statements)
                if (stmt->kind == Stmt::Kind::Function || stmt->kind == Stmt::Kind::Record) {
                    if (!bindings.emplace(stmt->name, stmt.get()).second)
                        fail("E3001", stmt->location, "duplicate function '" + stmt->name + "'");
                }
            for (auto& stmt : module.program.statements)
                if (stmt->kind == Stmt::Kind::Import) {
                    auto& target = compilation.modules.at(stmt->resolved_module);
                    std::map<std::string, Stmt*> exports;
                    for (auto& function : target.program.statements)
                        if ((function->kind == Stmt::Kind::Function ||
                             function->kind == Stmt::Kind::Record) &&
                            !function->name.starts_with('_'))
                            exports.emplace(function->name, function.get());
                    if (stmt->imports.empty()) {
                        const auto root = stmt->name.substr(0, stmt->name.find('.'));
                        if (reserved_alias(root) || bindings.contains(root) ||
                            !import_paths.insert(stmt->name).second)
                            fail("E3105", stmt->location,
                                 "duplicate or reserved module binding '" + stmt->name + "'");
                        namespaces.push_back(root);
                        for (auto& [name, function] : exports)
                            if (!bindings.emplace(stmt->name + "." + name, function).second)
                                fail("E3105", stmt->location,
                                     "duplicate imported function binding");
                    } else
                        for (auto& item : stmt->imports) {
                            const auto found = exports.find(item.name);
                            if (found == exports.end())
                                fail("E3104", item.location,
                                     "module does not export function '" + item.name + "'");
                            if (reserved_alias(item.alias) || bindings.contains(item.alias) ||
                                std::find(namespaces.begin(), namespaces.end(), item.alias) !=
                                    namespaces.end())
                                fail("E3105", item.location,
                                     "duplicate or reserved import binding '" + item.alias + "'");
                            bindings.emplace(item.alias, found->second);
                        }
                }
            environments.push_back(std::move(bindings));
            namespace_sets.push_back(std::move(namespaces));
        }
        return {std::move(environments), std::move(namespace_sets), symbol};
    } catch (Diagnostic& error) {
        if (error.location.source < compilation.modules.size()) {
            error.file = compilation.modules[error.location.source].file;
            error.source_text = compilation.modules[error.location.source].source;
        }
        throw;
    }
}
void analyze(Compilation& compilation) {
    try {
        auto bindings = bind_modules(compilation);
        std::vector<const Program*> programs;
        for (std::size_t i = 0; i < compilation.modules.size(); ++i) {
            resolve_signatures(compilation.modules[i].program, bindings.environments[i]);
            programs.push_back(&compilation.modules[i].program);
        }
        std::vector<Program*> mutable_programs;
        for (auto& module : compilation.modules)
            mutable_programs.push_back(&module.program);
        analyze_bodies(std::move(mutable_programs), std::move(bindings.environments),
                       std::move(bindings.namespaces), bindings.next_symbol);
    } catch (Diagnostic& error) {
        if (error.location.source < compilation.modules.size()) {
            error.file = compilation.modules[error.location.source].file;
            error.source_text = compilation.modules[error.location.source].source;
        }
        throw;
    }
}
} // namespace wood
