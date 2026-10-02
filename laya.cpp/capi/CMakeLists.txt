# Flat C ABI shared library over laya.cpp: laya.dll (Windows), liblaya.so (Linux), liblaya.dylib (macOS).
# Enable from the top level with -DLAYA_C_API=ON. Build ggml statically (-DBUILD_SHARED_LIBS=OFF)
# to get a single self-contained library.
set(LAYA_C_VERSION "1.0.14" CACHE STRING "Version string reported by laya_version()")

add_library(laya_c SHARED laya_c.cpp)
target_include_directories(laya_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(laya_c PRIVATE laya)
target_compile_features(laya_c PRIVATE cxx_std_20)
target_compile_definitions(laya_c PRIVATE LAYA_C_VERSION="${LAYA_C_VERSION}"
  $<$<BOOL:${LAYA_CUDA}>:LAYA_CUDA=1> $<$<BOOL:${LAYA_VULKAN}>:LAYA_VULKAN=1>)
# Linux Vulkan builds can open the system Vulkan loader lazily (pass the shim as Vulkan_LIBRARY, see
# scripts/capi/build-linux.sh); the library then loads on machines without Vulkan.
option(LAYA_VULKAN_LAZY "Vulkan_LIBRARY is the lazy-loading shim (capi/vulkan_lazy.c)" OFF)
if(LAYA_VULKAN_LAZY)
  target_compile_definitions(laya_c PRIVATE LAYA_VULKAN_LAZY=1)
endif()
# Tell the library which instruction-set baseline ggml was compiled for, so it can refuse to load
# with a clear message on an older CPU instead of crashing on an illegal instruction.
foreach(feature AVX AVX2 FMA F16C BMI2 AVX512F)
  if(GGML_${feature} AND NOT GGML_NATIVE)
    target_compile_definitions(laya_c PRIVATE LAYA_REQUIRE_${feature}=1)
  endif()
endforeach()
target_compile_options(laya_c PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/W4> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra>)
set_target_properties(laya_c PROPERTIES
  OUTPUT_NAME laya
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(WIN32)
  set_target_properties(laya_c PROPERTIES PREFIX "")  # laya.dll, not liblaya.dll
endif()
# MinGW GCC needs its runtime folded in explicitly; clang/zig for Windows links libc++ statically
# already and exports only the dllexport functions.
set(LAYA_MINGW_GCC OFF)
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(LAYA_MINGW_GCC ON)
endif()
if(LAYA_MINGW_GCC)
  # A Delphi/.NET host has no MinGW runtime: fold libgcc, libstdc++ and winpthread into the DLL.
  target_link_options(laya_c PRIVATE -static-libgcc -static-libstdc++ -static -Wl,--exclude-all-symbols)
elseif(WIN32)
elseif(APPLE)
  # Found next to the app via @rpath (e.g. Contents/MacOS or Contents/Frameworks), exporting only
  # the C API. Apple's linker honours the list; the zig cross build relinks with ld64.lld
  # (scripts/capi/build-macos.sh) because zig's linker ignores it.
  set_target_properties(laya_c PROPERTIES INSTALL_NAME_DIR "@rpath" BUILD_WITH_INSTALL_NAME_DIR ON)
  target_link_options(laya_c PRIVATE -Wl,-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/macos-exports.txt -Wl,-dead_strip)
elseif(UNIX)
  # A version script (rather than --exclude-libs) hides everything but the C API with GNU ld,
  # lld and zig alike, including symbols of the statically linked C++ runtime and ICU.
  target_link_options(laya_c PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/linux-exports.map -Wl,--no-undefined)
endif()

if(BUILD_TESTING)
  add_executable(test-capi ${CMAKE_SOURCE_DIR}/tests/capi/test_capi.c)
  set_target_properties(test-capi PROPERTIES C_STANDARD 11 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_link_libraries(test-capi PRIVATE laya_c)
  if(LAYA_MINGW_GCC)
    target_link_options(test-capi PRIVATE -static)
  elseif(WIN32)
  elseif(APPLE)
    set_target_properties(test-capi PROPERTIES BUILD_RPATH "@executable_path")
  elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(test-capi PRIVATE Threads::Threads m)
  endif()
  add_executable(laya-bench ${CMAKE_SOURCE_DIR}/tests/capi/bench_capi.c)
  set_target_properties(laya-bench PROPERTIES C_STANDARD 11 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_link_libraries(laya-bench PRIVATE laya_c)
  if(LAYA_MINGW_GCC)
    target_link_options(laya-bench PRIVATE -static)
  elseif(WIN32)
  elseif(APPLE)
    set_target_properties(laya-bench PROPERTIES BUILD_RPATH "@executable_path")
  endif()
  # Stand-alone check of the C++ runtime features the library relies on (no laya.dll, no model).
  add_executable(laya-probe ${CMAKE_SOURCE_DIR}/tests/capi/probe.cpp)
  set_target_properties(laya-probe PROPERTIES CXX_STANDARD 20 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  if(LAYA_MINGW_GCC)
    target_link_options(laya-probe PRIVATE -static)
  elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(laya-probe PRIVATE Threads::Threads)
  endif()
  add_executable(laya-diag ${CMAKE_SOURCE_DIR}/tests/capi/diag_capi.c)
  set_target_properties(laya-diag PROPERTIES C_STANDARD 11 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
  target_link_libraries(laya-diag PRIVATE laya_c)
  if(LAYA_MINGW_GCC)
    target_link_options(laya-diag PRIVATE -static)
  elseif(WIN32)
  elseif(APPLE)
    set_target_properties(laya-diag PROPERTIES BUILD_RPATH "@executable_path")
  endif()
  if(NOT WIN32)  # uses dirent/setenv
    add_executable(laya-trace ${CMAKE_SOURCE_DIR}/tests/capi/trace_capi.c)
    set_target_properties(laya-trace PROPERTIES C_STANDARD 11 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    target_link_libraries(laya-trace PRIVATE laya_c)
    if(APPLE)
      set_target_properties(laya-trace PROPERTIES BUILD_RPATH "@executable_path")
    else()
      target_link_libraries(laya-trace PRIVATE m)
    endif()
  endif()
  # The model-dependent tests are skipped (exit 77) unless LAYA_TEST_MODEL names a checkpoint.
  add_test(NAME capi COMMAND test-capi)
  set_tests_properties(capi PROPERTIES SKIP_RETURN_CODE 77)
endif()
