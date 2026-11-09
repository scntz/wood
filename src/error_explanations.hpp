#pragma once
#include <string>
#include <string_view>

// CLI help content, not part of the public diagnostic API or generated runtime.
namespace wood::detail {
struct ErrorExplanation {
    std::string_view code, title, meaning, cause, fix, before, after;
};
inline constexpr ErrorExplanation error_explanations[] = {
    {"E3002", "Type mismatch or invalid operand",
     "An expression does not have the type required at this location, or an\n"
     "operation does not support its operand types.",
     "Variables keep their types after initialization. Wood does not implicitly\n"
     "convert strings to numbers or numbers to bool. Container element types\n"
     "must agree; int-to-float widening does not convert list[int] to list[float].",
     "Read the expected and actual types in the diagnostic. Supply a compatible\n"
     "value, choose the intended annotation, or convert explicitly when appropriate.\n"
     "Text conversion with int/float can fail at runtime if the text is invalid.\n"
     "For conditions, use a bool expression such as count != 0.",
     "count: int = \"3\"\n", "count: int = int(\"3\")\nassert(count == 3)\n"},
    {"E3003", "Incorrect function arguments",
     "A call's arguments do not match the callable's parameter rules.",
     "A required argument may be missing, too many positional arguments may be\n"
     "supplied, or a keyword may be unknown, duplicated or positional-only.\n"
     "Some callable forms accept positional arguments only.",
     "Follow the expected signature or count in the diagnostic. Supply required\n"
     "arguments and remove extras or duplicates. Parameters before / must be\n"
     "positional; parameters after * must use keywords. Only parameters with\n"
     "defaults can be omitted.",
     "fn greet(name: str) -> str:\n    return \"hello, \" + name\nprint(greet())\n",
     "fn greet(name: str) -> str:\n    return \"hello, \" + name\n"
     "assert(greet(\"wood\") == \"hello, wood\")\n"},
    {"E3005", "Unknown function",
     "The compiler cannot resolve this function or constructor name at the call site.",
     "The name may be misspelled, not imported, or missing its module alias.\n"
     "Wood resolves calls statically; it does not search for or import a function\n"
     "automatically.",
     "Check the spelling and any suggested name. Declare the callable in this file\n"
     "or import it; file-scope functions can be called before their definitions.\n"
     "After import helpers as tools, use tools.function_name(...).\n"
     "A suggestion is advice, not an automatic correction.",
     "fn greet(name: str) -> str:\n    return \"hello, \" + name\ngret(\"wood\")\n",
     "fn greet(name: str) -> str:\n    return \"hello, \" + name\n"
     "assert(greet(\"wood\") == \"hello, wood\")\n"},
    {"E3007", "Undefined variable",
     "This name is not available as a value in the current lexical scope.",
     "A variable may be misspelled, used before its first assignment, or declared\n"
     "only inside a branch or loop. File-scope functions do not capture script\n"
     "variables; local closures have separate, documented capture rules.",
     "Check spelling and scope. Initialize shared values in the enclosing block\n"
     "before a branch, and pass needed values as function parameters. Do not rely\n"
     "on a branch or loop binding being available after that block.",
     "if true:\n    total = 1\nprint(total)\n",
     "total = 0\nif true:\n    total = 1\nassert(total == 1)\n"},
    {"E3013", "Missing return path",
     "A function that produces a value can reach the end of its body without one.",
     "A branch may fall through, or the only return may be inside a loop.\n"
     "Return analysis conservatively allows loops to finish, including while true.\n"
     "Wood does not insert an implicit None result.",
     "Return a compatible value or raise on every possible path. A direct exit\n"
     "also terminates a path. If absence is intentional, use T | None and return\n"
     "None explicitly. An annotation alone does not add a missing return.",
     "fn absolute(value: int) -> int:\n    if value < 0:\n        return -value\n",
     "fn absolute(value: int) -> int:\n    if value < 0:\n        return -value\n"
     "    return value\nassert(absolute(-3) == 3 and absolute(3) == 3)\n"},
    {"E3014", "Empty collection needs a type",
     "The compiler has no concrete element type for [] or key/value types for {}.",
     "An empty literal contains no values from which to infer its types, and no\n"
     "suitable annotation or call context supplies them. A later append does not\n"
     "retroactively type an earlier unannotated empty assignment.",
     "Provide an annotation such as list[int] or dict[str, int]. A concrete\n"
     "parameter or return annotation can also supply context for an empty literal.",
     "values = []\n", "values: list[int] = []\nappend(values, 1)\nassert(values == [1])\n"},
    {"E3030", "Return type cannot be inferred safely",
     "The compiler cannot establish one compatible result type for an unannotated\n"
     "function. Most nonrecursive functions do not need a return annotation.",
     "Return inference may depend recursively on itself, directly or through\n"
     "other functions. Returns may also have incompatible types, or mix a bare\n"
     "return with value returns. Separate closure expressions have distinct types.",
     "For a recursive dependency, annotate at least one function in the cycle.\n"
     "For inconsistent returns, make the returned values compatible; an annotation\n"
     "does not make int and str interchangeable. For an absent value, use T | None\n"
     "and return None, not a bare return. Compatible closures can share an explicit\n"
     "callable(...) -> R result. All value-returning paths must still return, raise\n"
     "or directly exit (see E3013). The example fixes recursion, not every cause.",
     "fn countdown(n: int):\n    if n == 0:\n        return 0\n    return countdown(n - 1)\n",
     "fn countdown(n: int) -> int:\n    if n == 0:\n        return 0\n"
     "    return countdown(n - 1)\nassert(countdown(3) == 0)\n"}};

inline const ErrorExplanation* find_error_explanation(std::string_view code) {
    for (const auto& entry : error_explanations)
        if (entry.code == code)
            return &entry;
    return nullptr;
}
inline std::string available_explanations() {
    std::string result = "Available explanations: ";
    for (const auto& entry : error_explanations) {
        if (entry.code != error_explanations[0].code)
            result += ", ";
        result += entry.code;
    }
    return result;
}
inline std::string render_error_explanation(const ErrorExplanation& entry) {
    return std::string(entry.code) + " - " + std::string(entry.title) + "\n\nMeaning:\n" +
           std::string(entry.meaning) + "\n\nWhy it happens:\n" + std::string(entry.cause) +
           "\n\nHow to fix:\n" + std::string(entry.fix) + "\n\nExample that fails:\n```wood\n" +
           std::string(entry.before) + "```\n\nOne correction:\n```wood\n" +
           std::string(entry.after) + "```\n";
}
} // namespace wood::detail
