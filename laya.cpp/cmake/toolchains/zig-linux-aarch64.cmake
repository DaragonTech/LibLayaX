# Cross-compile Linux ARM64 (aarch64, glibc >= 2.28) binaries from any host with Zig.
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-linux-aarch64.cmake ...
# Baseline: ARMv8.0-A with NEON (Raspberry Pi 4/5, AWS Graviton, Ampere, ...). libc++ is linked in.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER   ${CMAKE_CURRENT_LIST_DIR}/zig/zigcc-aarch64-linux)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zigcxx-aarch64-linux)
set(CMAKE_AR           ${CMAKE_CURRENT_LIST_DIR}/zig/zigar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${CMAKE_CURRENT_LIST_DIR}/zig/zigranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# Run cross-built tests through QEMU user emulation when available.
find_program(QEMU_AARCH64 NAMES qemu-aarch64-static qemu-aarch64)
if(QEMU_AARCH64 AND EXISTS /usr/aarch64-linux-gnu)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${QEMU_AARCH64} -L /usr/aarch64-linux-gnu)
endif()
