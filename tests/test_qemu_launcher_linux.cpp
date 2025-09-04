// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <gtest/gtest.h>
#include <qemu/launcher.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <unistd.h>
#include <sys/stat.h>

// Forward declarations for internal functions we want to test
namespace qemu {
    std::string getExePathIfExists(const std::string& directory, const std::string& system);
    std::string findQemuExecutableEnv(const std::string& system);
    std::string findQemuExecutablePath(const std::string& system);
    std::string findQemuExecutable(const std::string& system);
}

class QemuLinuxTest : public ::testing::Test {
protected:
    std::string tempDir;
    std::string originalPath;
    std::string originalQemuRoot;
    
    void SetUp() override {
        // Create temporary directory for tests
        char tempTemplate[] = "/tmp/qemu_test_XXXXXX";
        tempDir = mkdtemp(tempTemplate);
        ASSERT_FALSE(tempDir.empty()) << "Failed to create temp directory";
        
        // Save original environment variables
        const char* path = getenv("PATH");
        if (path) originalPath = path;
        
        const char* qemuRoot = getenv("QEMU_ROOT");
        if (qemuRoot) originalQemuRoot = qemuRoot;
    }
    
    void TearDown() override {
        // Restore environment variables
        if (!originalPath.empty()) {
            setenv("PATH", originalPath.c_str(), 1);
        }
        if (!originalQemuRoot.empty()) {
            setenv("QEMU_ROOT", originalQemuRoot.c_str(), 1);
        } else {
            unsetenv("QEMU_ROOT");
        }
        
        // Clean up temp directory
        if (!tempDir.empty()) {
            std::filesystem::remove_all(tempDir);
        }
    }
    
