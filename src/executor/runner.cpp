#include "executor/runner.h"

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
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

std::mutex outputCallbackMutex;

void reportOutput(const Command& cmd, OutputStream stream, std::string_view text) {
    if (!cmd.onOutput || text.empty()) return;
    std::lock_guard<std::mutex> lock(outputCallbackMutex);
    cmd.onOutput(stream, text);
}

void saveOutput(const Command& cmd, OutputStream stream,
                std::string_view text, std::string& buffer) {
    if (cmd.captureOutput) buffer.append(text);
    reportOutput(cmd, stream, text);
}

#ifndef _WIN32
std::mutex processSpawnMutex;

bool setCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void closePipe(int (&pipeFds)[2]) {
    if (pipeFds[0] >= 0) close(pipeFds[0]);
    if (pipeFds[1] >= 0) close(pipeFds[1]);
}
#endif

#ifdef _WIN32
std::mutex processSpawnMutex;

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

std::string windows_output_to_utf8(std::string_view bytes) {
    if (bytes.empty()) return {};

    const int utf8Length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (utf8Length > 0) return std::string(bytes);

    const UINT codePage = GetACP();
    const int wideLength = MultiByteToWideChar(
        codePage, 0, bytes.data(), static_cast<int>(bytes.size()),
        nullptr, 0);
    if (wideLength <= 0) return std::string(bytes);

    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(),
                        static_cast<int>(bytes.size()), wide.data(), wideLength);
    return utf16_to_utf8(wide);
}

std::size_t incompleteOutputTail(std::string_view bytes, bool final) {
    if (final || bytes.empty()) return bytes.size();

    std::size_t tailStart = bytes.size();
    const bool validUtf8 = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0) > 0;
    if (validUtf8) return tailStart;

    const auto isContinuation = [](unsigned char ch) {
        return (ch & 0xC0) == 0x80;
    };

    std::size_t lead = bytes.size() - 1;
    std::size_t continuationCount = 0;
    while (lead > 0 && isContinuation(static_cast<unsigned char>(bytes[lead])) &&
           continuationCount < 3) {
        --lead;
        ++continuationCount;
    }

    const auto first = static_cast<unsigned char>(bytes[lead]);
    std::size_t sequenceLength = first >= 0xC2 && first <= 0xDF ? 2
        : first >= 0xE0 && first <= 0xEF ? 3
        : first >= 0xF0 && first <= 0xF4 ? 4
        : 1;
    if (sequenceLength > continuationCount + 1 &&
        sequenceLength > 1 && lead + sequenceLength > bytes.size()) {
        tailStart = lead;
    }

    const UINT codePage = GetACP();
    const auto last = static_cast<unsigned char>(bytes.back());
    if (IsDBCSLeadByteEx(codePage, last)) {
        tailStart = std::min(tailStart, bytes.size() - 1);
    }
    return tailStart;
}

