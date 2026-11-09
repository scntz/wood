#include "error_explanations.hpp"
#include "wood/compiler.hpp"
#include "wood/language_info.hpp"
#include "wood/support.hpp"
#include <iostream>
#include <set>

namespace {
int checks = 0, failures = 0;
void check(bool condition, const std::string& description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << description << '\n';
    }
}
void accepts(std::string_view source) {
    try {
        check(!wood::compile(source, "explanation.wd").empty(), "correction compiles");
    } catch (const std::exception& error) {
        check(false, std::string("invalid correction: ") + error.what());
    }
}
void rejects(std::string_view source, std::string_view code) {
    try {
        (void)wood::compile(source, "explanation.wd");
        check(false, "failing example was accepted");
    } catch (const wood::Diagnostic& error) {
        check(error.code == code, std::string("example produces ") + std::string(code));
        const auto diagnostic =
            wood::Json::parse(wood::render_diagnostic(error, "explanation.wd", source, true));
        check(diagnostic.object().size() == 6 && diagnostic.at("code").string() == code &&
                  diagnostic.at("severity").string() == "error" &&
                  diagnostic.at("file").string() == "explanation.wd" &&
                  diagnostic.at("line").integer() > 0 && diagnostic.at("column").integer() > 0,
              "existing six-field diagnostic API retained");
    } catch (const std::exception& error) {
        check(false, std::string("unexpected explanation exception: ") + error.what());
    }
}
} // namespace
int main() {
    std::set<std::string_view> codes;
    for (const auto& entry : wood::detail::error_explanations) {
        check(codes.insert(entry.code).second, "unique explanation code");
        check(wood::detail::find_error_explanation(entry.code) == &entry, "exact catalog lookup");
        check(!entry.meaning.empty() && !entry.cause.empty() && !entry.fix.empty() &&
                  !entry.title.empty() && !entry.before.empty() && !entry.after.empty(),
              "complete explanation");
        const auto text = wood::detail::render_error_explanation(entry);
        check(text.starts_with(std::string(entry.code) + " - ") &&
                  text.find("\n\nMeaning:\n") != std::string::npos &&
                  text.find("\n\nWhy it happens:\n") != std::string::npos &&
                  text.find("\n\nHow to fix:\n") != std::string::npos &&
                  text.find("Example that fails:\n```wood\n" + std::string(entry.before) + "```") !=
                      std::string::npos &&
                  text.ends_with("One correction:\n```wood\n" + std::string(entry.after) + "```\n"),
              "readable explanation contains exact tested examples");
        rejects(entry.before, entry.code);
        accepts(entry.after);
    }
    for (const auto code : {"E3031", "E9999", "R4001", "D5001", "e3030", "", "E3030extra"})
        check(!wood::detail::find_error_explanation(code), "no fabricated or prefix explanation");
    const auto metadata = wood::Json::parse(wood::language_info);
    const auto& documented = metadata.at("diagnostics").at("explain").at("codes").array();
    std::set<std::string_view> reported;
    for (const auto& code : documented)
        reported.insert(code.string());
    check(reported == codes && documented.size() == codes.size(),
          "metadata matches embedded catalog");

    // E3030 describes several real semantic failures, not only direct recursion.
    rejects("fn choose(flag: bool):\n    if flag:\n        return 1\n    return 'one'\n", "E3030");
    accepts("fn choose(flag: bool) -> str:\n    if flag:\n        return '1'\n    return 'one'\n");
    rejects("fn lookup(found: bool):\n    if found:\n        return\n    return 7\n", "E3030");
    accepts("fn lookup(found: bool) -> int | None:\n    if found:\n        return None\n    return "
            "7\n");
    rejects("fn first(n: int):\n    if n == 0:\n        return 0\n    return second(n - 1)\n"
            "fn second(n: int):\n    return first(n)\n",
            "E3030");
    accepts("fn first(n: int) -> int:\n    if n == 0:\n        return 0\n    return second(n - 1)\n"
            "fn second(n: int):\n    return first(n)\n");
    rejects("fn choose(flag: bool):\n    if flag:\n        return fn(x: int) => x\n"
            "    return fn(x: int) => x + 1\n",
            "E3030");
    accepts("fn choose(flag: bool) -> callable(int) -> int:\n    if flag:\n"
            "        return fn(x: int) => x\n    return fn(x: int) => x + 1\n");
    accepts("fn choose(flag: bool):\n    if flag:\n        return 1\n    return 2.5\n");
    accepts("fn lookup(found: bool):\n    if found:\n        return 7\n    return None\n");
    rejects("values = {}\n", "E3014");
    accepts("values: dict[str, int] = {}\nvalues['wood'] = 1\n");
    // An annotation does not magically coerce incompatible values or add paths.
    rejects("fn choose(flag: bool) -> str:\n    if flag:\n        return 1\n    return 'one'\n",
            "E3002");
    rejects("fn missing(flag: bool) -> int:\n    if flag:\n        return 1\n", "E3013");
    std::cout << checks << " explanation checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
