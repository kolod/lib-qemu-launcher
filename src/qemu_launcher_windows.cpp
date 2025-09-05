// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <string>
#include <iostream>
#include <vector>
#include <functional>
#include <memory>
#include <wchar.h>
#if defined(__GNUC__)
  #include <ext/stdio_filebuf.h>      // __gnu_cxx::stdio_filebuf
#else
  #include <io.h>                     // _open_osfhandle
  #include <fcntl.h>                  // _O_RDONLY, etc.
  #include <fstream>                  // std::filebuf
#endif


#include "qemu/launcher.h"

// Windows-specific implementation will go here
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace qemu {

    size_t maxPathLength() {
        // Cache the result for the lifetime of the process (thread-safe initialization)
        static const size_t cached = []() -> size_t {
            // Windows 10 (1607+) can enable long paths (up to 32767 chars) via policy.
            // Detect policy: HKLM\SYSTEM\CurrentControlSet\Control\FileSystem\LongPathsEnabled == 1
            DWORD longPaths = 0;
            DWORD type = 0;
            DWORD cb = sizeof(longPaths);
            if (RegGetValueW(
                    HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\FileSystem",
                    L"LongPathsEnabled",
                    RRF_RT_REG_DWORD,
                    &type,
                    &longPaths,
                    &cb) == ERROR_SUCCESS &&
                type == REG_DWORD &&
                longPaths == 1) {
                return 32767; // Extended-length path max for Unicode APIs (includes null)
            }
            return MAX_PATH;
        }();
        return cached;
    }

    // Convert std::string to smart pointer to wchar_t* (null-terminated) for Win32 API
    std::unique_ptr<wchar_t[]> stdStringToWindows(const std::string& str) {
        int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
        if (size <= 0) {
            return nullptr;
        }
        std::unique_ptr<wchar_t[]> buffer(new wchar_t[size]);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, buffer.get(), size);
        return buffer;
    }

    // Convert wchar_t* (null-terminated) to std::string
    std::string windowsToStdString(const wchar_t* wstr) {
        int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
        std::string str(size, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size, nullptr, nullptr);
        return str;
    }

    // Convert std::unique_ptr<wchar_t[]> to std::string
    // input data deleted after use
    std::string windowsToStdString(std::unique_ptr<wchar_t[]> wstr) {
        return windowsToStdString(wstr.get());
    }

    // Check if a file exists
    bool isFileExists(const wchar_t* path) {
        DWORD attr = GetFileAttributesW(path);
        return (attr != INVALID_FILE_ATTRIBUTES);
    }

    // Construct the path to the QEMU executable from the given directory and system
    // Returns a unique_ptr to the path if it exists, nullptr otherwise
    std::unique_ptr<wchar_t[]> getExePathIfExists(wchar_t* directory, std::string system) {
        auto max_path = maxPathLength();
        auto path = std::make_unique<wchar_t[]>(max_path);
        auto system_w = stdStringToWindows(system);
        auto locale = _create_locale(LC_ALL, "C");

        _snwprintf_s_l(path.get(), max_path * sizeof(wchar_t), max_path, L"%s\\%s.exe", locale, directory, system_w.get());
        if (isFileExists(path.get())) return path;

        path.reset();
        return nullptr;
    }

    // Find the QEMU executable in the QEMU_ROOT environment variable
    std::unique_ptr<wchar_t[]> findQemuExecutableEnv(const std::string& system) {
        auto size = GetEnvironmentVariableW(L"QEMU_ROOT", nullptr, 0);
        if (size == 0) return nullptr;

        auto qemu_root_path = std::make_unique<wchar_t[]>(size);
        auto written = GetEnvironmentVariableW(L"QEMU_ROOT", qemu_root_path.get(), size);
        if ((written > 0) && (written < size)) {
            auto path = getExePathIfExists(qemu_root_path.get(), system);
            if (path) return path;
        }

        return nullptr;
    }

    // Find the QEMU executable in the system PATH
    std::unique_ptr<wchar_t[]> findQemuExecutablePath(const std::string& system) {
        auto size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (size == 0) return nullptr;

        auto path_env = std::make_unique<wchar_t[]>(size);
        auto written = GetEnvironmentVariableW(L"PATH", path_env.get(), size);
        if ((written > 0) && (written < size)) {
            auto current_path = path_env.get();
            for (size_t i = 0; i < size; i++) {
                if (path_env[i] == L';' || path_env[i] == L'\0') {
                    path_env[i] = L'\0';

                    auto path = getExePathIfExists(current_path, system);
                    if (path) return path;

                    current_path = &path_env[i + 1];
                }
            }
        }

        return nullptr;
    }

    // Find the QEMU executable by HKLM\SOFTWARE\QEMU\Install_Dir registry key
    std::unique_ptr<wchar_t[]> findQemuExecutableRegistry(const std::string& system) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\QEMU", 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
            return nullptr;
        }

        DWORD qemu_root_path_size = maxPathLength() * sizeof(wchar_t);
        auto qemu_root_path = std::make_unique<wchar_t[]>(maxPathLength());
        if (RegQueryValueExW(
            hKey,                  // Registry key handle
            L"Install_Dir",        // Value name
            nullptr,               // Reserved
            nullptr,               // Type
            reinterpret_cast<LPBYTE>(qemu_root_path.get()), // Data
            &qemu_root_path_size
        ) != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return nullptr;
        }

        RegCloseKey(hKey);
        return getExePathIfExists(qemu_root_path.get(), system);
    }
    struct Launcher::Impl {

        std::string mQemuPath;
        std::string mBiosFile;
        std::vector<std::string> mArguments;
        std::function<void(const std::string& msg)> mOnStdOut;
        std::function<void(const std::string& msg)> mOnStdErr;
        std::function<void(const std::string& msg)> mOnSerial;
        std::function<void(int exitCode)> mOnExit;

        std::unique_ptr<std::iostream> mQemuSerial = nullptr;
        std::unique_ptr<std::istream> mQemuStdout = nullptr;
        std::unique_ptr<std::istream> mQemuStderr = nullptr;
        std::unique_ptr<std::ostream> mQemuStdin = nullptr;

        bool findQemuExecutable(const std::string& system) {
            mQemuPath = "";

            // Check for empty system name
            if (system.empty()) {
                std::cerr << "Error: System name is empty." << std::endl;
                return false;
            }
            
            // Check the QEMU_ROOT environment variable
            if (auto path = findQemuExecutableEnv(system)) {
                mQemuPath = windowsToStdString(std::move(path));
                std::cout << "Found QEMU executable in QEMU_ROOT: " << mQemuPath << std::endl;
                return true;
            }

            // Search for the QEMU executable in the system PATH
            if (auto path = findQemuExecutablePath(system)) {
                mQemuPath = windowsToStdString(std::move(path));
                std::cout << "Found QEMU executable in PATH: " << mQemuPath << std::endl;
                return true;
            }

            // Check the registry for the QEMU installation path
            if (auto path = findQemuExecutableRegistry(system)) {
                mQemuPath = windowsToStdString(std::move(path));
                std::cout << "Found QEMU executable in registry: " << mQemuPath << std::endl;
                return true;
            }

            return false;
        }

        bool start() {
            // Create named pipe
            auto pipeName = L"\\\\.\\pipe\\qemu_pipe";
            HANDLE hSerialPipe = CreateNamedPipeW(
                pipeName,
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                1,
                1024 * 1024,
                1024 * 1024,
                0,
                nullptr
            );

            if (hSerialPipe == INVALID_HANDLE_VALUE) {
                std::cerr << "Failed to create named pipe." << std::endl;
                return false;
            }

            // Prepare security attributes for inherited handles
            SECURITY_ATTRIBUTES sa{};
            sa.nLength = sizeof(sa);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = nullptr;

            // Create pipes for child's STDIN, STDOUT, and STDERR
            HANDLE stdInRd = nullptr, stdInWr = nullptr;
            CreatePipe(&stdInRd, &stdInWr, &sa, 0);

            HANDLE stdOutRd = nullptr, stdOutWr = nullptr;
            CreatePipe(&stdOutRd, &stdOutWr, &sa, 0);

            HANDLE stdErrRd = nullptr, stdErrWr = nullptr;
            CreatePipe(&stdErrRd, &stdErrWr, &sa, 0);

            // Ensure the parent-side handles are not inherited
            SetHandleInformation(stdInWr, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdOutRd, HANDLE_FLAG_INHERIT, 0);
            SetHandleInformation(stdErrRd, HANDLE_FLAG_INHERIT, 0);

            // Set up STARTUPINFO to redirect child's std handles
            STARTUPINFOW si;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = stdInWr;
            si.hStdOutput = stdOutRd;
            si.hStdError = stdErrRd;

            PROCESS_INFORMATION pi;
            ZeroMemory(&pi, sizeof(pi));

            // Create the QEMU command line
            std::string commandLine = mQemuPath + " -bios " + mBiosFile;
            for (const auto& arg : mArguments) commandLine += " " + arg;

            // Create the child process
            auto qemuPathW = stdStringToWindows(mQemuPath);
            auto commandLineW = stdStringToWindows(commandLine);
            if (!CreateProcessW(qemuPathW.get(), commandLineW.get(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi)) {
                std::cerr << "Failed to start QEMU process." << std::endl;
                return false;
            }

            // Close unused ends in the parent
            CloseHandle(stdInRd);
            CloseHandle(stdOutWr);
            CloseHandle(stdErrWr);

            // Wrap pipe
#if defined(__GNUC__)
            __gnu_cxx::stdio_filebuf<char> stdInBuf(reinterpret_cast<intptr_t>(stdInWr), std::ios::out);
            __gnu_cxx::stdio_filebuf<char> stdOutBuf(reinterpret_cast<intptr_t>(stdOutRd), std::ios::in);
            __gnu_cxx::stdio_filebuf<char> stdErrBuf(reinterpret_cast<intptr_t>(stdErrRd), std::ios::in);

            mQemuStdin.reset(new std::ostream(&stdInBuf));
            mQemuStdout.reset(new std::istream(&stdOutBuf));
            mQemuStderr.reset(new std::istream(&stdErrBuf));
#else
            // For MSVC, use _open_osfhandle to convert HANDLE to file descriptor
            if (int stdInFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdInRd), _O_RDONLY) > 0) {
                mQemuStdin.reset(new std::ostream(&stdInFd));
            }

            if (int stdOutFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdOutWr), _O_WRONLY) > 0) {
                mQemuStdout.reset(new std::istream(&stdOutFd));
            }

            if (int stdErrFd = _open_osfhandle(reinterpret_cast<intptr_t>(stdErrWr), _O_WRONLY) > 0) {
                mQemuStderr.reset(new std::istream(&stdErrFd));
            }
