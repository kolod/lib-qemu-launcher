// Copyright (c) 2025 Oleksandr Kolodkin <oleksandr.kolodkin@example.com>
//
// Licensed under the MIT License. 
// See LICENSE file in the project root for full license information.

#include <gtest/gtest.h>
#include <qemu/launcher.h>
// Windows-specific test helpers
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <fstream>

namespace {

// RAII environment variable setter/restorer for Windows (wide API)
class WindowsEnvVarGuard {
public:
	WindowsEnvVarGuard(const wchar_t* name, const std::wstring& value) : name_(name) {
		// Save original
		DWORD needed = GetEnvironmentVariableW(name_, nullptr, 0);
		if (needed) {
			original_.resize(needed);
			DWORD written = GetEnvironmentVariableW(name_, original_.data(), needed);
			if (written > 0 && written < needed) {
				original_.resize(written);
				hadOriginal_ = true;
			} else {
				original_.clear();
			}
		}
		// Set new value
		SetEnvironmentVariableW(name_, value.c_str());
	}

	~WindowsEnvVarGuard() {
		if (hadOriginal_) {
			SetEnvironmentVariableW(name_, original_.c_str());
		} else {
			// Unset
			SetEnvironmentVariableW(name_, nullptr);
		}
	}

private:
	const wchar_t* name_;
	bool hadOriginal_ = false;
	std::wstring original_;
};

// Create a unique temporary directory and return its path
std::filesystem::path make_temp_dir(const std::wstring& prefix) {
	auto base = std::filesystem::temp_directory_path();
	for (int i = 0; i < 100; ++i) {
		auto candidate = base / (prefix + L"_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(i));
		std::error_code ec;
		if (std::filesystem::create_directory(candidate, ec)) {
			return candidate;
		}
	}
	throw std::runtime_error("Failed to create temporary directory");
}

void touch_file(const std::filesystem::path& p) {
	std::ofstream ofs(p, std::ios::binary);
	ofs << "\x4D\x5A"; // Write 'MZ' header bytes (optional)
}

} // namespace

TEST(WindowsFindQemuExecutableEnv, PicksFromQemuRoot) {
	// Arrange: create temp dir with fake qemu exe
	auto tmpDir = make_temp_dir(L"qemu_env");
	auto exePath = tmpDir / L"qemu-system-x86_64.exe";
	touch_file(exePath);

	// Set QEMU_ROOT to tempDir and ensure PATH doesn't interfere
	WindowsEnvVarGuard guardQemuRoot(L"QEMU_ROOT", tmpDir.wstring());
	WindowsEnvVarGuard guardPath(L"PATH", L"");

	// Act
	qemu::Launcher launcher("qemu-system-x86_64");

	// Assert
	EXPECT_FALSE(launcher.qemuPath().empty());
	EXPECT_TRUE(std::filesystem::exists(launcher.qemuPath()));
	EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(WindowsFindQemuExecutablePath, PicksFromPathWhenEnvMissing) {
	// Arrange: create temp dir with fake qemu exe
	auto tmpDir = make_temp_dir(L"qemu_path");
	auto exePath = tmpDir / L"qemu-system-arm.exe";
	touch_file(exePath);

	// Ensure QEMU_ROOT is not set and prepend our dir to PATH
	WindowsEnvVarGuard guardQemuRoot(L"QEMU_ROOT", L"");
	// Prepend tmpDir to existing PATH
	DWORD needed = GetEnvironmentVariableW(L"PATH", nullptr, 0);
	std::wstring newPath = tmpDir.wstring();
	if (needed) {
		std::wstring oldPath(needed, L'\0');
		DWORD written = GetEnvironmentVariableW(L"PATH", oldPath.data(), needed);
		if (written > 0 && written < needed) {
			oldPath.resize(written);
			newPath.append(L";").append(oldPath);
		}
	}
	WindowsEnvVarGuard guardPath(L"PATH", newPath);

	// Act
	qemu::Launcher launcher("qemu-system-arm");

	// Assert
	EXPECT_FALSE(launcher.qemuPath().empty());
	EXPECT_TRUE(std::filesystem::equivalent(std::filesystem::path(launcher.qemuPath()), exePath));
}

TEST(LauncherStartValidation, RequiresValidPaths) {
	qemu::Launcher launcher("qemu-system-x86_64");

	// Override any auto-detected path to simulate missing exe
	launcher.setQemuPath("");
	launcher.setBios("");
	EXPECT_FALSE(launcher.start()); // Missing qemu path

	// Now set qemu path to a temp file but BIOS missing
	auto tmpDir = make_temp_dir(L"qemu_start");
	auto exePath = tmpDir / L"qemu-system-x86_64.exe";
	touch_file(exePath);
	launcher.setQemuPath(exePath.string());
	EXPECT_FALSE(launcher.start()); // BIOS not set

	// Set BIOS path but file doesn't exist
	auto biosPath = tmpDir / L"bios.bin";
	launcher.setBios(biosPath.string());
	EXPECT_FALSE(launcher.start()); // BIOS file missing

	// Create BIOS file and expect start() to pass validation
	touch_file(biosPath);
	EXPECT_TRUE(launcher.start());
}

