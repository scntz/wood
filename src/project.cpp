#include "wood/project.hpp"
#include "wood/standard_sources.hpp"
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>

namespace wood {
namespace {
bool identifier(std::string_view name) {
    if (name.empty() || !((name[0] >= 'a' && name[0] <= 'z') ||
                          (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_'))
        return false;
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
}
std::string relative_name(const fs::path& path, const fs::path& root) {
    const auto relative = resolve_path(path).lexically_relative(resolve_path(root));
    if (relative.empty())
        throw std::runtime_error(
            "package paths cannot be represented relative to the project root");
    return path_text(relative);
}
bool version(std::string_view name) {
    std::istringstream input{std::string(name)};
    std::string part;
    int count = 0;
    while (std::getline(input, part, '.')) {
        if (part.empty() || (part.size() > 1 && part[0] == '0') ||
            !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return false;
        ++count;
    }
    return count == 3 && !name.ends_with('.');
}
void keys(const Json& value, const std::set<std::string>& allowed) {
    for (const auto& [key, item] : value.object()) {
        (void)item;
        if (!allowed.contains(key))
            throw std::runtime_error("unknown manifest field '" + key + "'");
    }
}
fs::path inside(const fs::path& root, const std::string& path) {
    if (path.find('\0') != std::string::npos || path_from(path).is_absolute())
        throw std::runtime_error("manifest paths must be relative and contain no NUL");
    const auto result = resolve_path(root / path_from(path), true);
    if (!contained(result, root))
        throw std::runtime_error("manifest path escapes package: " + path);
    return result;
}
std::string key(const fs::path& path) {
    auto result = path_text(resolve_path(path));
#ifdef _WIN32
    for (char& c : result)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
#endif
    return result;
}
std::vector<fs::path> source_files(const fs::path& root) {
    std::vector<fs::path> result;
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        if (is_link(item.path()))
            throw std::runtime_error("source snapshots do not allow filesystem links: " +
                                     path_text(item.path()));
        if (item.is_regular_file() && item.path().extension() == ".wd")
            result.push_back(item.path());
        if (result.size() > 4096)
            throw std::runtime_error("package exceeds 4096 source files");
    }
    std::sort(result.begin(), result.end(),
              [](const auto& a, const auto& b) { return path_text(a) < path_text(b); });
    return result;
}
} // namespace
Project load_project(const fs::path& manifest) {
    Project result;
    std::map<std::string, std::size_t> seen;
    std::set<std::size_t> active;
    std::function<std::size_t(fs::path)> load = [&](fs::path file) -> std::size_t {
        file = resolve_path(file);
        const auto identity = key(file);
        if (const auto existing = seen.find(identity); existing != seen.end()) {
            if (active.contains(existing->second))
                throw std::runtime_error("cyclic package dependency: " + path_text(file));
            return existing->second;
        }
        if (result.packages.size() >= 128)
            throw std::runtime_error("package graph exceeds 128 packages");
        const auto text = read_file(file, 1024 * 1024);
        Json data;
        try {
            data = Json::parse(text);
            keys(data, {"format", "name", "version", "source", "entry", "tests", "dependencies", "python"});
            // Exact versions for the optional python@ provider; never read by the compiler.
            if (data.contains("python"))
                for (const auto& [package, version] : data.at("python").object()) (void)version.string();
        } catch (const std::exception& error) {
            throw std::runtime_error(path_text(file) + ": " + error.what());
        }
        if (data.at("format").integer() != 1)
            throw std::runtime_error("unsupported wood.json format");
        Package package;
        package.root = file.parent_path();
        package.manifest_text = text;
        package.name = data.at("name").string();
        package.version = data.at("version").string();
        if (!identifier(package.name) || package.name == "std")
            throw std::runtime_error("package name must be an identifier other than std");
        if (!version(package.version))
            throw std::runtime_error(
                "package version must be exact MAJOR.MINOR.PATCH without leading zeros");
        package.source = inside(package.root, data.at("source").string());
        if (!fs::is_directory(package.source))
            throw std::runtime_error("package source directory does not exist");
        if (data.contains("entry"))
            package.entry = inside(package.source, data.at("entry").string());
        if (data.contains("tests")) {
            package.tests = inside(package.root, data.at("tests").string());
            if (!fs::is_directory(package.tests))
                throw std::runtime_error("tests directory does not exist");
        }
        const auto id = result.packages.size();
        seen.emplace(identity, id);
        result.packages.push_back(std::move(package));
        active.insert(id);
        if (data.contains("dependencies"))
            for (const auto& [alias, spec] : data.at("dependencies").object()) {
                if (!identifier(alias) || alias == "std")
                    throw std::runtime_error(
                        "dependency alias must be an identifier other than std");
                keys(spec, {"path", "version", "source"});
                if (spec.contains("source")) (void)spec.at("source").string();
                const auto dependency_path = spec.at("path").string();
                if (dependency_path.find('\0') != std::string::npos ||
                    path_from(dependency_path).is_absolute())
                    throw std::runtime_error("dependency paths must be relative");
                const auto target =
                    load(file.parent_path() / path_from(dependency_path) / "wood.json");
                if (spec.at("version").string() != result.packages[target].version)
                    throw std::runtime_error(
                        "dependency '" + alias + "' version mismatch; expected " +
                        spec.at("version").string() + ", found " + result.packages[target].version);
                result.packages[id].dependencies.emplace(alias, target);
            }
        active.erase(id);
        return id;
    };
    load(manifest);
    return result;
}
fs::path discover_project(const fs::path& start) {
    auto directory = fs::absolute(start);
    if (!fs::is_directory(directory))
        directory = directory.parent_path();
    for (;;) {
        if (fs::is_regular_file(directory / "wood.json"))
            return directory / "wood.json";
        if (directory.parent_path() == directory)
            return {};
        directory = directory.parent_path();
    }
}
std::vector<PackageRoot> Project::roots() const {
    std::vector<PackageRoot> result;
    for (std::size_t i = 0; i < packages.size(); ++i)
        result.push_back({packages[i].source, packages[i].dependencies,
                          i == 0 ? packages[i].tests : fs::path{}});
    return result;
}
std::vector<fs::path> Project::tests() const {
    std::vector<fs::path> result;
    if (packages[0].tests.empty())
        return result;
    for (const auto& file : source_files(packages[0].tests))
        if (path_text(file.filename()).starts_with("test_"))
            result.push_back(file);
    return result;
}
Json Project::lock_document(std::string_view language_version) const {
    Json::Array locked;
    std::size_t bytes = 0;
    const auto& root = packages.at(0).root;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        const auto& package = packages[i];
        Json::Object files, dependencies;
        if (i != 0)
            for (const auto& file : source_files(package.source)) {
                auto content = read_file(file);
                bytes += content.size();
                if (bytes > 128 * 1024 * 1024)
                    throw std::runtime_error("dependency sources exceed 128 MiB");
                files.emplace(relative_name(file, package.root), sha256(content));
            }
        for (const auto& [alias, id] : package.dependencies)
            dependencies.emplace(alias, relative_name(packages[id].root, root));
        locked.emplace_back(Json::Object{{"root", relative_name(package.root, root)},
                                         {"name", package.name},
                                         {"version", package.version},
                                         {"manifest_sha256", sha256(package.manifest_text)},
                                         {"sources", Json(std::move(files))},
                                         {"dependencies", Json(std::move(dependencies))}});
    }
    std::sort(locked.begin(), locked.end(), [](const Json& a, const Json& b) {
        return a.at("root").string() < b.at("root").string();
    });
    Json::Object standard;
    for (const auto& [name, source] : standard_sources)
        standard.emplace(std::string(name), sha256(source));
    return Json::Object{{"format", 1},
                        {"compiler", std::string(language_version)},
                        {"stdlib", Json(std::move(standard))},
                        {"packages", Json(std::move(locked))}};
}
void Project::verify_lock(bool required, std::string_view language_version) {
    const auto file = packages[0].root / "wood.lock";
    if (!fs::exists(file)) {
        if (required)
            throw std::runtime_error("wood.lock is required; run 'wood lock' and review it");
        return;
    }
    auto locked = Json::parse(read_file(file, 16 * 1024 * 1024));
    // GitHub and Python pins are package-manager metadata; content verification is unchanged.
    Json::Object content;
    for (const auto& [name, value] : locked.object())
        if (name != "remote" && name != "python") content.emplace(name, value);
    if (Json(std::move(content)).dump() != lock_document(language_version).dump())
        throw std::runtime_error("wood.lock differs from manifests or dependency sources; run "
                                 "'wood lock' and review the change");
    verified_lock = std::move(locked);
}
void Project::verify_compilation(const Compilation& compilation) const {
    if (!verified_lock)
        return;
    for (const auto& module : compilation.modules)
        if (module.package != 0 && !module.path.empty()) {
            const auto& package = packages.at(module.package);
            const auto root = relative_name(package.root, packages[0].root);
            bool checked = false;
            for (const auto& locked : verified_lock->at("packages").array())
                if (locked.at("root").string() == root) {
                    if (locked.at("sources")
                            .at(relative_name(module.path, package.root))
                            .string() != sha256(module.source))
                        throw std::runtime_error("dependency changed after lock verification: " +
                                                 module.file);
                    checked = true;
                    break;
                }
            if (!checked)
                throw std::runtime_error("compiled dependency is absent from wood.lock: " +
                                         module.file);
        }
}
void init_project(const fs::path& directory) {
    if (directory.empty())
        throw std::runtime_error(
            "init directory must not be empty; use '.' for the current directory");
    if (path_text(directory).find('\0') != std::string::npos)
        throw std::runtime_error("init directory must not contain NUL");
    auto target = fs::absolute(directory).lexically_normal();
    // A trailing separator (including normalized '.') must not make the staging
    // parent equal to the target, or discard the directory's package name.
    while (target.filename().empty() && target.has_relative_path())
        target = target.parent_path();
    auto check_target = [&] {
        std::error_code error;
        const auto status = fs::symlink_status(target, error);
        if (error && error != std::errc::no_such_file_or_directory)
            throw fs::filesystem_error("cannot inspect init target", target, error);
        if (fs::is_symlink(status) || (fs::exists(status) && is_link(target)))
            throw std::runtime_error("init target must not be a filesystem link: " +
                                     path_text(target));
        if (fs::exists(status) && (!fs::is_directory(status) || !fs::is_empty(target)))
            throw std::runtime_error("init target must be absent or an empty directory: " +
                                     path_text(target) + ". Choose a new name: wood init NAME");
        return fs::exists(status);
    };
    check_target();
    if (!fs::is_directory(target.parent_path()))
        throw std::runtime_error("init parent must be an existing directory: " +
                                 path_text(target.parent_path()));
    auto name = path_text(target.filename());
    for (char& c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            c = '_';
    if (!identifier(name) || name == "std")
        name = "wood_app";
    TemporaryDirectory stage(target.parent_path());
    fs::create_directory(stage.path / "src");
    fs::create_directory(stage.path / "tests");
    write_file(stage.path / "wood.json", Json(Json::Object{{"format", 1},
                                                           {"name", name},
                                                           {"version", "0.1.0"},
                                                           {"source", "src"},
                                                           {"entry", "main.wd"},
                                                           {"tests", "tests"},
                                                           {"dependencies", Json::Object{}}})
                                                 .dump() +
                                             "\n");
    write_file(stage.path / "src" / "greeting.wd",
               "fn greet(name: str) -> str:\n    return \"hello, \" + name\n");
    write_file(stage.path / "src" / "main.wd",
               "from greeting import greet\nname = \"wood\"\nif len(args()) > 0:\n    name = "
               "args()[0]\nprint(greet(name))\n");
    write_file(stage.path / "tests" / "test_greeting.wd",
               "from greeting import greet\nassert(greet(\"world\") == \"hello, world\")\n");
    if (check_target()) {
        // Preserve the existing directory, especially the process's current
        // directory. Default copy behavior refuses to overwrite existing files.
        // Publish the manifest last so source/test copy failures do not expose a
        // seemingly complete project. Partial new files may remain on I/O failure.
        for (const auto* subtree : {"src", "tests"})
            fs::copy(stage.path / subtree, target / subtree, fs::copy_options::recursive);
        fs::copy_file(stage.path / "wood.json", target / "wood.json");
    } else {
        fs::rename(stage.path, target);
        stage.path.clear();
    }
}
Json inspect_compilation(const Compilation& compilation) {
    std::unordered_map<const Stmt*, std::string> record_modules;
    for (const auto& module : compilation.modules)
        for (const auto& stmt : module.program.statements)
            if (stmt->kind == Stmt::Kind::Record)
                record_modules.emplace(stmt.get(), module.file);
    std::function<Json(const Type&)> type_ref = [&](const Type& type) -> Json {
        if (type.kind == Type::Parameter)
            return Json::Object{{"kind", "type_parameter"}, {"name", type.name}};
        if (type.kind == Type::Optional)
            return Json::Object{{"kind", "optional"}, {"element", type_ref(*type.element)}};
        if (type.kind == Type::Tuple) {
            Json::Array elements;
            for (const auto& element : type.parameters)
                elements.push_back(type_ref(element));
            return Json::Object{{"kind", "tuple"}, {"elements", std::move(elements)}};
        }
        if (type.kind == Type::Projection)
            return Json::Object{{"kind", "associated_type"},
                                {"base", type_ref(*type.element)},
                                {"name", type.name}};
        if (type.kind == Type::Step || type.kind == Type::Cursor)
            return Json::Object{{"kind", type.kind == Type::Step ? "step" : "cursor"},
                                {"element", type_ref(*type.element)}};
        if (type.kind == Type::Function || type.kind == Type::Callable ||
            type.kind == Type::Signature || type.kind == Type::Closure) {
            Json::Array parameters;
            for (const auto& parameter : type.parameters)
                parameters.push_back(type_ref(parameter));
            Json::Object result{{"kind", type.kind == Type::Function    ? "function"
                                         : type.kind == Type::Signature ? "callable_pattern"
                                         : type.kind == Type::Closure   ? "closure"
                                                                        : "callable"},
                                {"parameters", std::move(parameters)},
                                {"returns", type_ref(*type.element)}};
            if (type.kind == Type::Signature)
                result.emplace("name", type.name);
            return result;
        }
        if (type.kind == Type::Record) {
            Json::Array arguments;
            for (const auto& argument : type.parameters)
                arguments.push_back(type_ref(argument));
            const auto* origin =
                type.declaration->origin ? type.declaration->origin : type.declaration;
            return Json::Object{{"kind", "record"},
                                {"module", record_modules.at(origin)},
                                {"name", origin->name},
                                {"arguments", std::move(arguments)}};
        }
        if (type.kind == Type::List || type.kind == Type::Array)
            return Json::Object{{"kind", type.kind == Type::Array ? "array" : "list"},
                                {"element", type_ref(*type.element)}};
        if (type.kind == Type::Dict)
            return Json::Object{
                {"kind", "dict"}, {"key", type_ref(*type.key)}, {"value", type_ref(*type.element)}};
        return Json::Object{{"kind", type_name(type)}};
    };
    auto function_info = [&](const Stmt& stmt) -> Json {
        Json::Array parameters;
        for (const auto& parameter : stmt.parameters)
            parameters.emplace_back(Json::Object{
                {"name", parameter.name},
                {"type", type_name(parameter.type)},
                {"mode", parameter.mode == Parameter::PositionalOnly ? "positional_only"
                         : parameter.mode == Parameter::KeywordOnly  ? "keyword_only"
                                                                     : "positional_or_keyword"},
                {"has_default", bool(parameter.default_value)},
                {"mutable_borrow", parameter.mutable_borrow},
                {"type_ref", type_ref(parameter.type)}});
        Json::Array type_parameters;
        for (const auto& parameter : stmt.type_parameters)
            type_parameters.emplace_back(parameter);
        return Json::Object{
            {"name", stmt.name},
            {"external", stmt.external},
            {"type_parameters", std::move(type_parameters)},
            {"parameters", Json(std::move(parameters))},
            {"returns", type_name(stmt.return_type)},
            {"returns_ref", type_ref(stmt.return_type)},
            {"return_inferred", stmt.inferred_return},
            {"return_resolved", stmt.return_type != Type::Unknown},
            {"line", static_cast<std::int64_t>(stmt.location.line)},
            {"receiver", stmt.method ? (stmt.mutating ? "mutable" : "value") : "none"}};
    };
    Json::Array modules;
    for (const auto& module : compilation.modules) {
        Json::Array exports, imports;
        for (const auto& stmt : module.program.statements) {
            if (stmt->kind == Stmt::Kind::Import)
                imports.emplace_back(compilation.modules[stmt->resolved_module].file);
            if (stmt->kind == Stmt::Kind::Function && !stmt->name.starts_with('_'))
                exports.push_back(function_info(*stmt));
            if (stmt->kind == Stmt::Kind::Record && !stmt->name.starts_with('_')) {
                Json::Array fields, methods, type_parameters;
                for (const auto& name : stmt->type_parameters)
                    type_parameters.emplace_back(name);
                for (const auto& method : stmt->body)
                    methods.push_back(function_info(*method));
                for (const auto& field : stmt->parameters)
                    fields.emplace_back(Json::Object{{"name", field.name},
                                                     {"type", type_name(field.type)},
                                                     {"type_ref", type_ref(field.type)}});
                exports.emplace_back(
                    Json::Object{{"name", stmt->name},
                                 {"kind", "record"},
                                 {"type_ref", type_ref(Type::record(*stmt))},
                                 {"fields", Json(std::move(fields))},
                                 {"methods", Json(std::move(methods))},
                                 {"type_parameters", Json(std::move(type_parameters))},
                                 {"line", static_cast<std::int64_t>(stmt->location.line)}});
            }
        }
        modules.emplace_back(Json::Object{{"file", module.file},
                                          {"sha256", sha256(module.source)},
                                          {"imports", Json(std::move(imports))},
                                          {"exports", Json(std::move(exports))}});
    }
    return Json::Object{{"schema_version", 1},
                        {"entry", compilation.modules[compilation.entry].file},
                        {"modules", Json(std::move(modules))}};
}
} // namespace wood
