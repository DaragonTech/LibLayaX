# Cross-compile macOS binaries (liblaya.dylib) from Linux with Zig (pip install ziglang).
#   ZIG=/path/to/zig  LAYA_MACOS_ARCH=arm64|x86_64  cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/zig-macos.cmake ...
# Zig bundles the macOS libSystem stubs and libc++, so no Apple SDK is needed. Frameworks
# (Metal, Accelerate, CoreML) are not available this way: these are CPU builds.
set(CMAKE_SYSTEM_NAME Darwin)
if(NOT DEFINED LAYA_MACOS_ARCH)
  set(LAYA_MACOS_ARCH "$ENV{LAYA_MACOS_ARCH}")
endif()
if(LAYA_MACOS_ARCH STREQUAL "x86_64")
  set(CMAKE_SYSTEM_PROCESSOR x86_64)
  set(_zig_arch x86_64)
else()
  set(CMAKE_SYSTEM_PROCESSOR arm64)
  set(_zig_arch aarch64)
endif()
set(CMAKE_OSX_ARCHITECTURES "" CACHE STRING "" FORCE)  # zig takes the arch from ZIG_TARGET
set(CMAKE_OSX_SYSROOT "" CACHE PATH "" FORCE)          # no SDK; zig provides libSystem
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)  # also enables CMake's @rpath support
# Per-architecture wrapper names carry the target, so every step (compile and link) gets it.
set(CMAKE_C_COMPILER   ${CMAKE_CURRENT_LIST_DIR}/zig/zigcc-${_zig_arch}-macos)
set(CMAKE_CXX_COMPILER ${CMAKE_CURRENT_LIST_DIR}/zig/zigcxx-${_zig_arch}-macos)
set(CMAKE_AR           ${CMAKE_CURRENT_LIST_DIR}/zig/zigar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${CMAKE_CURRENT_LIST_DIR}/zig/zigranlib CACHE FILEPATH "")
set(CMAKE_INSTALL_NAME_TOOL true CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
# Apple Silicon baseline: M1 (zig names CPUs, so -mcpu rather than ggml's -march).
if(_zig_arch STREQUAL "aarch64")
  set(CMAKE_C_FLAGS_INIT   "-mcpu=apple_m1")
  set(CMAKE_CXX_FLAGS_INIT "-mcpu=apple_m1")
endif()
# CMake learns the macOS version from sw_vers, which a Linux host lacks, and then withholds
# @rpath support. Restore the flags it uses for any macOS since 10.5, after its platform files.
set(CMAKE_USER_MAKE_RULES_OVERRIDE ${CMAKE_CURRENT_LIST_DIR}/zig-macos-rules.cmake)
