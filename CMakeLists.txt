cmake_minimum_required(VERSION 3.31.0)
project(lib-qemu-launcher VERSION 0.1.0 LANGUAGES C CXX)
include(GNUInstallDirs)
include(FetchContent)
include(GoogleTest)
include(CTest)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_TESTING "Build testing" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)

message(STATUS "Building with options:")
message(STATUS "  BUILD_SHARED_LIBS: ${BUILD_SHARED_LIBS}")
message(STATUS "  BUILD_TESTING: ${BUILD_TESTING}")
message(STATUS "  BUILD_EXAMPLES: ${BUILD_EXAMPLES}")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "  CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  CMAKE_HOST_SYSTEM_NAME: ${CMAKE_HOST_SYSTEM_NAME}")
message(STATUS "  CMAKE_SYSTEM_NAME: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  CMAKE_SYSTEM_VERSION: ${CMAKE_SYSTEM_VERSION}")
message(STATUS "  CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
message(STATUS "  CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "  CMAKE_INSTALL_PREFIX: ${CMAKE_INSTALL_PREFIX}")

# Create the qemu_launcher library
add_library(qemu_launcher STATIC 
    $<$<PLATFORM_ID:Windows>:src/qemu_launcher_windows.cpp>
    $<$<PLATFORM_ID:Linux>:src/qemu_launcher_linux.cpp>
    $<$<PLATFORM_ID:Darwin>:src/qemu_launcher_macos.cpp>
)

# Set include directories
target_include_directories(qemu_launcher PUBLIC ${CMAKE_SOURCE_DIR}/include)

# Add the examples directory
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Add the tests directory
if(BUILD_TESTING)
    # Ensure CTest is active at the top level so ctest in the build root sees tests
    enable_testing()
    add_subdirectory(tests)
endif()