#include "wood/project.hpp"
#include <iostream>

int run_tests() {
    int checks = 0, failed = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        if (!value) {
            ++failed;
            std::cerr << "FAIL " << name << '\n';
        }
    };
    auto rejects = [&](auto action, const char* name) {
        try {
            action();
            check(false, name);
        } catch (const std::exception&) {
            check(true, name);
        }
    };
    wood::TemporaryDirectory temp(wood::fs::current_path());
    rejects([&] { wood::init_project({}); }, "init rejects empty API path");
    std::string invalid_name = "invalid";
    invalid_name.push_back('\0');
    invalid_name += "project";
    rejects([&] { wood::init_project(temp.path / wood::path_from(invalid_name)); },
            "init rejects NUL before filesystem access");
    const auto app = temp.path / "app", lib = temp.path / "lib";
    wood::fs::create_directories(app / "src");
    wood::fs::create_directories(lib / "src");
    wood::write_file(
        app / "wood.json",
        R"({"format":1,"name":"app","version":"1.0.0","source":"src","entry":"main.wd","dependencies":{"lib":{"path":"../lib","version":"1.0.0"}}})");
    wood::write_file(lib / "wood.json",
                     R"({"format":1,"name":"lib","version":"1.0.0","source":"src"})");
    wood::write_file(app / "src/main.wd", "from lib.value import value\nassert(value() == 1)\n");
    wood::write_file(lib / "src/value.wd", "fn value() -> int:\n    return 1\n");
    auto project = wood::load_project(app / "wood.json");
    check(project.packages.size() == 2, "package graph");
    wood::write_atomic(app / "wood.lock", project.lock_document("0.3.0").dump());
    project.verify_lock(true, "0.3.0");
    auto compilation = wood::load_modules(project.packages[0].entry, project.roots());
    project.verify_compilation(compilation);
    wood::analyze(compilation);
    check(compilation.modules.size() == 2, "module graph");
    // Verify bytes actually compiled, even if a dependency changes after the
    // inventory was checked. No timing-dependent concurrent test is needed.
    wood::write_file(lib / "src/value.wd", "fn value() -> int:\n    return 2\n");
    auto changed = wood::load_modules(project.packages[0].entry, project.roots());
    rejects([&] { project.verify_compilation(changed); }, "post-verification source change");
    rejects([&] { project.verify_lock(true, "0.3.0"); }, "lock source mismatch");
    wood::write_file(lib / "src/value.wd", "fn value() -> int:\n    return 1\n");
    project.verify_lock(true, "0.3.0");
    wood::write_file(app / "src/main.wd", "from lib.value import value\nprint(value())\n");
    project.verify_lock(true, "0.3.0");
    check(true, "editable root sources");
    rejects([&] { project.verify_lock(true, "0.4.0"); }, "compiler lock mismatch");
    wood::write_file(lib / "src/added.wd", "fn extra():\n    pass\n");
    rejects([&] { project.verify_lock(true, "0.3.0"); }, "added dependency source");
    wood::fs::remove(lib / "src/added.wd");
    const auto outside = temp.path / "outside";
    wood::fs::create_directory(outside);
    wood::write_file(outside / "secret.wd", "fn secret():\n    pass\n");
    std::error_code link_error;
    wood::fs::create_directory_symlink(outside, app / "src/link", link_error);
    if (!link_error) {
        wood::write_file(app / "src/main.wd", "import link.secret\n");
        rejects([&] { (void)wood::load_modules(project.packages[0].entry, project.roots()); },
                "symlink source escape");
        check(wood::contained(app / "src", app), "contained directory");
        check(!wood::contained(app / "src/link/secret.wd", app), "resolved containment");
    } else
        std::cout << "Symlink tests unavailable: " << link_error.message() << '\n';
    wood::write_file(app / "src/a.wd", "record Item:\n    value: int\n");
    wood::write_file(app / "src/b.wd", "record Item:\n    value: int\n");
    wood::write_file(app / "src/main.wd",
                     "import a\nimport b\nfn first(x: list[a.Item]) -> a.Item:\n    return x[0]\n"
                     "fn second(x: dict[str,b.Item]) -> b.Item:\n    return x['key']\n");
    auto nominal = wood::load_modules(app / "src/main.wd", project.roots());
    wood::analyze(nominal);
    const auto inspection = wood::inspect_compilation(nominal);
    const auto& exports = inspection.at("modules").array()[0].at("exports").array();
    check(exports[0].at("returns").string() == exports[1].at("returns").string(),
          "inspection retains compatible display names");
    check(exports[0].at("returns_ref").at("module").string() !=
              exports[1].at("returns_ref").at("module").string(),
          "inspection distinguishes nominal modules");
    check(exports[0].at("parameters").array()[0].at("type_ref").at("element") ==
              exports[0].at("returns_ref"),
          "nested list nominal identity");
    check(exports[1].at("parameters").array()[0].at("type_ref").at("value") ==
              exports[1].at("returns_ref"),
          "nested dictionary nominal identity");
    wood::write_file(app / "src/main.wd", "import a\nfn apply(callback: fn(a.Item) -> a.Item, "
                                          "item: a.Item) -> a.Item:\n    return callback(item)\n"
                                          "extern fn native_value(x: int) -> float\n");
    auto callbacks = wood::load_modules(app / "src/main.wd", project.roots());
    wood::analyze(callbacks);
    const auto callback_report = wood::inspect_compilation(callbacks);
    const auto& functions = callback_report.at("modules").array()[0].at("exports").array();
    const auto& callback_type = functions[0].at("parameters").array()[0].at("type_ref");
    check(callback_type.at("kind").string() == "function" &&
              callback_type.at("parameters").array()[0] == functions[0].at("returns_ref") &&
              callback_type.at("returns") == functions[0].at("returns_ref"),
          "callback inspection preserves nominal identity");
    check(functions[1].at("external").boolean(), "inspection identifies external ABI declarations");
    wood::write_file(app / "src/main.wd",
                     "fn identity(value: stream) -> stream:\n    return value\n");
    auto streams = wood::load_modules(app / "src/main.wd", project.roots());
    wood::analyze(streams);
    const auto stream_report = wood::inspect_compilation(streams);
    const auto& stream_function = stream_report.at("modules").array()[0].at("exports").array()[0];
    check(stream_function.at("returns_ref").at("kind").string() == "stream" &&
              stream_function.at("parameters").array()[0].at("type_ref") ==
                  stream_function.at("returns_ref"),
          "inspection exposes native resource type independently of nominal records");
    wood::write_file(app / "src/a.wd", "extern fn conflicting(x: int) -> int\n");
    wood::write_file(app / "src/b.wd", "extern fn conflicting(x: float) -> int\n");
    wood::write_file(app / "src/main.wd", "import a\nimport b\n");
    try {
        auto conflict = wood::load_modules(app / "src/main.wd", project.roots());
        wood::analyze(conflict);
        check(false, "conflicting external declarations accepted");
    } catch (const wood::Diagnostic& error) {
        check(error.code == "E3023", "external ABI conflict diagnosed before native compilation");
    }
    std::cout << checks << " project checks, " << failed << " failures\n";
    return failed ? 1 : 0;
}
int main() {
    try {
        return run_tests();
    } catch (const std::exception& error) {
        std::cerr << "Project test exception: " << error.what() << '\n';
        return 1;
    }
}
