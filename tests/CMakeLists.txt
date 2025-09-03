enable_testing()

include(CTest)
include(GoogleTest)

include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(qemu_launcher_tests
    test_qemu_launcher.cpp
)

# Link with the qemu_launcher library
target_link_libraries(qemu_launcher_tests PRIVATE qemu_launcher GTest::gtest_main)

# Enable Google Test
gtest_discover_tests(qemu_launcher_tests)