class WindowsOutputDecoder {
public:
    std::string convert(std::string_view bytes, bool final = false) {
        pending_.append(bytes);
        const std::size_t processLength = incompleteOutputTail(pending_, final);
        std::string converted = windows_output_to_utf8(
            std::string_view(pending_).substr(0, processLength));
        pending_.erase(0, processLength);
        if (final && !pending_.empty()) {
            converted += windows_output_to_utf8(pending_);
            pending_.clear();
        }
        return converted;
    }

private:
    std::string pending_;
};

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
    std::wstring application;
    std::wstring cmdline;
    if (cmd.runInShell) {
        wchar_t systemDir[MAX_PATH + 1]{};
        UINT systemDirLength = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (systemDirLength == 0 || systemDirLength >= MAX_PATH) {
            DWORD error = systemDirLength == 0
                ? GetLastError()
                : ERROR_INSUFFICIENT_BUFFER;
            res.exitCode = -1;
            res.stderrOutput = "Failed to locate cmd.exe (Windows error " +
                               std::to_string(error) + ")";
            return res;
        }
        application.assign(systemDir, systemDirLength);
        application += L"\\cmd.exe";
        cmdline = quote_arg(application) + L" /D /S /C \"";
        cmdline += utf8_to_utf16(cmd.shellCommand);
        cmdline.push_back(L'"');
    } else {
        cmdline = build_cmdline(cmd.program, cmd.arguments);
    }
    std::wstring cwd = utf8_to_utf16(cmd.workingDirectory);
    std::wstring envblock = build_env_block(cmd.env);

    std::unique_lock<std::mutex> spawnLock(processSpawnMutex);
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdoutR = nullptr, stdoutW = nullptr;
    HANDLE stderrR = nullptr, stderrW = nullptr;
    if (!CreatePipe(&stdoutR, &stdoutW, &sa, 0) ||
        !CreatePipe(&stderrR, &stderrW, &sa, 0)) {
        DWORD error = GetLastError();
        if (stdoutR) CloseHandle(stdoutR);
        if (stdoutW) CloseHandle(stdoutW);
        if (stderrR) CloseHandle(stderrR);
        if (stderrW) CloseHandle(stderrW);
        res.exitCode = -1;
        res.stderrOutput = "CreatePipe failed (Windows error " +
                           std::to_string(error) + ")";
        return res;
    }

    if (!SetHandleInformation(stdoutR, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderrR, HANDLE_FLAG_INHERIT, 0)) {
        DWORD error = GetLastError();
        CloseHandle(stdoutR);
        CloseHandle(stdoutW);
        CloseHandle(stderrR);
        CloseHandle(stderrW);
        res.exitCode = -1;
        res.stderrOutput = "SetHandleInformation failed (Windows error " +
                           std::to_string(error) + ")";
        return res;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = stdoutW;
    si.hStdError = stderrW;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(
        application.empty() ? nullptr : application.c_str(),
        cmdline.data(), nullptr, nullptr, TRUE,
        CREATE_UNICODE_ENVIRONMENT,
        envblock.data(), cwd.empty() ? nullptr : cwd.c_str(),
        &si, &pi);
    DWORD createError = ok ? ERROR_SUCCESS : GetLastError();

    CloseHandle(stdoutW);
    CloseHandle(stderrW);
    spawnLock.unlock();

    if (!ok) {
        CloseHandle(stdoutR);
        CloseHandle(stderrR);
        res.exitCode = -1;
        res.stderrOutput = "Failed to start process (Windows error " +
                           std::to_string(createError) + ")";
        return res;
    }

    // Read both streams concurrently to avoid blocking on a full pipe buffer.
    std::string outBuf, errBuf;
    std::thread tOut([&]() {
        WindowsOutputDecoder decoder;
        char buf[4096];
        DWORD read = 0;
        while (ReadFile(stdoutR, buf, sizeof(buf), &read, nullptr) && read > 0) {
            std::string converted = decoder.convert(std::string_view(buf, read));
            saveOutput(cmd, OutputStream::Stdout, converted, outBuf);
        }
        std::string converted = decoder.convert({}, true);
        saveOutput(cmd, OutputStream::Stdout, converted, outBuf);
    });
    std::thread tErr([&]() {
        WindowsOutputDecoder decoder;
        char buf[4096];
        DWORD read = 0;
        while (ReadFile(stderrR, buf, sizeof(buf), &read, nullptr) && read > 0) {
            std::string converted = decoder.convert(std::string_view(buf, read));
            saveOutput(cmd, OutputStream::Stderr, converted, errBuf);
        }
        std::string converted = decoder.convert({}, true);
        saveOutput(cmd, OutputStream::Stderr, converted, errBuf);
    });

    DWORD waitResult = WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD waitError = waitResult == WAIT_OBJECT_0 ? ERROR_SUCCESS : GetLastError();
    if (waitResult != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }
    tOut.join();
    tErr.join();

    DWORD code = 0;
    if (waitResult == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code)) {
        res.exitCode = static_cast<int>(code);
    } else {
        res.exitCode = -1;
        DWORD error = waitResult == WAIT_OBJECT_0 ? GetLastError() : waitError;
        const std::string message = "Failed to wait for process (Windows error " +
                                    std::to_string(error) + ")";
        saveOutput(cmd, OutputStream::Stderr, message, errBuf);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(stdoutR);
    CloseHandle(stderrR);

    res.stdoutOutput = std::move(outBuf);
    res.stderrOutput = std::move(errBuf);
#else
    // ---- POSIX: pipe + fork + exec ----
    std::unordered_map<std::string, std::string> mergedEnv;
    for (char** entry = environ; entry && *entry; ++entry) {
        std::string value(*entry);
        std::size_t equals = value.find('=');
        if (equals != std::string::npos && equals > 0) {
            mergedEnv[value.substr(0, equals)] = value.substr(equals + 1);
        }
    }
    for (const auto& [key, value] : cmd.env) mergedEnv[key] = value;

    std::vector<std::string> envStrings;
    envStrings.reserve(mergedEnv.size());
    for (const auto& [key, value] : mergedEnv) {
        envStrings.push_back(key + "=" + value);
    }
    std::vector<char*> envp;
    envp.reserve(envStrings.size() + 1);
    for (auto& entry : envStrings) envp.push_back(entry.data());
    envp.push_back(nullptr);

    const std::string_view program = cmd.runInShell
        ? std::string_view("/bin/sh")
        : std::string_view(cmd.program);
    std::vector<char*> argv;
    argv.reserve((cmd.runInShell ? 2 : cmd.arguments.size()) + 2);
    argv.push_back(const_cast<char*>(program.data()));
    if (cmd.runInShell) {
        argv.push_back(const_cast<char*>("-c"));
        argv.push_back(const_cast<char*>(cmd.shellCommand.c_str()));
    } else {
        for (const auto& arg : cmd.arguments) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
    }
    argv.push_back(nullptr);

    std::vector<std::string> execPaths;
    if (program.find('/') != std::string::npos) {
        execPaths.emplace_back(program);
    } else {
        auto pathIt = mergedEnv.find("PATH");
        std::string searchPath = pathIt == mergedEnv.end() ? "/bin:/usr/bin" : pathIt->second;
        std::size_t start = 0;
        while (start <= searchPath.size()) {
            std::size_t end = searchPath.find(':', start);
            if (end == std::string::npos) end = searchPath.size();
            std::string directory = searchPath.substr(start, end - start);
            execPaths.push_back(directory.empty()
                ? std::string(program)
                : directory + "/" + std::string(program));
            if (end == searchPath.size()) break;
            start = end + 1;
        }
    }

    int outPipe[2] = {-1, -1};
    int errPipe[2] = {-1, -1};
    pid_t pid = -1;
    std::unique_lock<std::mutex> spawnLock(processSpawnMutex);
    if (pipe(outPipe) != 0 || pipe(errPipe) != 0) {
        int savedErrno = errno;
        for (int fd : {outPipe[0], outPipe[1], errPipe[0], errPipe[1]}) {
            if (fd != STDOUT_FILENO && fd != STDERR_FILENO) close(fd);
        }
        res.exitCode = -1;
        res.stderrOutput = std::string("pipe() failed: ") + std::strerror(savedErrno);
        return res;
    }

    if (!setCloseOnExec(outPipe[0]) || !setCloseOnExec(outPipe[1]) ||
        !setCloseOnExec(errPipe[0]) || !setCloseOnExec(errPipe[1])) {
        int savedErrno = errno;
        closePipe(outPipe);
        closePipe(errPipe);
        res.exitCode = -1;
        res.stderrOutput = std::string("fcntl(FD_CLOEXEC) failed: ") +
                           std::strerror(savedErrno);
        return res;
    }

    pid = fork();
    if (pid < 0) {
        int savedErrno = errno;
        closePipe(outPipe);
        closePipe(errPipe);
        res.exitCode = -1;
        res.stderrOutput = std::string("fork() failed: ") + std::strerror(savedErrno);
        return res;
    }

    if (pid == 0) {
        if (dup2(outPipe[1], STDOUT_FILENO) == -1 ||
            dup2(errPipe[1], STDERR_FILENO) == -1) {
            _exit(127);
        }
        closePipe(outPipe);
        closePipe(errPipe);

        if (!cmd.workingDirectory.empty()) {
            if (chdir(cmd.workingDirectory.c_str()) != 0) _exit(127);
        }

        bool permissionDenied = false;
        for (const auto& path : execPaths) {
            execve(path.c_str(), argv.data(), envp.data());
            if (errno == EACCES) permissionDenied = true;
        }
        _exit(permissionDenied ? 126 : 127);
    }

    close(outPipe[1]);
    outPipe[1] = -1;
    close(errPipe[1]);
    errPipe[1] = -1;
    spawnLock.unlock();

    std::string outBuf, errBuf;
    std::thread tOut([&]() {
        char buf[4096];
        while (true) {
            ssize_t n = read(outPipe[0], buf, sizeof(buf));
            if (n > 0) {
                saveOutput(cmd, OutputStream::Stdout,
                           std::string_view(buf, static_cast<std::size_t>(n)), outBuf);
            }
            else if (n < 0 && errno == EINTR) continue;
            else break;
        }
    });
    std::thread tErr([&]() {
        char buf[4096];
        while (true) {
            ssize_t n = read(errPipe[0], buf, sizeof(buf));
            if (n > 0) {
                saveOutput(cmd, OutputStream::Stderr,
                           std::string_view(buf, static_cast<std::size_t>(n)), errBuf);
            }
            else if (n < 0 && errno == EINTR) continue;
            else break;
        }
    });

    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    int waitErrno = waited == -1 ? errno : 0;
    tOut.join();
    tErr.join();

    close(outPipe[0]);
    close(errPipe[0]);

    if (waited == -1) {
        res.exitCode = -1;
        const std::string message = std::string("waitpid() failed: ") +
                                    std::strerror(waitErrno);
        saveOutput(cmd, OutputStream::Stderr, message, errBuf);
    } else if (WIFEXITED(status)) res.exitCode = WEXITSTATUS(status);
    else res.exitCode = -1;

    res.stdoutOutput = std::move(outBuf);
    res.stderrOutput = std::move(errBuf);
#endif

    auto t1 = std::chrono::steady_clock::now();
    res.durationSeconds = std::chrono::duration<double>(t1 - t0).count();
    return res;
}

} // namespace pmake::executor