    void createMockExecutable(const std::string& path, const std::string& name) {
        std::string fullPath = path + "/" + name;
        std::ofstream file(fullPath);
        file << "#!/bin/bash\necho 'Mock QEMU executable'\n";
        file.close();
        chmod(fullPath.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    }
};

// Test getExePathIfExists function
TEST_F(QemuLinuxTest, GetExePathIfExists_FileExists) {
    std::string system = "qemu-system-x86_64";
    createMockExecutable(tempDir, system);
    
    std::string result = qemu::getExePathIfExists(tempDir, system);
    EXPECT_EQ(result, tempDir + "/" + system);
}

TEST_F(QemuLinuxTest, GetExePathIfExists_FileDoesNotExist) {
    std::string system = "qemu-system-arm";
    
    std::string result = qemu::getExePathIfExists(tempDir, system);
    EXPECT_TRUE(result.empty());
}

TEST_F(QemuLinuxTest, GetExePathIfExists_DirectoryDoesNotExist) {
    std::string nonExistentDir = "/this/directory/does/not/exist";
    std::string system = "qemu-system-x86_64";
    
    std::string result = qemu::getExePathIfExists(nonExistentDir, system);
    EXPECT_TRUE(result.empty());
}

// Test findQemuExecutableEnv function
TEST_F(QemuLinuxTest, FindQemuExecutableEnv_QemuRootSet) {
    std::string system = "qemu-system-x86_64";
    createMockExecutable(tempDir, system);
    
    setenv("QEMU_ROOT", tempDir.c_str(), 1);
    
    std::string result = qemu::findQemuExecutableEnv(system);
    EXPECT_EQ(result, tempDir + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutableEnv_QemuRootNotSet) {
    unsetenv("QEMU_ROOT");
    
    std::string result = qemu::findQemuExecutableEnv("qemu-system-x86_64");
    EXPECT_TRUE(result.empty());
}

TEST_F(QemuLinuxTest, FindQemuExecutableEnv_QemuRootSetButFileNotFound) {
    setenv("QEMU_ROOT", tempDir.c_str(), 1);
    
    std::string result = qemu::findQemuExecutableEnv("qemu-system-nonexistent");
    EXPECT_TRUE(result.empty());
}

// Test findQemuExecutablePath function
TEST_F(QemuLinuxTest, FindQemuExecutablePath_FileInPath) {
    std::string system = "qemu-system-x86_64";
    std::string binDir = tempDir + "/bin";
    std::filesystem::create_directory(binDir);
    createMockExecutable(binDir, system);
    
    std::string newPath = binDir + ":" + originalPath;
    setenv("PATH", newPath.c_str(), 1);
    
    std::string result = qemu::findQemuExecutablePath(system);
    EXPECT_EQ(result, binDir + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutablePath_FileInMultiplePaths) {
    std::string system = "qemu-system-x86_64";
    std::string binDir1 = tempDir + "/bin1";
    std::string binDir2 = tempDir + "/bin2";
    std::filesystem::create_directory(binDir1);
    std::filesystem::create_directory(binDir2);
    
    // Create executable in second directory
    createMockExecutable(binDir2, system);
    
    std::string newPath = binDir1 + ":" + binDir2 + ":" + originalPath;
    setenv("PATH", newPath.c_str(), 1);
    
    std::string result = qemu::findQemuExecutablePath(system);
    EXPECT_EQ(result, binDir2 + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutablePath_PathNotSet) {
    unsetenv("PATH");
    
    std::string result = qemu::findQemuExecutablePath("qemu-system-x86_64");
    EXPECT_TRUE(result.empty());
}

TEST_F(QemuLinuxTest, FindQemuExecutablePath_FileNotInPath) {
    std::string binDir = tempDir + "/bin";
    std::filesystem::create_directory(binDir);
    
    setenv("PATH", binDir.c_str(), 1);
    
    std::string result = qemu::findQemuExecutablePath("qemu-system-nonexistent");
    EXPECT_TRUE(result.empty());
}

// Test findQemuExecutable function (combines env and path search)
TEST_F(QemuLinuxTest, FindQemuExecutable_FoundInQemuRoot) {
    std::string system = "qemu-system-x86_64";
    createMockExecutable(tempDir, system);
    
    setenv("QEMU_ROOT", tempDir.c_str(), 1);
    
    std::string result = qemu::findQemuExecutable(system);
    EXPECT_EQ(result, tempDir + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutable_FoundInPath) {
    std::string system = "qemu-system-x86_64";
    std::string binDir = tempDir + "/bin";
    std::filesystem::create_directory(binDir);
    createMockExecutable(binDir, system);
    
    unsetenv("QEMU_ROOT");
    std::string newPath = binDir + ":" + originalPath;
    setenv("PATH", newPath.c_str(), 1);
    
    std::string result = qemu::findQemuExecutable(system);
    EXPECT_EQ(result, binDir + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutable_PrioritizesQemuRootOverPath) {
    std::string system = "qemu-system-x86_64";
    std::string qemuRootDir = tempDir + "/qemu_root";
    std::string pathDir = tempDir + "/path_dir";
    
    std::filesystem::create_directory(qemuRootDir);
    std::filesystem::create_directory(pathDir);
    
    createMockExecutable(qemuRootDir, system);
    createMockExecutable(pathDir, system);
    
    setenv("QEMU_ROOT", qemuRootDir.c_str(), 1);
    std::string newPath = pathDir + ":" + originalPath;
    setenv("PATH", newPath.c_str(), 1);
    
    std::string result = qemu::findQemuExecutable(system);
    EXPECT_EQ(result, qemuRootDir + "/" + system);
}

TEST_F(QemuLinuxTest, FindQemuExecutable_NotFound) {
    unsetenv("QEMU_ROOT");
    setenv("PATH", tempDir.c_str(), 1);
    
    std::string result = qemu::findQemuExecutable("qemu-system-nonexistent");
    EXPECT_TRUE(result.empty());
}

// Test Launcher class basic functionality
class QemuLauncherTest : public ::testing::Test {
protected:
    void SetUp() override {
        launcher = std::make_unique<qemu::Launcher>("qemu-system-x86_64");
    }
    
    std::unique_ptr<qemu::Launcher> launcher;
};

TEST_F(QemuLauncherTest, Constructor_SetsSystem) {
    // The constructor should initialize with the system type
    EXPECT_NO_THROW(qemu::Launcher("qemu-system-arm"));
    EXPECT_NO_THROW(qemu::Launcher("qemu-system-riscv64"));
}

TEST_F(QemuLauncherTest, SetQemuPath) {
    std::string testPath = "/usr/bin/qemu-system-x86_64";
    launcher->setQemuPath(testPath);
    EXPECT_EQ(launcher->qemuPath(), testPath);
}

TEST_F(QemuLauncherTest, SetBios) {
    std::string testBios = "/path/to/bios.bin";
    launcher->setBios(testBios);
    EXPECT_EQ(launcher->bios(), testBios);
}

TEST_F(QemuLauncherTest, AddArgument) {
    launcher->addArgument("-m");
    launcher->addArgument("512");
    launcher->addArgument("-smp");
    launcher->addArgument("2");
    
    auto args = launcher->arguments();
    EXPECT_EQ(args.size(), 4);
    EXPECT_EQ(args[0], "-m");
    EXPECT_EQ(args[1], "512");
    EXPECT_EQ(args[2], "-smp");
    EXPECT_EQ(args[3], "2");
}

TEST_F(QemuLauncherTest, CallbackRegistration) {
    bool stdoutCalled = false;
    bool stderrCalled = false;
    bool serialCalled = false;
    bool exitCalled = false;
    
    launcher->onStdOut([&stdoutCalled](const std::string& msg) {
        stdoutCalled = true;
    });
    
    launcher->onStdErr([&stderrCalled](const std::string& msg) {
        stderrCalled = true;
    });
    
    launcher->onSerial([&serialCalled](const std::string& msg) {
        serialCalled = true;
    });
    
    launcher->onExit([&exitCalled](int exitCode) {
        exitCalled = true;
    });
    
    // Callbacks should be registered without throwing
    EXPECT_NO_THROW(launcher->onStdOut([](const std::string&) {}));
    EXPECT_NO_THROW(launcher->onStdErr([](const std::string&) {}));
    EXPECT_NO_THROW(launcher->onSerial([](const std::string&) {}));
    EXPECT_NO_THROW(launcher->onExit([](int) {}));
}

// Edge case tests
TEST(QemuLinuxEdgeCaseTest, EmptySystemName) {
    std::string result = qemu::findQemuExecutable("");
    EXPECT_TRUE(result.empty());
}

TEST(QemuLinuxEdgeCaseTest, SystemNameWithSpecialCharacters) {
    std::string result = qemu::findQemuExecutable("qemu-system-../../../etc/passwd");
    EXPECT_TRUE(result.empty());
}

TEST(QemuLinuxEdgeCaseTest, VeryLongSystemName) {
    std::string longName(1000, 'a');
    std::string result = qemu::findQemuExecutable(longName);
    EXPECT_TRUE(result.empty());
}