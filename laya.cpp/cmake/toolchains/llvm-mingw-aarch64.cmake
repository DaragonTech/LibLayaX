# Cross-compile native Windows ARM64 binaries with llvm-mingw (https://github.com/mstorsjo/llvm-mingw),
# the clang/lld/libc++ MinGW toolchain that is tested on real Windows-on-ARM machines.
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/llvm-mingw-aarch64.cmake -DLLVM_MINGW=/path/to/llvm-mingw ...
# (or put llvm-mingw's bin directory on PATH). -static folds libc++ and libunwind into each binary,
# leaving only Windows system DLLs (kernel32, the Universal CRT) as dependencies.
if(NOT LLVM_MINGW AND DEFINED ENV{LLVM_MINGW})
  set(LLVM_MINGW "$ENV{LLVM_MINGW}")
endif()
set(LLVM_MINGW "${LLVM_MINGW}" CACHE PATH "llvm-mingw installation directory")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW)
if(LLVM_MINGW)
  set(_llvm_mingw_bin "${LLVM_MINGW}/bin/")
endif()
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(CMAKE_C_COMPILER   ${_llvm_mingw_bin}aarch64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${_llvm_mingw_bin}aarch64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER  ${_llvm_mingw_bin}aarch64-w64-mingw32-windres)
# Windows on ARM starts at Windows 10 (cpp-httplib requires its API level).
add_compile_definitions(_WIN32_WINNT=0x0A00)
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
