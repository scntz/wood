// Real OS output failures. This launcher is a test tool, never linked into Wood.
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::string mode = argv[2];
    const int fd = mode.starts_with("stderr") ? 2 : 1;
    int result = -1;
#ifdef _WIN32
    HANDLE read = nullptr, write = INVALID_HANDLE_VALUE;
    if (mode.find("pipe") != std::string::npos) {
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        if (!CreatePipe(&read, &write, &security, 0)) return 3;
        CloseHandle(read); // A genuine broken pipe, with no reader handles.
    }
    const auto quote = [](const std::wstring& value) {
        std::wstring out = L"\"";
        unsigned slashes = 0;
        for (wchar_t c : value) {
            if (c == L'\\') { ++slashes; continue; }
            out.append(slashes * (c == L'"' ? 2 : 1), L'\\');
            slashes = 0;
            if (c == L'"') out += L'\\';
            out += c;
        }
        out.append(slashes * 2, L'\\');
        return out + L'"';
    };
    const auto binary = std::filesystem::path(argv[1]).wstring();
    std::wstring command = quote(binary) + L" " + quote(std::filesystem::path(argv[3]).wstring()) +
                           L" " + quote(std::filesystem::path(mode).wstring());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = fd == 1 ? write : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = fd == 2 ? write : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(binary.c_str(), command.data(), nullptr, nullptr, TRUE, 0,
                        nullptr, nullptr, &startup, &child)) return 4;
    if (write != INVALID_HANDLE_VALUE) CloseHandle(write);
    if (WaitForSingleObject(child.hProcess, 60000) != WAIT_OBJECT_0) {
        // Kill only this fixture's own child after its explicit test deadline.
        TerminateProcess(child.hProcess, 124);
        WaitForSingleObject(child.hProcess, INFINITE);
    }
    DWORD status = 0;
    if (GetExitCodeProcess(child.hProcess, &status)) result = static_cast<int>(status);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
#else
    const auto child = fork();
    if (child < 0) return 3;
    if (child == 0) {
        if (mode.find("pipe") != std::string::npos) {
            int pipefd[2];
            if (pipe(pipefd)) _exit(4);
            close(pipefd[0]);
            if (dup2(pipefd[1], fd) < 0) _exit(5);
            if (pipefd[1] != fd) close(pipefd[1]);
        } else if (mode.ends_with("full")) {
            const int full = open("/dev/full", O_WRONLY);
            if (full < 0 || dup2(full, fd) < 0) _exit(6);
            if (full != fd) close(full);
        } else if (mode.ends_with("closed")) close(fd);
        else {
            // Keep the descriptor allocated so scope-opening cannot reuse a
            // deliberately closed stdout/stderr number before the failing write.
            const int readonly = open("/dev/null", O_RDONLY);
            if (readonly < 0 || dup2(readonly, fd) < 0) _exit(6);
            if (readonly != fd) close(readonly);
        }
        if (mode.find("masked") != std::string::npos) {
            sigset_t blocked;
            sigemptyset(&blocked);
            sigaddset(&blocked, SIGPIPE);
            if (sigprocmask(SIG_BLOCK, &blocked, nullptr)) _exit(11);
            if (mode.find("pending") != std::string::npos && raise(SIGPIPE)) _exit(12);
        }
        execl(argv[1], argv[1], argv[3], argv[2], static_cast<char*>(nullptr));
        _exit(7);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return 8;
    if (WIFEXITED(status)) result = WEXITSTATUS(status);
#endif
    if (result != 0) {
        std::cerr << "Output child failed: " << mode << ": " << result << '\n';
        return 9;
    }
    std::ifstream marker(argv[3], std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(marker)), {});
    if (text != "caught and cleaned\n") {
        std::cerr << "Missing output cleanup evidence: " << mode << '\n';
        return 10;
    }
    std::cout << "checked output " << mode << " passed\n";
}
