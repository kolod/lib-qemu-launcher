// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <string>
#include <iostream>
#include <vector>
#include <functional>
#include <filesystem>
#include <memory>
#include <cstdlib>

// Linux-specific implementation will go here

namespace qemu {

    std::string getExePathIfExists(const std::string& directory, const std::string& system) {
        // Construct the expected path to the QEMU executable
        std::string exePath = directory + "/" + system + ".exe";

        // Check if the file exists
        if (std::filesystem::exists(exePath)) return exePath;
        return "";
    }

    std::string findQemuExecutableEnv(const std::string& system) {
        // Check the QEMU_ROOT environment variable
        const auto* qemu_root = std::getenv("QEMU_ROOT");
        if (qemu_root == nullptr) return "";

        return getExePathIfExists(qemu_root, system);
    }

    std::string findQemuExecutablePath(const std::string& system) {
        // Check the PATH environment variable
        const auto* path_env = std::getenv("PATH");
        if (path_env == nullptr) return "";

        std::string pathStr(path_env);
        size_t start = 0;
        size_t end = pathStr.find(':');

        while (end != std::string::npos) {
            std::string directory = pathStr.substr(start, end - start);
            auto exePath = getExePathIfExists(directory, system);
            if (!exePath.empty()) return exePath;

            start = end + 1;
            end = pathStr.find(':', start);
        }

        // Check the last (or only) directory in PATH
        std::string directory = pathStr.substr(start);
        return getExePathIfExists(directory, system);
    }

    // Find the QEMU executable in the system PATH or common installation paths or environment variable QEMU_ROOT
    // argument: system - the system type (e.g., "qemu-system-avr", "qemu-system-arm", "qemu-system-x86_64", etc.)
    std::string findQemuExecutable(const std::string& system) {
        // Check the QEMU_ROOT environment variable
        auto qemu_env = findQemuExecutableEnv(system);
        if (!qemu_env.empty()) return qemu_env;

        // Search for the QEMU executable in the system PATH
        auto path_path = findQemuExecutablePath(system);
        if (!path_path.empty()) return path_path;

        return "";
    }

} // namespace qemu