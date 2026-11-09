#include "wood/process.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <system_error>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace wood {
namespace {
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
volatile LONG interrupted = 0;
BOOL WINAPI interrupt_handler(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
        return FALSE;
    InterlockedExchange(&interrupted, 1);
    return TRUE;
}
struct Interrupts {
    Interrupts() {
        InterlockedExchange(&interrupted, 0);
        if (!SetConsoleCtrlHandler(interrupt_handler, TRUE))
            throw std::runtime_error("cannot install child cancellation handler");
    }
    ~Interrupts() {
        SetConsoleCtrlHandler(interrupt_handler, FALSE);
    }
};
#else
volatile std::sig_atomic_t interrupted = 0;
void interrupt_handler(int) {
    interrupted = 1;
}
struct Interrupts {
    using Handler = void (*)(int);
    Handler previous_int, previous_term;
    Interrupts() {
        interrupted = 0;
        previous_int = std::signal(SIGINT, interrupt_handler);
        if (previous_int == SIG_ERR)
            throw std::runtime_error("cannot install child cancellation handler");
        previous_term = std::signal(SIGTERM, interrupt_handler);
        if (previous_term == SIG_ERR) {
            std::signal(SIGINT, previous_int);
            throw std::runtime_error("cannot install child cancellation handler");
        }
    }
    ~Interrupts() {
        std::signal(SIGINT, previous_int);
        std::signal(SIGTERM, previous_term);
    }
};
#endif
class Capture {
    std::ofstream output;
    std::size_t count = 0, limit;

  public:
    Capture(const std::filesystem::path& path, std::size_t maximum) : limit(maximum) {
        if (!path.empty()) {
            output.open(path, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("cannot create process log");
        }
    }
    void write(const char* bytes, std::size_t size) {
        const auto allowed = std::min(size, limit - count);
        output.write(bytes, static_cast<std::streamsize>(allowed));
        if (!output)
            throw std::runtime_error("cannot write process log");
        count += allowed;
        if (allowed != size)
            throw ProcessFailure("D5004", "captured process output exceeds " +
                                              std::to_string(limit) + " bytes");
    }
    void finish() {
        output.close();
        if (!output)
            throw std::runtime_error("cannot close process log");
    }
};
void deadline(Clock::time_point start, const ProcessLimits& limits) {
    if (interrupted)
        throw ProcessFailure("D5005", "process cancelled");
    if (limits.timeout.count() > 0 && Clock::now() - start >= limits.timeout)
        throw ProcessFailure("D5003", "process exceeded timeout of " +
                                          std::to_string(limits.timeout.count()) + " ms");
}
#ifdef _WIN32
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE value = nullptr) : value(value) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Child {
    Handle job{CreateJobObjectW(nullptr, nullptr)}, process;
    ~Child() {
        if (process.value) {
            TerminateJobObject(job.value, 2);
            WaitForSingleObject(process.value, INFINITE);
        }
    }
};
std::wstring widen(const std::string& value) {
    if (value.empty())
        return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0);
    if (!count)
        throw std::runtime_error("invalid UTF-8 process argument");
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        result.data(), count);
    return result;
}
std::wstring quote(const std::wstring& value) {
    // System tools such as wsl.exe parse their own command line. Unnecessary
    // quotes around option names can change their meaning; preserve simple
    // arguments literally, while retaining CRT escaping where quoting is needed.
    if (!value.empty() && value.find_first_of(L" \t\"") == std::wstring::npos)
        return value;
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}
#else
struct Descriptor {
    int value = -1;
    ~Descriptor() {
        if (value >= 0)
            close(value);
    }
};
struct Child {
    pid_t pid;
    bool reaped = false;
    ~Child() {
        // Kill the group before reaping the root so its ID cannot be reused.
        if (!reaped) {
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
            }
        }
    }
};
#endif
} // namespace
int run_process(const std::vector<std::string>& arguments, const std::filesystem::path& log,
                ProcessLimits limits) {
    if (arguments.empty() || arguments[0].empty())
        throw std::logic_error("empty process command");
    if (limits.timeout.count() < 0)
        throw std::logic_error("negative process timeout");
    for (const auto& argument : arguments)
        if (argument.find('\0') != std::string::npos)
            throw std::runtime_error("process argument contains a NUL byte");
    Capture capture(log, limits.output_bytes);
    Interrupts interrupts;
    const auto start = Clock::now();
#ifdef _WIN32
    std::wstring command;
    for (const auto& argument : arguments) {
        if (!command.empty())
            command += L' ';
        command += quote(widen(argument));
    }
    Child child;
    if (!child.job.value)
        throw std::runtime_error("cannot create child process job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(child.job.value, JobObjectExtendedLimitInformation, &info,
                                 sizeof(info)))
        throw std::runtime_error("cannot configure child process job");
    Handle reader, writer, null_input;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!limits.inherit_stdin) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        null_input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                       &security, OPEN_EXISTING, 0, nullptr);
        if (null_input.value == INVALID_HANDLE_VALUE) {
            null_input.value = nullptr;
            throw std::runtime_error("cannot open compiler null input");
        }
    }
    if (!log.empty()) {
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        if (!CreatePipe(&reader.value, &writer.value, &security, 0) ||
            !SetHandleInformation(reader.value, HANDLE_FLAG_INHERIT, 0))
            throw std::runtime_error("cannot create process output pipe");
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = writer.value;
        startup.hStdError = writer.value;
    }
    if (!limits.inherit_stdin) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_input.value;
        if (log.empty()) {
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
    }
    // Windows would otherwise look for a bare program name in the current directory first.
    SetEnvironmentVariableW(L"NoDefaultCurrentDirectoryInExePath", L"1");
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr,
                        nullptr, &startup, &process))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "cannot start '" + arguments[0] + "'");
    child.process.value = process.hProcess;
    Handle thread(process.hThread);
    if (!AssignProcessToJobObject(child.job.value, child.process.value)) {
        const auto error = GetLastError();
        TerminateProcess(child.process.value, 2);
        throw std::system_error(static_cast<int>(error), std::system_category(),
                                "cannot manage child process tree");
    }
    if (ResumeThread(thread.value) == static_cast<DWORD>(-1))
        throw std::runtime_error("cannot resume child process");
    if (writer.value) {
        CloseHandle(writer.value);
        writer.value = nullptr;
    }
    bool exited = false;
    DWORD code = 2;
    for (;;) {
        const auto waited = WaitForSingleObject(child.process.value, 0);
        if (waited == WAIT_FAILED)
            throw std::runtime_error("cannot wait for child process");
        if (waited == WAIT_OBJECT_0 && !exited) {
            if (!GetExitCodeProcess(child.process.value, &code))
                throw std::runtime_error("cannot read child status");
            exited = true;
            TerminateJobObject(child.job.value, 2); // close descendant pipe writers as well
        }
        bool drained = true;
        if (reader.value) {
            for (unsigned batch = 0; batch < 16; ++batch) {
                DWORD available = 0;
                if (!PeekNamedPipe(reader.value, nullptr, 0, nullptr, &available, nullptr)) {
                    if (GetLastError() == ERROR_BROKEN_PIPE)
                        break;
                    throw std::runtime_error("cannot inspect process output");
                }
                if (!available)
                    break;
                char bytes[8192];
                DWORD size = 0;
                if (!ReadFile(reader.value, bytes, std::min<DWORD>(available, sizeof(bytes)), &size,
                              nullptr))
                    throw std::runtime_error("cannot read process output");
                capture.write(bytes, size);
                if (batch == 15)
                    drained = false;
            }
        }
        if (exited && drained)
            break;
        deadline(start, limits);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!log.empty())
        capture.finish();
    return code <= 255 ? static_cast<int>(code) : 2;
