#pragma once
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace wood {
struct ProcessLimits {
    std::chrono::milliseconds timeout{0};        // zero leaves execution time unlimited
    std::size_t output_bytes = 16 * 1024 * 1024; // captured output only
    bool inherit_stdin = true; // compiler tools must not drain the program's input
};
struct ProcessFailure : std::runtime_error {
    std::string code;
    ProcessFailure(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(code)) {}
};
// Arguments are passed directly to the OS; never interpreted by a command shell.
int run_process(const std::vector<std::string>& arguments, const std::filesystem::path& log = {},
                ProcessLimits limits = {});
} // namespace wood
