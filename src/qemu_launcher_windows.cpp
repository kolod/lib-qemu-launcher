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

    // Find the QEMU executable in the system PATH or common installation paths or environment variable QEMU_ROOT
    // argument: system - the system type (e.g., "qemu-system-avr", "qemu-system-arm", "qemu-system-x86_64", etc.)
    std::string findQemuExecutable(const std::string& system) {
        
        // Check the QEMU_ROOT environment variable
        auto qemu_env = findQemuExecutableEnv(system);
        if (qemu_env) return windowsToStdString(std::move(qemu_env));

        // Search for the QEMU executable in the system PATH
        auto path_path = findQemuExecutablePath(system);
        if (path_path) return windowsToStdString(std::move(path_path));

        // Check the registry for the QEMU installation path
        auto registry_path = findQemuExecutableRegistry(system);
        if (registry_path) return windowsToStdString(std::move(registry_path));

        return "";
    }

    // Launch QEMU with the specified arguments
    bool launchQemu(
        std::string qemuPath,
        std::string biosFile,
        std::vector<std::string> arguments,
        std::iostream* onStdOut,
        std::iostream* onStdErr,
        std::iostream* onSerial,
        std::function<void(int exitCode)> onExit
    ) {

        // Execute QEMU process
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        ZeroMemory(&pi, sizeof(pi));

        // Create the QEMU command line
        std::string commandLine = qemuPath + " -bios " + biosFile;
        for (const auto& arg : arguments) commandLine += " " + arg;

        // Create the process
        auto qemuPathW = stdStringToWindows(qemuPath);
        auto commandLineW = stdStringToWindows(commandLine);

        if (!CreateProcessW(
            qemuPathW.get(),    // QEMU executable path
            commandLineW.get(), // Command line arguments
            nullptr,            // Process attributes
            nullptr,            
            TRUE,
            0,
            nullptr,
            nullptr,
            &si,
            &pi
        )) {
            std::cerr << "Failed to start QEMU process." << std::endl;
            return false;
        }


        // Implementation
        return true;
    }
}