#endif

            // Wait while QEMU connects to serial pipe
            if (!ConnectNamedPipe(hSerialPipe, nullptr)) {
                if (GetLastError() == ERROR_PIPE_CONNECTED) {
                    std::cerr << "QEMU is already connected to the serial pipe." << std::endl;
                } else {
                    std::cerr << "Failed to connect to serial pipe." << std::endl;
                }
                stop();
                return false;
            }

            return true;
        }

        bool stop() {
            // Implementation
            return true;
        }

        bool terminate() {
            // Implementation
            return true;
        }
    };

    Launcher::Launcher(const std::string& system) : pImpl(std::make_unique<Impl>()) {
        pImpl->findQemuExecutable(system);
    }

    Launcher::~Launcher() {
        if (!pImpl->stop()) {
            pImpl->terminate();
        }
    }

    void Launcher::setQemuPath(const std::string& path) {
        pImpl->mQemuPath = path;
    }

    void Launcher::setBios(const std::string& bios) {
        pImpl->mBiosFile = bios;
    }

    void Launcher::addArgument(const std::string& arg) {
        pImpl->mArguments.push_back(arg);
    }

    void Launcher::onStdOut(std::function<void(const std::string& msg)> callback) {
        pImpl->mOnStdOut = std::move(callback);
    }

    void Launcher::onStdErr(std::function<void(const std::string& msg)> callback) {
        pImpl->mOnStdErr = std::move(callback);
    }

    void Launcher::onSerial(std::function<void(const std::string& msg)> callback) {
        pImpl->mOnSerial = std::move(callback);
    }

    void Launcher::onExit(std::function<void(const int exitCode)> callback) {
        pImpl->mOnExit = std::move(callback);
    }

    void Launcher::writeStdIn(const std::string& msg) {
        if (pImpl->mQemuStdin != nullptr) {
            *pImpl->mQemuStdin << msg;
        }
    }

    bool Launcher::start() {
        if (!pImpl->mQemuPath.empty()) {
            std::cerr << "Error: QEMU executable path is not set." << std::endl;
            return false;
        }

        if (!pImpl->mBiosFile.empty()) {
            std::cerr << "Error: BIOS file is not set." << std::endl;
            return false;
        }

        std::cout << "Starting QEMU with the following parameters:" << std::endl;
        std::cout << "QEMU Path: " << pImpl->mQemuPath << std::endl;
        std::cout << "BIOS File: " << pImpl->mBiosFile << std::endl;
        std::cout << "Arguments: ";
        for (const auto& arg : pImpl->mArguments) std::cout << arg << " ";
        std::cout << std::endl;

        return pImpl->start();
    }

    bool Launcher::stop() {
        return pImpl->stop();
    }

    bool Launcher::terminate() {
        return pImpl->terminate();
    }

    std::string Launcher::qemuPath() const {
        return pImpl->mQemuPath;
    }

    std::string Launcher::bios() const {
        return pImpl->mBiosFile;
    }

    std::vector<std::string> Launcher::arguments() const {
        return pImpl->mArguments;
    }

} // namespace qemu