#include "error_explanations.hpp"
#include "wood/compiler.hpp"
#include "wood/config.hpp"
#include "wood/language_info.hpp"
#include "wood/process.hpp"
#include "wood/project.hpp"
#include "wood/runtime_source.hpp"
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using wood::path_from;
using wood::path_text;
using wood::publish;
using wood::read_file;
using wood::same_path;
using wood::TemporaryDirectory;
using wood::write_file;
struct Options {
    std::string command = "run";
    std::string input, output, cxx;
    std::vector<std::string> program_args;
    std::vector<std::string> libraries;
    bool json = false;
    bool optimize = true;
    bool locked = false;
    bool report_json = false;
    bool sanitize = false;
    wood::ProcessLimits limits;
};
void help() {
    std::cout << "wood " WOOD_VERSION " - a small native programming language\n\n"
                 "Usage: wood [run|check|build|emit-cpp] script.wd [options] [-- arguments...]\n"
                 "       wood script.wd\n\n"
                 "  init [PATH]        Create a project; default is the empty current directory\n"
                 "  explain CODE       Explain a documented error code; no project required\n"
                 "  lock [PROJECT]     Snapshot exact local dependency content\n"
                 "  deps [PROJECT]     Print resolved dependency inventory as JSON\n"
                 "  inspect [INPUT]    Print typed modules and public signatures as JSON\n"
                 "  test [PROJECT]     Build and run test_*.wd scripts\n"
                 "  --locked           Require and verify wood.lock\n"
                 "  --report=json      Structured test summary on stdout\n"
                 "  --sanitize=address Instrument generated native programs with ASan\n"
                 "  --timeout-ms N     Per-child deadline; 0 disables it (default)\n"
                 "  --max-output-bytes N  Captured compiler/test output cap (1..16777216)\n"
                 "  check              Check syntax and types without a C++ toolchain\n"
                 "  build              Compile a standalone native executable\n"
                 "  emit-cpp           Write a self-contained C++23 translation unit\n"
                 "  -o PATH            Output path (build and emit-cpp)\n"
                 "  --cxx PATH         Native C++ compiler executable (no embedded flags)\n"
                 "  --link-library PATH  Link an explicit native static library; repeatable\n"
                 "  -O0, -O2           Native optimization level (default: -O2)\n"
                 "  --diagnostics=json Emit frontend, driver, and runtime JSON diagnostics\n"
                 "  --language-info    Print machine-readable language capabilities\n"
                 "  --version          Print version\n"
                 "  --help             Show this help\n\n"
                 "Native compilation requires GCC, Clang, or MSVC. With MSVC, use a\n"
                 "Developer PowerShell/Command Prompt with INCLUDE and LIB configured.\n";
}
Options options(const std::vector<std::string>& args) {
    Options result;
    std::size_t i = 0;
    if (!args.empty() && (args[0] == "run" || args[0] == "check" || args[0] == "build" ||
                          args[0] == "emit-cpp" || args[0] == "init" || args[0] == "lock" ||
                          args[0] == "deps" || args[0] == "inspect" || args[0] == "test"))
        result.command = args[i++];
    for (; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg == "--") {
            if (result.command != "run")
                throw std::runtime_error("program arguments are only valid with run");
            result.program_args.assign(args.begin() + static_cast<std::ptrdiff_t>(i + 1),
                                       args.end());
            break;
        } else if (arg == "--timeout-ms" || arg == "--max-output-bytes") {
            if (++i == args.size())
                throw std::runtime_error("missing value for '" + arg + "'");
            std::uint64_t number = 0;
            const auto& text = args[i];
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            const auto maximum = arg == "--timeout-ms" ? 86400000ULL : 16777216ULL;
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                text.empty() || number > maximum || (arg == "--max-output-bytes" && number == 0))
                throw std::runtime_error("invalid value for '" + arg + "'");
            if (arg == "--timeout-ms")
                result.limits.timeout = std::chrono::milliseconds(number);
            else
                result.limits.output_bytes = static_cast<std::size_t>(number);
        } else if (arg == "--link-library") {
            if (++i == args.size())
                throw std::runtime_error("missing value for '--link-library'");
            const auto path = wood::resolve_path(path_from(args[i]));
            auto extension = path_text(path.extension());
            for (char& byte : extension)
                if (byte >= 'A' && byte <= 'Z')
                    byte = static_cast<char>(byte + ('a' - 'A'));
            if (!fs::is_regular_file(path) || (extension != ".a" && extension != ".lib"))
                throw std::runtime_error(
                    "--link-library requires an existing .a or .lib static library");
            result.libraries.push_back(path_text(path));
        } else if (arg == "-o" || arg == "--cxx") {
            if (++i == args.size())
                throw std::runtime_error("missing value for '" + arg + "'");
            (arg == "-o" ? result.output : result.cxx) = args[i];
        } else if (arg == "--diagnostics=json")
            result.json = true;
        else if (arg == "--locked")
            result.locked = true;
        else if (arg == "--report=json")
            result.report_json = true;
        else if (arg == "--sanitize=address")
            result.sanitize = true;
        else if (arg == "-O0")
            result.optimize = false;
        else if (arg == "-O2")
            result.optimize = true;
        else if (!arg.empty() && arg[0] == '-')
            throw std::runtime_error("unknown option '" + arg + "'");
        else if (arg.empty() && result.command == "init")
            throw std::runtime_error(
                "init directory must not be empty; use '.' for the current directory");
        else if (!result.input.empty())
            throw std::runtime_error(result.command == "init" ? "init accepts at most one directory"
                                                              : "expected exactly one source file");
        else
            result.input = arg;
    }
    if (result.report_json && result.command != "test")
        throw std::runtime_error("--report=json is only valid with test");
    if (!result.libraries.empty() && result.command != "run" && result.command != "build" &&
        result.command != "test")
        throw std::runtime_error("--link-library is only valid with run, build, or test");
    if (!result.output.empty() && result.command != "build" && result.command != "emit-cpp")
        throw std::runtime_error("-o is only valid with build or emit-cpp");
    return result;
}
std::string compiler(const Options& opts) {
    if (!opts.cxx.empty())
        return opts.cxx;
#ifdef _WIN32
    const auto count = GetEnvironmentVariableW(L"WOOD_CXX", nullptr, 0);
    if (count > 1) {
        std::wstring value(count, L'\0');
        const auto written = GetEnvironmentVariableW(L"WOOD_CXX", value.data(), count);
        if (written > 0 && written < count) {
            value.resize(written);
            return path_text(fs::path(value));
        }
    }
#else
    if (const auto* env = std::getenv("WOOD_CXX"); env && *env)
        return env;
#endif
    if (fs::exists(path_from(wood::configured_cxx)))
        return wood::configured_cxx;
#ifdef _WIN32
    return "cl.exe";
#else
    return "c++";
#endif
}
int explain(const std::vector<std::string>& arguments) {
    constexpr auto usage = "Usage: wood explain CODE [--diagnostics=json]";
    if (arguments.size() == 2 && arguments[1] == "--help") {
        std::cout << usage << '\n' << wood::detail::available_explanations() << '\n';
        return 0;
    }
    std::string_view code;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--diagnostics=json")
            continue;
        if (argument.starts_with('-'))
            throw std::runtime_error("unsupported option for explain '" + argument + "'. " + usage);
        if (!code.empty())
            throw std::runtime_error("explain requires exactly one error code. " +
                                     std::string(usage));
        if (argument.empty())
            throw std::runtime_error("explain requires a nonempty error code, for example E3030");
        code = argument;
    }
    if (code.empty())
        throw std::runtime_error("explain requires an error code. " + std::string(usage));
    if (code.size() != 5 || (code[0] != 'E' && code[0] != 'D' && code[0] != 'R') ||
        code.substr(1).find_first_not_of("0123456789") != std::string_view::npos)
        throw std::runtime_error(
            "expected an uppercase code such as E3030 (E, D or R and four digits)");
    const auto* entry = wood::detail::find_error_explanation(code);
    if (!entry)
        throw std::runtime_error("No built-in explanation is available for '" + std::string(code) +
                                 "'. This does not mean the code is invalid.\n" +
                                 wood::detail::available_explanations() +
                                 "\nSee docs/cli.md for diagnostic categories.");
    std::cout << wood::detail::render_error_explanation(*entry);
    return 0;
}
int driver(const std::vector<std::string>& arguments, const fs::path& execution_log = {}) {
    if (!arguments.empty() && arguments[0] == "explain")
        return explain(arguments);
    if (!arguments.empty() && (arguments[0] == "install" || arguments[0] == "lsp" || arguments[0] == "fmt" || arguments[0] == "package" || arguments[0] == "add" ||
                                arguments[0] == "remove" || arguments[0] == "update" ||
                                arguments[0] == "list" || arguments[0] == "info"))
        throw std::runtime_error("'wood " + arguments[0] +
                                 "' is provided by the Wood-written native CLI, not this C++ reference driver");
    if ((arguments.empty() && wood::discover_project(fs::current_path()).empty()) ||
        (arguments.size() == 1 && arguments[0] == "--help")) {
        help();
        return 0;
    }
    if (arguments.size() == 1 && arguments[0] == "--version") {
        std::cout << "wood " WOOD_VERSION << '\n';
        return 0;
    }
    if (arguments.size() == 1 && arguments[0] == "--language-info") {
        std::cout << wood::language_info << '\n';
        return 0;
    }
    const auto opts = options(arguments);
    if (opts.command == "init") {
        const auto target = opts.input.empty() ? fs::current_path() : path_from(opts.input);
        wood::init_project(target);
        std::cout << "Created wood project in "
                  << path_text(fs::absolute(target).lexically_normal())
                  << "\n\nFrom the project directory:\n  wood run\n  wood build\n  wood test\n";
        return 0;
    }
    auto input = fs::absolute(opts.input.empty() ? fs::current_path() : path_from(opts.input));
    const bool script = !opts.input.empty() && input.extension() == ".wd";
    fs::path manifest = script                    ? wood::discover_project(input)
                        : fs::is_directory(input) ? wood::discover_project(input)
                                                  : input;
    std::optional<wood::Project> project;
    if (!manifest.empty() && manifest.filename() == "wood.json")
        project = wood::load_project(manifest);
    else if (!script)
        throw std::runtime_error(
            "no wood.json project found; provide a .wd file or run 'wood init PATH'");
    if (opts.command == "lock" || opts.command == "deps") {
        if (!project)
            throw std::runtime_error("command requires a wood.json project");
        if (opts.command == "lock" && opts.locked)
            throw std::runtime_error("lock refresh cannot be combined with --locked");
        if (opts.command == "deps" && opts.locked)
            project->verify_lock(true, WOOD_VERSION);
        const auto inventory = project->lock_document(WOOD_VERSION).dump() + "\n";
        if (inventory.size() > 16 * 1024 * 1024)
            throw std::runtime_error("lock document exceeds 16 MiB");
        if (opts.command == "lock") {
            // Refreshing content keeps deliberately recorded GitHub commit pins.
            const auto lock_file = project->packages[0].root / "wood.lock";
            std::string refreshed = inventory;
            if (std::filesystem::exists(lock_file)) {
                const auto existing = wood::Json::parse(wood::read_file(lock_file, 16 * 1024 * 1024));
                auto fields = project->lock_document(WOOD_VERSION).object();
                for (const auto* key : {"remote", "python"})
                    if (existing.contains(key)) fields.emplace(key, existing.at(key));
                refreshed = wood::Json(std::move(fields)).dump() + "\n";
            }
            wood::write_atomic(lock_file, refreshed);
            std::cout << "Updated wood.lock\n";
        } else
            std::cout << inventory;
        return 0;
    }
    if (project)
        project->verify_lock(opts.locked, WOOD_VERSION);
    else if (opts.locked)
        throw std::runtime_error("--locked requires a project");
    if (opts.command == "test") {
        const auto tests = script ? std::vector<fs::path>{input} : project->tests();
        if (tests.empty())
            throw std::runtime_error("no test_*.wd files found in the declared tests directory");
        wood::Json::Array results;
        TemporaryDirectory logs;
        int failed = 0;
        for (std::size_t i = 0; i < tests.size(); ++i) {
            const auto log = logs.path / (std::to_string(i) + ".log");
            std::vector<std::string> command{"run", path_text(tests[i]),
                                             opts.optimize ? "-O2" : "-O0"};
            command.insert(command.end(),
                           {"--timeout-ms", std::to_string(opts.limits.timeout.count()),
                            "--max-output-bytes", std::to_string(opts.limits.output_bytes)});
            if (opts.json)
                command.push_back("--diagnostics=json");
            if (opts.locked)
                command.push_back("--locked");
            if (opts.sanitize)
                command.push_back("--sanitize=address");
            if (!opts.cxx.empty()) {
                command.push_back("--cxx");
                command.push_back(opts.cxx);
            }
            for (const auto& library : opts.libraries) {
                command.push_back("--link-library");
                command.push_back(library);
            }
            int status = 2;
            std::string message;
            std::string driver_code;
            try {
                status = driver(command, log);
            } catch (const std::exception& error) {
                message = error.what();
                if (const auto* failure = dynamic_cast<const wood::ProcessFailure*>(&error))
                    driver_code = failure->code;
            }
            auto output = fs::exists(log) ? read_file(log) : std::string{};
            if (!message.empty())
                output += "\n" + message;
            if (status != 0)
                ++failed;
            results.emplace_back(wood::Json::Object{{"file", path_text(tests[i])},
                                                    {"exit_code", status},
                                                    {"passed", status == 0},
                                                    {"driver_error", driver_code},
                                                    {"output", output}});
            if (!opts.report_json) {
                std::cout << (status == 0 ? "PASS " : "FAIL ") << path_text(tests[i]) << '\n';
                if (status != 0)
                    std::cout << output;
            }
        }
        if (opts.report_json)
            std::cout << wood::Json(
                             wood::Json::Object{
                                 {"schema_version", 1},
                                 {"passed", static_cast<std::int64_t>(tests.size()) - failed},
                                 {"failed", failed},
                                 {"tests", wood::Json(std::move(results))}})
                             .dump()
                      << '\n';
        else
            std::cout << tests.size() - static_cast<std::size_t>(failed) << " passed, " << failed
                      << " failed\n";
        return failed ? 1 : 0;
    }
    if (!script) {
        input = project->packages[0].entry;
        if (input.empty())
            throw std::runtime_error("project has no entry; declare entry or provide a .wd file");
    }
    const auto source = read_file(input);
    wood::Compilation compilation;
    std::string generated;
    try {
        compilation = wood::load_modules(input, project ? project->roots()
                                                        : std::vector<wood::PackageRoot>{});
        if (project)
            project->verify_compilation(compilation);
        wood::analyze(compilation);
        if (opts.command == "inspect") {
            std::cout << wood::inspect_compilation(compilation).dump() << '\n';
            return 0;
        }
        if (opts.command != "check")
            generated = wood::generate(compilation, opts.json);
    } catch (const wood::Diagnostic& error) {
        std::cerr << wood::render_diagnostic(
            error, error.file.empty() ? path_text(input) : error.file,
            error.file.empty() ? source : error.source_text, opts.json);
        return 1;
    }
    if (opts.command == "check")
        return 0;
    generated = std::string(wood::runtime_source) + "\n" + generated;
    fs::path output;
    if (opts.command == "build" || opts.command == "emit-cpp") {
        output = opts.output.empty() ? fs::current_path() / input.stem()
                                     : fs::absolute(path_from(opts.output));
        if (project && opts.output.empty()) {
            fs::create_directories(project->packages[0].root / "build");
            output = project->packages[0].root / "build" / project->packages[0].name;
        }
        if (opts.output.empty()) {
            if (opts.command == "emit-cpp")
                output += ".cpp";
#ifdef _WIN32
            else
                output += ".exe";
#endif
        }
        if (output.extension() == ".wd")
            throw std::runtime_error("output must not replace a Wood source file");
        for (const auto& module : compilation.modules)
            if (!module.path.empty() && same_path(module.path, output))
                throw std::runtime_error("output must not overwrite a source module");
        if (project)
            for (const auto& package : project->packages)
                if (same_path(package.root / "wood.json", output) ||
                    same_path(package.root / "wood.lock", output))
                    throw std::runtime_error("output must not overwrite project metadata");
        if (same_path(input, output))
            throw std::runtime_error("output must not overwrite the source file");
        if (!fs::is_directory(output.parent_path()))
            throw std::runtime_error("output directory does not exist");
    }
    TemporaryDirectory temp(output.empty() ? fs::temp_directory_path() : output.parent_path());
    const auto cpp = temp.path / "program.cpp";
    write_file(cpp, generated);
    if (opts.command == "emit-cpp") {
        publish(cpp, output);
        return 0;
    }
#ifdef _WIN32
    const auto executable = temp.path / "program.exe";
#else
    const auto executable = temp.path / "program";
#endif
    const auto cxx = compiler(opts);
    auto compiler_name = path_text(path_from(cxx).filename());
    for (char& c : compiler_name)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    const bool msvc = compiler_name == "cl" || compiler_name == "cl.exe" ||
                      compiler_name == "clang-cl" || compiler_name == "clang-cl.exe";
    std::vector<std::string> command{cxx};
    if (msvc) {
        // Wood function values compare declaration identity. ICF can merge
        // distinct wrappers into one address, violating that language contract.
        if (opts.sanitize)
            command.insert(command.end(), {"/fsanitize=address", "/Zi"});
        command.insert(command.end(),
                       {"/nologo", "/std:c++latest", "/EHsc", "/utf-8", "/permissive-", "/bigobj",
                        opts.optimize ? "/O2" : "/Od", "/Fe:" + path_text(executable),
                        "/Fo:" + path_text(temp.path / "program.obj"),
                        "/Fd:" + path_text(temp.path / "program.pdb"), path_text(cpp), "/link",
                        "/INCREMENTAL:NO", "/OPT:NOICF"});
    } else {
        if (opts.sanitize)
            command.insert(command.end(), {"-fsanitize=address", "-fno-omit-frame-pointer", "-g"});
        command.insert(command.end(), {"-std=c++23", opts.optimize ? "-O2" : "-O0", "-o",
                                       path_text(executable), path_text(cpp)});
    }
    command.insert(command.end(), opts.libraries.begin(), opts.libraries.end());
    if (!msvc)
        command.push_back("-pthread");
#if !defined(_WIN32) && !defined(__APPLE__)
    command.push_back("-ldl");
#endif
#ifdef _WIN32
    if (!msvc)
        command.push_back("-lws2_32");
#endif
    const auto log = temp.path / "compiler.log";
    const int status = wood::run_process(command, log, opts.limits);
    if (status != 0) {
        if (opts.json) {
            const wood::Diagnostic error("D5002", {0, 0, 0},
                                         "native compilation failed (exit " +
                                             std::to_string(status) + "): " + read_file(log));
            std::cerr << wood::render_diagnostic(error, opts.input, source, true);
            return 2;
        }
        std::cerr << read_file(log);
        std::cerr << "wood: native compilation failed (exit " << status
                  << "). Use --cxx to select a compiler";
        if (msvc)
            std::cerr << "; run from a Visual Studio developer shell";
        std::cerr << ".\n";
        return 2;
    }
    if (opts.command == "build") {
        publish(executable, output);
        return 0;
    }
    std::vector<std::string> execution{path_text(executable)};
    execution.insert(execution.end(), opts.program_args.begin(), opts.program_args.end());
    return wood::run_process(execution, execution_log, opts.limits);
}
int safe_driver(const std::vector<std::string>& args) {
    try {
        return driver(args);
    } catch (const std::exception& error) {
        bool json = false;
        for (const auto& arg : args) {
            if (arg == "--")
                break;
            if (arg == "--diagnostics=json")
                json = true;
        }
        if (json)
            std::cerr << wood::render_diagnostic(
                wood::Diagnostic(dynamic_cast<const wood::ProcessFailure*>(&error)
                                     ? static_cast<const wood::ProcessFailure&>(error).code
                                     : "D5001",
                                 {0, 0, 0}, error.what()),
                "", "", true);
        else
            std::cerr << "wood: " << error.what() << '\n';
        return 2;
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        const auto count =
            WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        if (count <= 0) {
            std::cerr << "wood: could not decode command line\n";
            return 2;
        }
        std::string arg(static_cast<std::size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, arg.data(), count, nullptr, nullptr);
        arg.pop_back();
        args.push_back(std::move(arg));
    }
    return safe_driver(args);
}
#else
int main(int argc, char** argv) {
    return safe_driver(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
