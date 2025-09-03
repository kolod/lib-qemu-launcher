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
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    add_executable(qemu_launcher_tests test_qemu_launcher_windows.cpp)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    add_executable(qemu_launcher_tests test_qemu_launcher_macos.cpp)
else()
    add_executable(qemu_launcher_tests test_qemu_launcher_linux.cpp)
endif()

# Link with the qemu_launcher library
target_link_libraries(qemu_launcher_tests PRIVATE qemu_launcher)
target_link_libraries(qemu_launcher_tests PRIVATE gtest_main)

# # Enable Google Test
# if(WIN32)
#     # Copy runtime DLLs next to the test exe for discovery to work with MinGW
#     add_custom_command(TARGET qemu_launcher_tests POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_if_different
#         $<TARGET_RUNTIME_DLLS:qemu_launcher_tests>
#         $<TARGET_FILE_DIR:qemu_launcher_tests>
#     COMMAND_EXPAND_LISTS)
# endif()

gtest_discover_tests(qemu_launcher_tests)
