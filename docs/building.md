# Building from source

Most users do not need this page: the binary packages contain the library ready to use. Build
from source to change the library, to target a system the packages do not cover (an older
Linux, for example), or to check how the binaries were made.

All commands are run from the `laya.cpp/` folder of this repository. It contains upstream
laya.cpp at commit `e632da0` with the LibLayaX additions applied, and ggml and cpp-httplib
unmodified in `third_party/`.

## A native build

On the machine the library will run on, this is an ordinary CMake build:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF -DLAYA_CUDA=OFF
cmake --build build --target laya_c test-capi
```

`-DLAYA_C_API=ON` enables the library target (`laya_c`), which is off by default.
`-DBUILD_SHARED_LIBS=OFF` builds ggml statically, so the result is one self-contained file
in `build/bin/`. Add `-DLAYA_VULKAN=ON` for the GPU backend.

It needs CMake, a C++ compiler, ICU and nlohmann-json (on Ubuntu:
`cmake ninja-build g++ libicu-dev nlohmann-json3-dev`). A build made this way depends on the
system's ICU library; the scripts below link ICU in statically, which is what makes the
released binaries self-contained.

## Building every platform from one Linux machine

The released binaries were all cross-built on Ubuntu 24.04 with the scripts in
`scripts/capi/`. Each script downloads and builds ICU statically on first use, then writes
`build-<platform>-<variant>/bin/`.

Common needs: `cmake ninja-build g++ nlohmann-json3-dev curl`.

| Target | Command | Extra tools |
|---|---|---|
| Windows x64, CPU (`avx2` and `compat`) | `scripts/capi/build-windows-mingw.sh` | `mingw-w64` |
| Windows x64, with GPU | `VARIANTS=vulkan VULKAN_HEADERS=… SPIRV_HEADERS=… GLSLC=… VULKAN_DEF=… scripts/capi/build-windows-mingw.sh` | Vulkan-Headers, SPIRV-Headers, shaderc 2025.3 or later |
| Windows ARM64 | `LLVM_MINGW=/path/to/llvm-mingw scripts/capi/build-arm64.sh windows` | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) |
| Linux x86-64, CPU (`avx2` and `compat`) | `scripts/capi/build-linux.sh` | none |
| Linux x86-64, with GPU | `VARIANTS=vulkan VULKAN_HEADERS=… EXTRA_CMAKE_ARGS="-DVulkan_GLSLC_EXECUTABLE=…" scripts/capi/build-linux.sh` | as for Windows with GPU |
| Linux ARM64 | `scripts/capi/build-arm64.sh linux` | `pip install ziglang` |
| macOS arm64 and x86-64, CPU | `scripts/capi/build-macos.sh` | `pip install ziglang`, `lld` |
| macOS arm64, with GPU | `MOLTENVK=…/libMoltenVK.dylib VULKAN_HEADERS=… GLSLC=… scripts/capi/build-macos.sh arm64-vulkan` | a MoltenVK release |

The comment block at the top of each script documents its variables. `RUN_TESTS=1` with
`LAYA_TEST_MODEL=/path/to/model` runs the test suite after the build, through Wine or QEMU
where the target is not the build machine.

Things learnt the hard way:

* **Shader compiler.** The `glslc` that comes with Ubuntu 24.04 (2023.8) crashes on the
  engine's shaders. Use shaderc 2025.3 or later, or the one from the Vulkan SDK 1.4.
* **Windows ARM64 needs llvm-mingw.** A DLL built with Zig 0.16 for that target hung on real
  hardware; the same code built with llvm-mingw works.
* **macOS is built without an Apple SDK**, with Zig as the compiler and `ld64.lld` for the
  final link. That is also why the macOS builds have the CPU and Vulkan backends only: Metal,
  Accelerate and Core ML need Apple's frameworks.

## Visual C++ and CUDA

A workflow for GitHub Actions, `.github/workflows/capi-windows.yml`, builds the library with
Visual C++ in three variants (CPU, Vulkan, CUDA 12). It was written but has never been run.
The local equivalent, from a Visual Studio 2022 x64 prompt with vcpkg:

```
vcpkg install icu:x64-windows-static nlohmann-json:x64-windows-static
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAYA_PORTABLE=ON -DLAYA_C_API=ON ^
  -DLAYA_CUDA=OFF -DLAYA_VULKAN=ON ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --target laya_c
