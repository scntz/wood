#include "wood/process.hpp"
#include "wood/support.hpp"
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

int run(const std::vector<std::string>& args) {
    using namespace std::chrono_literals;
    if (args.size() > 1) {
        if (args[1] == "--stdin-eof")
            return std::cin.peek() == std::char_traits<char>::eof() ? 0 : 19;
        if (args[1] == "--echo") {
            for (std::size_t i = 2; i < args.size(); ++i)
                std::cout << args[i].size() << ':' << args[i] << '\n';
            std::cout.flush();
            std::cerr << "stderr\n";
            return 17;
        }
        if (args[1] == "--flood") {
            for (int i = 0; i < 128; ++i)
                std::cout << std::string(8192, 'x') << std::flush;
            return 0;
        }
        if (args[1] == "--sleep") {
            std::this_thread::sleep_for(5s);
            return 0;
        }
#ifndef _WIN32
        if (args[1] == "--cancel") {
            kill(getppid(), SIGINT);
            std::this_thread::sleep_for(5s);
            return 0;
        }
#endif
        if (args[1] == "--marker") {
            std::this_thread::sleep_for(1s);
            wood::write_file(wood::path_from(args[2]), "escaped");
            return 0;
        }
        if (args[1] == "--tree") {
#ifdef _WIN32
            // Nested jobs still belong to their ancestor job.
            std::cout << "spawned\n" << std::flush;
            return wood::run_process({args[0], "--marker", args[2]});
#else
            const auto child = fork();
            if (child < 0)
                return 2;
            if (child == 0) {
                execl(args[0].c_str(), args[0].c_str(), "--marker", args[2].c_str(),
                      static_cast<char*>(nullptr));
                _exit(127);
            }
            std::cout << "spawned\n" << std::flush;
            std::this_thread::sleep_for(5s);
            return 0;
#endif
        }
        return 2;
    }
    wood::TemporaryDirectory temp(wood::fs::current_path());
    const auto log = temp.path / "process.log";
    int checks = 0;
    auto check = [&](bool value, const char* name) {
        ++checks;
        if (!value)
            throw std::runtime_error(name);
    };
    auto fails = [&](const std::vector<std::string>& command, wood::ProcessLimits limits,
                     const char* code) {
        try {
            (void)wood::run_process(command, log, limits);
        } catch (const wood::ProcessFailure& error) {
            check(error.code == code, "process failure code");
            return;
        }
        throw std::runtime_error("expected process failure");
    };
    check(wood::run_process({args[0], "--echo", "", "two words", "quote\"\\", "árbol & %!"}, log) ==
              17,
          "real child exit status");
    const auto actual = wood::read_file(log);
    check(actual == "0:\n9:two words\n7:quote\"\\\n11:árbol & %!\nstderr\n",
          ("exact quoting, Unicode, empty arguments and combined output: " + actual).c_str());
    check(wood::run_process({args[0], "--echo", "-d", "plain", "amp&%", "trailing\\"}, log) == 17,
          "literal argument child status");
    check(wood::read_file(log) == "2:-d\n5:plain\n5:amp&%\n9:trailing\\\nstderr\n",
          "plain options, metacharacters and trailing backslash stay literal");
    fails({args[0], "--flood"}, {0ms, 1024}, "D5004");
    check(wood::read_file(log) == std::string(1024, 'x'), "hard captured output limit");
    check(wood::run_process({args[0], "--stdin-eof"}, log,
                           {0ms, 1024, false}) == 0,
          "explicit null stdin for compiler tools");
    fails({args[0], "--sleep"}, {100ms}, "D5003");
#ifndef _WIN32
    fails({args[0], "--cancel"}, {2s}, "D5005");
#endif
    const auto marker = temp.path / "escaped.txt";
    fails({args[0], "--tree", wood::path_text(marker)}, {500ms}, "D5003");
    check(wood::read_file(log).find("spawned") != std::string::npos, "descendant was launched");
    std::this_thread::sleep_for(1100ms);
    check(!wood::fs::exists(marker), "timeout terminates descendants");
    bool rejected_nul = false;
    try {
        wood::run_process({args[0], std::string("a\0b", 3)}, log);
    } catch (const std::runtime_error&) {
        rejected_nul = true;
    }
    check(rejected_nul, "NUL process arguments are rejected");
    check(wood::run_process({args[0], "--echo"}, log, {0ms, 7}) == 17,
          "output exactly at limit is accepted");
    std::cout << checks << " process checks, 0 failures\n";
    return 0;
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) {
        const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr,
                                              0, nullptr, nullptr);
        if (!size)
            return 1;
        std::string text(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), size, nullptr,
                            nullptr);
        text.pop_back();
        args.push_back(std::move(text));
    }
#else
int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);
#endif
    try {
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
