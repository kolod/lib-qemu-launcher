// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <gtest/gtest.h>
#include <qemu/launcher.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>

namespace {

// RAII environment variable setter/restorer for macOS/Unix
class MacOSEnvVarGuard {
public:
    MacOSEnvVarGuard(const char* name, const std::string& value) : name_(name) {
        // Save original
        const char* orig = std::getenv(name_);
        if (orig) {
            original_ = orig;
            hadOriginal_ = true;
        }
        // Set new value using putenv (more portable)
        std::string envVar = std::string(name_) + "=" + value;
        envString_ = envVar; // Keep alive for putenv
        putenv(const_cast<char*>(envString_.c_str()));
    }

    ~MacOSEnvVarGuard() {
        if (hadOriginal_) {
            std::string envVar = std::string(name_) + "=" + original_;
            putenv(const_cast<char*>(envVar.c_str()));
        } else {
            // For unsetting, we use putenv with just the name (platform-specific behavior)
            std::string envVar = std::string(name_) + "=";
            putenv(const_cast<char*>(envVar.c_str()));
        }
    }

private:
    const char* name_;
    bool hadOriginal_ = false;
    std::string original_;
    std::string envString_; // Keep the string alive for putenv
};

// Create a unique temporary directory and return its path
std::filesystem::path make_temp_dir(const std::string& prefix) {
    auto tmpPath = std::filesystem::temp_directory_path();
    for (int i = 0; i < 100; ++i) {
        auto candidate = tmpPath / (prefix + "_" + std::to_string(getpid()) + "_" + std::to_string(i));
        std::error_code ec;
        if (std::filesystem::create_directory(candidate, ec)) {
            return candidate;
        }
    }
    throw std::runtime_error("Failed to create temporary directory");
}

void touch_file(const std::filesystem::path& p) {
    std::ofstream ofs(p, std::ios::binary);
    ofs << "#!/bin/bash\necho 'Mock QEMU'\n"; // Make it look like a shell script
    ofs.close();
    // Make executable using filesystem permissions
    std::filesystem::permissions(p, 
        std::filesystem::perms::owner_read | 
        std::filesystem::perms::owner_write | 
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_read |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_read |
        std::filesystem::perms::others_exec
    );
}

} // namespace

