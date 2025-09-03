// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <qemu/launcher.h>

namespace qemu {

    extern std::string findQemuExecutable(const std::string& system);
    extern bool launchQemu(
        std::string qemuPath,
        std::string biosFile,
        std::vector<std::string> arguments,
        std::function<void(const std::string& msg)> onStdOut,
        std::function<void(const std::string& msg)> onStdErr,
        std::function<void(const std::string& msg)> onSerial,
        std::function<void(int exitCode)> onExit
    );

    Launcher::Launcher(const std::string& system) : mQemuPath(""), mBiosFile(""), mArguments() {
        mQemuPath = findQemuExecutable(system);
    }

    Launcher::~Launcher() {
        stop();
    }

    void Launcher::setQemuPath(const std::string& path) {
        mQemuPath = path;
    }

    void Launcher::setBios(const std::string& bios) {
        mBiosFile = bios;
    }

    void Launcher::addArgument(const std::string& arg) {
        mArguments.push_back(arg);
    }

    void Launcher::onStdOut(std::function<void(const std::string& msg)> callback) {
        onStdOutCallback = callback;
    }

    void Launcher::onStdErr(std::function<void(const std::string& msg)> callback) {
        onStdErrCallback = callback;
    }

    void Launcher::onSerial(std::function<void(const std::string& msg)> callback) {
        onSerialCallback = callback;
    }

    void Launcher::onExit(std::function<void(int exitCode)> callback) {
        onExitCallback = callback;
    }

    void Launcher::writeStdIn(const std::string& msg) {
        // Implementation
    }

    bool Launcher::start() {
        // Check if QEMU executable path is set
        if (mQemuPath.empty()) {
            std::cerr << "QEMU executable path is not set." << std::endl;
            return false;
        }

        // Check if QEMU path is valid
        if (!std::filesystem::exists(mQemuPath)) {
            std::cerr << "QEMU executable not found at: " << mQemuPath << std::endl;
            return false;
        }

        // Check if BIOS file is set
        if (mBiosFile.empty()) {
            std::cerr << "BIOS file is not set." << std::endl;
            return false;
        }

        // Check if BIOS file exists
        if (!std::filesystem::exists(mBiosFile)) {
            std::cerr << "BIOS file not found at: " << mBiosFile << std::endl;
            return false;
        }

        // TODO: Set up QEMU command line arguments
        // TODO: Implement QEMU launch process

        // Start QEMU process
        return true;
    }

    bool Launcher::stop() {
        // Implementation
        return true;
    }

    bool Launcher::terminate() {
        // Implementation
        return true;
    }

    std::string Launcher::qemuPath() const {
        return mQemuPath;
    }

    std::string Launcher::bios() const {
        return mBiosFile;
    }

    std::vector<std::string> Launcher::arguments() const {
        return mArguments;
    }

} // namespace qemu