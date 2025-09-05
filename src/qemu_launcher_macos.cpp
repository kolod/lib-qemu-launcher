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
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <limits.h>
#include <ext/stdio_filebuf.h>

#include "qemu/launcher.h"

// macOS-specific implementation

namespace qemu {

    size_t maxPathLength() {
        static const size_t cached = PATH_MAX;
        return cached;
    }

    // Check if a file exists and is executable
    bool isFileExists(const std::string& path) {
        return std::filesystem::exists(path) && access(path.c_str(), X_OK) == 0;
    }

    // Construct the path to the QEMU executable from the given directory and system
    // Returns the path if it exists, empty string otherwise
    std::string getExePathIfExists(const std::string& directory, const std::string& system) {
        // Construct the expected path to the QEMU executable (no extension on macOS)
        std::string exePath = directory + "/" + system;

        // Check if the file exists and is executable
        if (isFileExists(exePath)) return exePath;
        return "";
    }

    // Find the QEMU executable in the QEMU_ROOT environment variable
    std::string findQemuExecutableEnv(const std::string& system) {
        // Check the QEMU_ROOT environment variable
        const auto* qemu_root = std::getenv("QEMU_ROOT");
        if (qemu_root == nullptr) return "";

        return getExePathIfExists(qemu_root, system);
    }

    // Find the QEMU executable in the system PATH
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

    // Find the QEMU executable in common installation paths for macOS
    std::string findQemuExecutableCommon(const std::string& system) {
        // Common installation paths on macOS
        std::vector<std::string> commonPaths = {
            "/usr/local/bin",
            "/opt/homebrew/bin",      // Apple Silicon Homebrew
            "/usr/local/Cellar/qemu", // Intel Homebrew
            "/opt/homebrew/Cellar/qemu", // Apple Silicon Homebrew
            "/Applications/QEMU.app/Contents/MacOS",
            "/usr/bin",
            "/opt/qemu/bin"
        };

        for (const auto& path : commonPaths) {
            auto exePath = getExePathIfExists(path, system);
            if (!exePath.empty()) return exePath;
        }

        return "";
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

        pid_t mQemuPid = -1;
        int mStdInPipe[2] = {-1, -1};
        int mStdOutPipe[2] = {-1, -1};
        int mStdErrPipe[2] = {-1, -1};

        bool findQemuExecutable(const std::string& system) {
            mQemuPath = "";

            // Check for empty system name
            if (system.empty()) {
                std::cerr << "Error: System name is empty." << std::endl;
                return false;
            }
            
            // Check the QEMU_ROOT environment variable
            auto envPath = findQemuExecutableEnv(system);
            if (!envPath.empty()) {
                mQemuPath = envPath;
                std::cout << "Found QEMU executable in QEMU_ROOT: " << mQemuPath << std::endl;
                return true;
            }

            // Search for the QEMU executable in the system PATH
            auto pathExe = findQemuExecutablePath(system);
            if (!pathExe.empty()) {
                mQemuPath = pathExe;
                std::cout << "Found QEMU executable in PATH: " << mQemuPath << std::endl;
                return true;
            }

            // Check common installation paths
            auto commonPath = findQemuExecutableCommon(system);
            if (!commonPath.empty()) {
                mQemuPath = commonPath;
                std::cout << "Found QEMU executable in common paths: " << mQemuPath << std::endl;
                return true;
            }

            return false;
        }

        bool start() {
            // Create pipes for communication
            if (pipe(mStdInPipe) == -1 || pipe(mStdOutPipe) == -1 || pipe(mStdErrPipe) == -1) {
                std::cerr << "Failed to create pipes." << std::endl;
                return false;
            }

            // Fork the process
            mQemuPid = fork();
            if (mQemuPid == -1) {
                std::cerr << "Failed to fork process." << std::endl;
                return false;
            }

            if (mQemuPid == 0) {
                // Child process
                // Redirect stdin, stdout, stderr
                dup2(mStdInPipe[0], STDIN_FILENO);
                dup2(mStdOutPipe[1], STDOUT_FILENO);
                dup2(mStdErrPipe[1], STDERR_FILENO);

                // Close unused pipe ends
                close(mStdInPipe[1]);
                close(mStdOutPipe[0]);
                close(mStdErrPipe[0]);

                // Build command line arguments
                std::vector<char*> args;
                args.push_back(const_cast<char*>(mQemuPath.c_str()));
                
                if (!mBiosFile.empty()) {
                    args.push_back(const_cast<char*>("-bios"));
                    args.push_back(const_cast<char*>(mBiosFile.c_str()));
                }

                for (const auto& arg : mArguments) {
                    args.push_back(const_cast<char*>(arg.c_str()));
                }
                args.push_back(nullptr);

                // Execute QEMU
                execv(mQemuPath.c_str(), args.data());
                std::cerr << "Failed to exec QEMU process." << std::endl;
                _exit(1);
            } else {
                // Parent process
                // Close unused pipe ends
                close(mStdInPipe[0]);
                close(mStdOutPipe[1]);
                close(mStdErrPipe[1]);

                // Create stream wrappers using GNU extension (available on macOS with GCC)
                __gnu_cxx::stdio_filebuf<char> stdInBuf(mStdInPipe[1], std::ios::out);
                __gnu_cxx::stdio_filebuf<char> stdOutBuf(mStdOutPipe[0], std::ios::in);
                __gnu_cxx::stdio_filebuf<char> stdErrBuf(mStdErrPipe[0], std::ios::in);

                mQemuStdin.reset(new std::ostream(&stdInBuf));
                mQemuStdout.reset(new std::istream(&stdOutBuf));
                mQemuStderr.reset(new std::istream(&stdErrBuf));

                return true;
            }
        }

        bool stop() {
            if (mQemuPid > 0) {
                kill(mQemuPid, SIGTERM);
                int status;
                waitpid(mQemuPid, &status, 0);
                mQemuPid = -1;
                
                // Close pipes
                if (mStdInPipe[1] != -1) { close(mStdInPipe[1]); mStdInPipe[1] = -1; }
                if (mStdOutPipe[0] != -1) { close(mStdOutPipe[0]); mStdOutPipe[0] = -1; }
                if (mStdErrPipe[0] != -1) { close(mStdErrPipe[0]); mStdErrPipe[0] = -1; }
                
                return true;
            }
            return false;
        }

        bool terminate() {
            if (mQemuPid > 0) {
                kill(mQemuPid, SIGKILL);
                int status;
                waitpid(mQemuPid, &status, 0);
                mQemuPid = -1;
                
                // Close pipes
                if (mStdInPipe[1] != -1) { close(mStdInPipe[1]); mStdInPipe[1] = -1; }
                if (mStdOutPipe[0] != -1) { close(mStdOutPipe[0]); mStdOutPipe[0] = -1; }
                if (mStdErrPipe[0] != -1) { close(mStdErrPipe[0]); mStdErrPipe[0] = -1; }
                
                return true;
            }
            return false;
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
        if (pImpl->mQemuPath.empty()) {
            std::cerr << "Error: QEMU executable path is not set." << std::endl;
            return false;
        }

        if (pImpl->mBiosFile.empty()) {
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