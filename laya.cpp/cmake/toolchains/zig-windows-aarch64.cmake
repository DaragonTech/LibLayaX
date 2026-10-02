# Cross-compile native Windows ARM64 binaries from any host with Zig (bundled MinGW-w64 + libc++).
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-windows-aarch64.cmake ...
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(CMAKE_C_COMPILER   ${CMAKE_CURRENT_LIST_DIR}/zig/zigcc-aarch64-windows)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zigcxx-aarch64-windows)
set(CMAKE_AR           ${CMAKE_CURRENT_LIST_DIR}/zig/zigar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${CMAKE_CURRENT_LIST_DIR}/zig/zigranlib CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