```

## Processor baselines

The x86-64 library is built twice. The baseline is fixed at build time; the library does not
switch at run time.

| Variant | Instructions used | CMake settings |
|---|---|---|
| `avx2` | AVX, AVX2, FMA, F16C, BMI2 | `-DGGML_NATIVE=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_BMI2=ON` |
| `compat` | SSE4.2 | `-DGGML_NATIVE=OFF -DGGML_SSE42=ON` and the others `OFF` |

`-DGGML_NATIVE=OFF` matters: with the default, the compiler targets the build machine's own
processor and the result may not run anywhere else. When a model is loaded, the library
checks that the processor has what the build needs and reports a clear error if not.

The ARM64 builds use the ARMv8.0 baseline with NEON on Linux and Windows, and the Apple M1
baseline on macOS.

## The import library for Windows

The build scripts make `laya.lib` from `capi/laya.def`:

```
llvm-dlltool -m i386:x86-64 -d capi/laya.def -D laya.dll -l laya.lib        (x64)
llvm-dlltool -m arm64       -d capi/laya.def -D laya.dll -l laya.lib        (ARM64)
```

With Visual Studio's tools the same is `lib /def:laya.def /machine:x64 /out:laya.lib`.

## What LibLayaX adds to laya.cpp

46 files, about 3,200 lines. Upstream's own code is touched in four files only
(`CMakeLists.txt`, `include/laya/runtime.hpp`, `src/runtime.cpp`, `src/main.cpp`).

| Path inside `laya.cpp/` | What it is |
|---|---|
| `capi/laya_c.h` | The public header: the whole contract in 94 lines. |
| `capi/laya_c.cpp` | The implementation: error handling, floating-point guard, processor check, crash reporter, debug trace. |
| `capi/CMakeLists.txt` | The `laya_c` target and the tools; enabled with `-DLAYA_C_API=ON`. |
| `capi/vulkan_lazy.c` | Linux: opens the Vulkan loader on first use, so the library loads on machines without Vulkan. |
| `capi/linux-exports.map`, `capi/macos-exports.txt`, `capi/laya.def` | Export lists: only the ten `laya_` functions are visible from outside. |
| `capi/patches/ggml-trace.diff` | Optional patch for ggml that extends the debug trace into the start-up of the CPU engine. |
| `tests/capi/test_capi.c` | The test suite (`test-capi`). |
| `tests/capi/bench_capi.c` | `laya-bench`: load time, speed, and whether a backend gives the reference answers. |
| `tests/capi/diag_capi.c` | `laya-diag`: 21 cases comparing a GPU backend with the CPU. |
| `tests/capi/trace_capi.c` | `laya-trace`: comparison of two backends tensor by tensor. |
| `tests/capi/probe.cpp` | `laya-probe`: checks the C++ runtime features the library needs, without the library. |
| `tests/capi/test_parity.py` | Checks that the library's answers are byte-identical to `laya-cli`. |
| `tests/capi/make_synthetic_model.py` | Writes a model with the real architecture and random weights, for testing without the 800 MB download. |
| `scripts/capi/build-*.sh` | The cross-build scripts above. |
| `scripts/capi/run-tests.bat` | The one-step confirmation run on Windows. |
| `cmake/toolchains/` | Toolchain files for MinGW, llvm-mingw and Zig. |
| `.github/workflows/capi-windows.yml` | The Visual C++ builds. |
| `src/runtime.cpp`, `include/laya/runtime.hpp` | Thread count and GPU choice settable by the library; a clean error where upstream aborted when no GPU exists; discrete GPU preferred; the macOS pass limit; the load trace. |
| `src/main.cpp` | `--threads N` for `laya-cli`. |

## Applying the additions to your own checkout of laya.cpp

The same additions are in `patches/` as 30 patches for `git am`:

```
git clone --recurse-submodules https://github.com/lkarlslund/laya.cpp
cd laya.cpp && git checkout e632da0
git am /path/to/patches/*.patch
```