#else
    std::vector<char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    Descriptor reader, writer, null_input;
    if (!log.empty()) {
        int pipes[2];
        if (pipe(pipes) != 0)
            throw std::system_error(errno, std::generic_category(), "cannot create process pipe");
        reader.value = pipes[0];
        writer.value = pipes[1];
        if (fcntl(reader.value, F_SETFL, O_NONBLOCK) < 0 ||
            fcntl(reader.value, F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(writer.value, F_SETFD, FD_CLOEXEC) < 0)
            throw std::system_error(errno, std::generic_category(),
                                    "cannot configure process pipe");
    }
    if (!limits.inherit_stdin) {
        null_input.value = open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (null_input.value < 0)
            throw std::runtime_error("cannot open compiler null input");
    }
    const auto pid = fork();
    if (pid < 0)
        throw std::system_error(errno, std::generic_category(), "cannot fork");
    if (pid == 0) {
        if (setpgid(0, 0) != 0)
            _exit(127);
        if (null_input.value >= 0) {
            if (null_input.value == 0) {
                if (fcntl(0, F_SETFD, 0) < 0)
                    _exit(127);
            } else if (dup2(null_input.value, STDIN_FILENO) < 0)
                _exit(127);
            if (null_input.value > 2)
                close(null_input.value);
        }
        if (writer.value >= 0) {
            if (dup2(writer.value, STDOUT_FILENO) < 0 || dup2(writer.value, STDERR_FILENO) < 0)
                _exit(127);
            close(reader.value);
            close(writer.value);
        }
        execvp(argv[0], argv.data());
        constexpr char message[] = "wood: could not execute native compiler or program\n";
        (void)write(STDERR_FILENO, message, sizeof(message) - 1);
        _exit(127);
    }
    Child child{pid};
    if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH)
        throw std::system_error(errno, std::generic_category(),
                                "cannot manage child process group");
    if (writer.value >= 0) {
        close(writer.value);
        writer.value = -1;
    }
    int status = 0;
    bool exited = false;
    for (;;) {
        // WNOWAIT keeps the root PID reserved until its group is cleaned up.
        siginfo_t state{};
        if (waitid(P_PID, static_cast<id_t>(pid), &state, WEXITED | WNOHANG | WNOWAIT) < 0) {
            if (errno == EINTR)
                continue;
            throw std::system_error(errno, std::generic_category(), "cannot inspect child status");
        }
        if (state.si_pid == pid && !exited) {
            exited = true;
            kill(-pid, SIGKILL);
        }
        bool drained = true;
        if (reader.value >= 0) {
            for (unsigned batch = 0; batch < 16; ++batch) {
                char bytes[8192];
                const auto size = read(reader.value, bytes, sizeof(bytes));
                if (size < 0) {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break;
                    throw std::system_error(errno, std::generic_category(),
                                            "cannot read process output");
                }
                if (size == 0)
                    break;
                capture.write(bytes, static_cast<std::size_t>(size));
                if (batch == 15)
                    drained = false;
            }
        }
        if (exited && drained)
            break;
        deadline(start, limits);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR)
            throw std::system_error(errno, std::generic_category(),
                                    "cannot wait for child process");
    }
    child.reaped = true;
    if (!log.empty())
        capture.finish();
    return WIFEXITED(status)     ? WEXITSTATUS(status)
           : WIFSIGNALED(status) ? 128 + WTERMSIG(status)
                                 : 2;
#endif
}
} // namespace wood
