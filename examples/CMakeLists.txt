add_executable(qemu_launcher_example
    qemu_launcher_example.cpp
)

# Link with the qemu_launcher library
target_link_libraries(qemu_launcher_example PRIVATE qemu_launcher)