TEST(MacOSFindQemuExecutableEnv, PicksFromQemuRoot) {
    // Arrange: create temp dir with fake qemu exe
    auto tmpDir = make_temp_dir("qemu_env");
    auto exePath = tmpDir / "qemu-system-x86_64";
    touch_file(exePath);

    // Set QEMU_ROOT to tempDir and ensure PATH doesn't interfere
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", tmpDir.string());
    MacOSEnvVarGuard guardPath("PATH", "");

    // Act
    qemu::Launcher launcher("qemu-system-x86_64");

    // Assert
    EXPECT_FALSE(launcher.qemuPath().empty());
    EXPECT_TRUE(std::filesystem::exists(launcher.qemuPath()));
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(MacOSFindQemuExecutablePath, PicksFromPathWhenEnvMissing) {
    // Arrange: create temp dir with fake qemu exe
    auto tmpDir = make_temp_dir("qemu_path");
    auto exePath = tmpDir / "qemu-system-arm";
    touch_file(exePath);

    // Ensure QEMU_ROOT is not set and prepend our dir to PATH
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", "");
    // Prepend tmpDir to existing PATH
    const char* oldPath = getenv("PATH");
    std::string newPath = tmpDir.string();
    if (oldPath) {
        newPath += ":";
        newPath += oldPath;
    }
    MacOSEnvVarGuard guardPath("PATH", newPath);

    // Act
    qemu::Launcher launcher("qemu-system-arm");

    // Assert
    EXPECT_FALSE(launcher.qemuPath().empty());
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(MacOSFindQemuExecutablePath, HandlesColonSeparatedPath) {
    // Arrange: create multiple temp dirs with different fake exes
    auto pathDir1 = make_temp_dir("qemu_path1");
    auto pathDir2 = make_temp_dir("qemu_path2");
    auto exePath = pathDir2 / "qemu-system-riscv64";
    touch_file(exePath);

    // Ensure QEMU_ROOT is not set and set PATH with multiple directories
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", "");
    std::string newPath = pathDir1.string() + ":" + pathDir2.string();
    MacOSEnvVarGuard guardPath("PATH", newPath);

    // Act
    qemu::Launcher launcher("qemu-system-riscv64");

    // Assert: should find in second directory
    EXPECT_FALSE(launcher.qemuPath().empty());
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(MacOSFindQemuExecutablePath, HandlesEmptyPathSegments) {
    // Arrange: create temp dir with fake qemu exe
    auto tmpDir = make_temp_dir("qemu_empty_seg");
    auto exePath = tmpDir / "qemu-system-mips";
    touch_file(exePath);

    // Test PATH with empty segments (::) which should be treated as current directory
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", "");
    std::string pathWithEmpty = tmpDir.string() + "::";
    MacOSEnvVarGuard guardPath("PATH", pathWithEmpty);

    // Act
    qemu::Launcher launcher("qemu-system-mips");

    // Assert
    EXPECT_FALSE(launcher.qemuPath().empty());
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(MacOSFindQemuExecutable, EnvTakesPrecedenceOverPath) {
    // Arrange: create two temp dirs with different fake exes
    auto envDir = make_temp_dir("qemu_env_prec");
    auto envExe = envDir / "qemu-system-arm";
    touch_file(envExe);

    auto pathDir = make_temp_dir("qemu_path_prec");
    auto pathExe = pathDir / "qemu-system-arm";
    touch_file(pathExe);

    // Set both QEMU_ROOT and PATH
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", envDir.string());
    MacOSEnvVarGuard guardPath("PATH", pathDir.string());

    // Act
    qemu::Launcher launcher("qemu-system-arm");

    // Assert: QEMU_ROOT should take precedence
    EXPECT_FALSE(launcher.qemuPath().empty());
    EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), envExe));
}

TEST(MacOSFindQemuExecutable, ReturnsEmptyWhenNotFound) {
    // Arrange: clear environment and set empty PATH
    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", "");
    MacOSEnvVarGuard guardPath("PATH", "");

    // Act
    qemu::Launcher launcher("qemu-system-nonexistent");

    // Assert: no detection
    EXPECT_TRUE(launcher.qemuPath().empty());
}

TEST(MacOSFindQemuExecutable, HandlesNonExecutableFile) {
    // Arrange: create temp dir with non-executable file
    auto tmpDir = make_temp_dir("qemu_non_exec");
    auto filePath = tmpDir / "qemu-system-x86_64";
    
    // Create file but don't make it executable
    std::ofstream ofs(filePath);
    ofs << "not executable";
    ofs.close();
    // Don't set execute permissions

    MacOSEnvVarGuard guardQemuRoot("QEMU_ROOT", tmpDir.string());
    MacOSEnvVarGuard guardPath("PATH", "");

    // Act
    qemu::Launcher launcher("qemu-system-x86_64");

    // Assert: should not find non-executable file
    EXPECT_TRUE(launcher.qemuPath().empty());
}

TEST(LauncherStartValidationMacOS, RequiresValidPaths) {
    qemu::Launcher launcher("qemu-system-x86_64");

    // Override any auto-detected path to simulate missing exe
    launcher.setQemuPath("");
    launcher.setBios("");
    EXPECT_FALSE(launcher.start()); // Missing qemu path

    // Now set qemu path to a temp file but BIOS missing
    auto tmpDir = make_temp_dir("qemu_start");
    auto exePath = tmpDir / "qemu-system-x86_64";
    touch_file(exePath);
    launcher.setQemuPath(exePath.string());
    EXPECT_FALSE(launcher.start()); // BIOS not set

    // Set BIOS path but file doesn't exist
    auto biosPath = tmpDir / "bios.bin";
    launcher.setBios(biosPath.string());
    EXPECT_FALSE(launcher.start()); // BIOS file missing

    // Create BIOS file and expect start() to pass validation
    touch_file(biosPath);
    EXPECT_TRUE(launcher.start());
}

TEST(LauncherBasicFunctionalityMacOS, SettersAndGetters) {
    qemu::Launcher launcher("qemu-system-x86_64");

    // Test setQemuPath/qemuPath
    std::string testPath = "/usr/local/bin/qemu-system-x86_64";
    launcher.setQemuPath(testPath);
    EXPECT_EQ(launcher.qemuPath(), testPath);

    // Test setBios/bios
    std::string testBios = "/usr/local/share/qemu/bios.bin";
    launcher.setBios(testBios);
    EXPECT_EQ(launcher.bios(), testBios);

    // Test addArgument/arguments
    launcher.addArgument("-m");
    launcher.addArgument("1024");
    launcher.addArgument("-smp");
    launcher.addArgument("4");

    auto args = launcher.arguments();
    ASSERT_EQ(args.size(), 4);
    EXPECT_EQ(args[0], "-m");
    EXPECT_EQ(args[1], "1024");
    EXPECT_EQ(args[2], "-smp");
    EXPECT_EQ(args[3], "4");
}

TEST(LauncherCallbacksMacOS, CallbackRegistration) {
    qemu::Launcher launcher("qemu-system-x86_64");

    bool stdoutCalled = false;
    bool stderrCalled = false;
    bool serialCalled = false;
    bool exitCalled = false;

    // Register callbacks - should not throw
    EXPECT_NO_THROW(launcher.onStdOut([&stdoutCalled](const std::string& msg) {
        stdoutCalled = true;
    }));

    EXPECT_NO_THROW(launcher.onStdErr([&stderrCalled](const std::string& msg) {
        stderrCalled = true;
    }));

    EXPECT_NO_THROW(launcher.onSerial([&serialCalled](const std::string& msg) {
        serialCalled = true;
    }));

    EXPECT_NO_THROW(launcher.onExit([&exitCalled](int exitCode) {
        exitCalled = true;
    }));
}

TEST(MacOSEdgeCases, EmptySystemName) {
    qemu::Launcher launcher("");
    EXPECT_TRUE(launcher.qemuPath().empty());
}

TEST(MacOSEdgeCases, SystemNameWithSpecialCharacters) {
    qemu::Launcher launcher("qemu-system-../../../etc/passwd");
    EXPECT_TRUE(launcher.qemuPath().empty());
}

TEST(MacOSEdgeCases, VeryLongSystemName) {
    // Use a long name that should be safe - 250 characters
    std::string longName(250, 'a');
    
    // The function should handle long names gracefully without throwing
    qemu::Launcher launcher(longName);
    
    // It should have empty path since the executable doesn't exist
    EXPECT_TRUE(launcher.qemuPath().empty());
}