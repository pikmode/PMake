#include "executor/runner.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <unistd.h>
    extern char** environ;
#endif

namespace pmake::executor {

namespace {

#ifdef _WIN32
std::wstring utf8_to_utf16(std::string_view s) {
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                   static_cast<int>(s.size()), nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w(static_cast<std::size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(),
                        static_cast<int>(s.size()), w.data(), wlen);
    return w;
}

std::string utf16_to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(),
                                  static_cast<int>(w.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string s(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                        s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring quote_arg(const std::wstring& arg) {
    if (arg.empty()) return L"\"\"";
    bool need = false;
    for (wchar_t c : arg) {
        if (c == L' ' || c == L'\t' || c == L'"') { need = true; break; }
    }
    if (!need) return arg;

    std::wstring out;
    out.push_back(L'"');
    int backslashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++backslashes; continue; }
        if (c == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
            backslashes = 0;
        } else {
            out.append(backslashes, L'\\');
            backslashes = 0;
            out.push_back(c);
        }
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring build_cmdline(const std::string& program,
                           const std::vector<std::string>& args) {
    std::wstring cmd = quote_arg(utf8_to_utf16(program));
    for (const auto& a : args) {
        cmd.push_back(L' ');
        cmd += quote_arg(utf8_to_utf16(a));
    }
    return cmd;
}

std::wstring build_env_block(const std::unordered_map<std::string, std::string>& overrides) {
    // Собираем environ + overrides в блок "K=V\0K=V\0\0"
    std::unordered_map<std::string, std::string> merged;

    LPWCH env_strings = GetEnvironmentStringsW();
    if (env_strings) {
        for (LPWCH p = env_strings; *p; ) {
            std::wstring entry(p);
            auto eq = entry.find(L'=');
            if (eq != std::wstring::npos && eq > 0) {
                std::string k = utf16_to_utf8(entry.substr(0, eq));
                std::string v = utf16_to_utf8(entry.substr(eq + 1));
                merged[k] = v;
            }
            p += entry.size() + 1;
        }
        FreeEnvironmentStringsW(env_strings);
    }

    for (const auto& [k, v] : overrides) merged[k] = v;

    std::wstring block;
    for (const auto& [k, v] : merged) {
        block += utf8_to_utf16(k);
        block.push_back(L'=');
        block += utf8_to_utf16(v);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
#endif

} // namespace

Result Runner::runSync(const Command& cmd) {
    auto t0 = std::chrono::steady_clock::now();
    Result res;

#ifdef _WIN32
    // ---- Windows: CreateProcessW + pipes ----
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutR = nullptr, stdoutW = nullptr;
    HANDLE stderrR = nullptr, stderrW = nullptr;
    CreatePipe(&stdoutR, &stdoutW, &sa, 0);
    CreatePipe(&stderrR, &stderrW, &sa, 0);

    SetHandleInformation(stdoutR, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutW;
    si.hStdError = stderrW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = build_cmdline(cmd.program, cmd.arguments);
    std::wstring cwd = utf8_to_utf16(cmd.workingDirectory);
    std::wstring envblock = build_env_block(cmd.env);

    BOOL ok = CreateProcessW(
        nullptr, cmdline.data(), nullptr, nullptr, TRUE,
        CREATE_UNICODE_ENVIRONMENT,
        envblock.data(), cwd.empty() ? nullptr : cwd.c_str(),
        &si, &pi);

    CloseHandle(stdoutW);
    CloseHandle(stderrW);

    if (!ok) {
        CloseHandle(stdoutR);
        CloseHandle(stderrR);
        res.exitCode = -1;
        res.stderrOutput = "Failed to start process";
        return res;
    }

    // Читаем параллельно оба потока, чтобы не было дедлока на 64KB буфере.
    std::string outBuf, errBuf;
    std::thread tOut([&]() {
        char buf[4096];
        DWORD read = 0;
        while (ReadFile(stdoutR, buf, sizeof(buf), &read, nullptr) && read > 0) {
            outBuf.append(buf, read);
        }
    });
    std::thread tErr([&]() {
        char buf[4096];
        DWORD read = 0;
        while (ReadFile(stderrR, buf, sizeof(buf), &read, nullptr) && read > 0) {
            errBuf.append(buf, read);
        }
    });

    WaitForSingleObject(pi.hProcess, INFINITE);
    tOut.join();
    tErr.join();

    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    res.exitCode = static_cast<int>(code);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(stdoutR);
    CloseHandle(stderrR);

    if (cmd.captureOutput) {
        res.stdoutOutput = std::move(outBuf);
        res.stderrOutput = std::move(errBuf);
    }
#else
    // ---- POSIX: pipe + fork + exec ----
    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        res.exitCode = -1;
        res.stderrOutput = "pipe() failed";
        return res;
    }

    pid_t pid = fork();
    if (pid < 0) {
        res.exitCode = -1;
        res.stderrOutput = "fork() failed";
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        return res;
    }

    if (pid == 0) {
        // child
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);

        if (!cmd.workingDirectory.empty()) {
            if (chdir(cmd.workingDirectory.c_str()) != 0) _exit(127);
        }

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(cmd.program.c_str()));
        for (const auto& a : cmd.arguments) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);

        if (cmd.env.empty()) {
            execvp(cmd.program.c_str(), argv.data());
        } else {
            // merge с environ
            std::unordered_map<std::string, std::string> merged;
            for (char** e = environ; e && *e; ++e) {
                std::string s(*e);
                auto eq = s.find('=');
                if (eq != std::string::npos && eq > 0)
                    merged[s.substr(0, eq)] = s.substr(eq + 1);
            }
            for (const auto& [k, v] : cmd.env) merged[k] = v;
            std::vector<std::string> envStrs;
            std::vector<char*> envp;
            envStrs.reserve(merged.size());
            for (const auto& [k, v] : merged) {
                envStrs.push_back(k + "=" + v);
            }
            for (auto& s : envStrs) envp.push_back(s.data());
            envp.push_back(nullptr);
            execvpe(cmd.program.c_str(), argv.data(), envp.data());
        }
        _exit(127);
    }

    // parent
    close(outPipe[1]);
    close(errPipe[1]);

    std::string outBuf, errBuf;
    std::thread tOut([&]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(outPipe[0], buf, sizeof(buf))) > 0) outBuf.append(buf, n);
    });
    std::thread tErr([&]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(errPipe[0], buf, sizeof(buf))) > 0) errBuf.append(buf, n);
    });

    int status = 0;
    waitpid(pid, &status, 0);
    tOut.join();
    tErr.join();

    close(outPipe[0]);
    close(errPipe[0]);

    if (WIFEXITED(status)) res.exitCode = WEXITSTATUS(status);
    else res.exitCode = -1;

    if (cmd.captureOutput) {
        res.stdoutOutput = std::move(outBuf);
        res.stderrOutput = std::move(errBuf);
    }
#endif

    auto t1 = std::chrono::steady_clock::now();
    res.durationSeconds = std::chrono::duration<double>(t1 - t0).count();
    return res;
}

} // namespace pmake::executor