# Force static build of gtest/gmock
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE) # optional: don't install
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE) # MSVC: use static CRT

FetchContent_Declare(
    googletest
    GIT_REPOSITORY      https://github.com/google/googletest.git
    GIT_TAG             v1.17.0
)

FetchContent_MakeAvailable(googletest)

# Enforce C++17 for tests
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-specific source files
add_executable(qemu_launcher_tests 
    $<$<PLATFORM_ID:Windows>:test_qemu_launcher_windows.cpp>
    $<$<PLATFORM_ID:Darwin>:test_qemu_launcher_macos.cpp>
    $<$<PLATFORM_ID:Linux>:test_qemu_launcher_linux.cpp>
)

# Link with the qemu_launcher library
target_link_libraries(qemu_launcher_tests PRIVATE qemu_launcher)
target_link_libraries(qemu_launcher_tests PRIVATE gtest_main)

# Discover tests
gtest_discover_tests(qemu_launcher_tests)
