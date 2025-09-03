// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <iostream>
#include <string>
#include <qemu/launcher.h>

int main() {
    qemu::Launcher launcher("qemu-system-avr");

    // Set the path to the QEMU executable
    launcher.setQemuPath("C:/Path/To/qemu-system-x86_64.exe");

    // Set the BIOS file for the virtual machine
    launcher.setBios("C:/Path/To/your-vm-image.qcow2");

    // Add additional arguments if needed
    launcher.addArgument("-m 2048"); // Allocate 2048 MB of RAM

    // Set up standard output callback
    launcher.onStdOut([](const std::string& output) {
        std::cout << "QEMU STDOUT: " << output << std::endl;
    });

    // Set up error callback
    launcher.onStdErr([](const std::string& error) {
        std::cerr << "QEMU STDERR: " << error << std::endl;
    });

    // Set up serial output callback
    launcher.onSerial([](const std::string& msg) {
        std::cout << "QEMU SERIAL: " << msg << std::endl;
    });

    // Set up exit callback
    launcher.onExit([](const int exitCode) {
        std::cerr << "QEMU exited with code: " << exitCode << std::endl;
    });

    // Launch the virtual machine
    if (launcher.start()) {
        std::cout << "QEMU launched successfully!" << std::endl;
    } else {
        std::cerr << "Failed to launch QEMU." << std::endl;
    }

    return 0;
}