// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#pragma once

#include <string>
#include <functional>
#include <vector>
#include <iostream>
#include <filesystem>
#include <memory>

namespace qemu {

class Launcher {
public:
    Launcher(const std::string& system);
    ~Launcher();

    void setQemuPath(const std::string& path);
    void setBios(const std::string& bios);
    void addArgument(const std::string& arg);

    void onStdOut(std::function<void(const std::string& msg)> callback);
    void onStdErr(std::function<void(const std::string& msg)> callback);
    void onSerial(std::function<void(const std::string& msg)> callback);
    void onExit(std::function<void(const int exitCode)> callback);

    void writeStdIn(const std::string& msg);

    bool start();
    bool stop();
    bool terminate();

    std::string qemuPath() const;
    std::string bios() const;
    std::vector<std::string> arguments() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace qemu
