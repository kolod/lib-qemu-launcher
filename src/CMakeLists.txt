
# Source files
set(SOURCES qemu_launcher.cpp)

# Platform-specific source files
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND SOURCES qemu_launcher_windows.cpp)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND SOURCES qemu_launcher_macos.cpp)
else()
    list(APPEND SOURCES qemu_launcher_linux.cpp)
endif()

# Create the qemu_launcher library
add_library(qemu_launcher ${SOURCES})

# Set include directories
target_include_directories(qemu_launcher PUBLIC ${CMAKE_SOURCE_DIR}/include)
