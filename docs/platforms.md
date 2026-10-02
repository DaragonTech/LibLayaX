# Platforms and packages

The library is built for six targets: Windows x64 and ARM64, Linux x86-64 and ARM64, macOS on
Apple Silicon and Intel. The functions, the requests and the answers are the same on all of
them.

## Which library for which program

Choose by the architecture of your **program**. A program can only load a library of its own
architecture, whatever the machine is.

| Your program | Package | Backends |
|---|---|---|
| Windows x64, processor with AVX2 | `laya-windows-…-avx2` | CPU |
| Windows x64, any processor | `laya-windows-…-compat-sse42` | CPU |
| Windows x64 with a GPU | `laya-windows-…-vulkan` | CPU and GPU |
| Windows ARM64 (native ARM64 program) | `laya-windows-arm64-…` | CPU |
| Linux x86-64 | `laya-linux-…`, folder `avx2`, `compat-sse42` or `vulkan` | CPU; CPU and GPU in `vulkan` |
| Linux ARM64 | `laya-linux-arm64-…` | CPU |
| macOS, Apple Silicon | `laya-macos-…-arm64` | CPU |
| macOS, Apple Silicon, with GPU | `laya-macos-vulkan-…-arm64` | CPU and GPU |
| macOS, Intel | `laya-macos-…-x86_64-avx2`, `…-x86_64-compat-sse42` | CPU |

**The two x86-64 flavours.** `avx2` needs a processor with AVX2, FMA, F16C and BMI2: Intel
since 2013 (Haswell), AMD since Zen. `compat-sse42` needs only SSE4.2 and runs on any 64-bit
PC since about 2009; it is slower. If a library is loaded on a processor that lacks what it
needs, `laya_create` fails with a message that names the missing instructions. It does not
crash.

**x64 programs on ARM machines.** An x64 program running under emulation (Windows on ARM,
or Rosetta on a Mac) must use a `compat-sse42` library, because the emulators do not provide
AVX. This includes Delphi Win64 applications on Windows on ARM: they are x64 programs and
cannot load the ARM64 library.

**The `vulkan` packages** contain the CPU backend too, and they load on machines without a
GPU. They are larger (the Windows DLL is about 75 MB against 35 MB). Ship one of them if you
want the same file to use a GPU where there is one.

## What a package contains

| File | Purpose |
|---|---|
| `laya.dll` / `liblaya.so` / `liblaya.dylib` | The library. The only file your application needs at run time (plus `libMoltenVK.dylib` in the macOS GPU package). |
| `laya_c.h` | The header, for C and C++. |
| `laya.lib`, `laya.def` | Windows only: the import library for Visual C++ and the list of exports it was made from. |
| `test-capi`, `laya-bench`, `laya-diag`, `laya-probe`, `laya-trace` | Test and benchmark tools; see [Testing](testing.md). `laya-trace` is not in the Windows packages. |
| `run-tests.bat` | Windows only: runs the tools one after another. |
| `LayaTests` | Windows x64 and Linux x86-64 only: the test program of the Delphi binding, already built, so that binding can be checked against each library. |
| `README.md` | Notes for that package. |

## What the library needs from the system

| Platform | Needs |
|---|---|
| Windows x64 | Built for Windows 10 and later; run on Windows 11. System DLLs only; the C++ runtime and ICU are inside the library. |
| Windows ARM64 | Built for Windows 10 on ARM and later; run on Windows 11. System DLLs only. |
| Linux x86-64 | glibc 2.38 or later (Ubuntu 24.04, Fedora 39, Debian 13 and newer). Nothing else. |
| Linux ARM64 | glibc 2.27 or later (Ubuntu 18.04 and newer). Nothing else. |
| macOS | macOS 11 or later; macOS 12 or later for the GPU package. System libraries only. |
| GPU, Windows | A GPU driver that provides Vulkan 1.2 or later (`vulkan-1.dll`, installed with NVIDIA, AMD and Intel drivers). |
| GPU, Linux | A driver with Vulkan 1.2 or later and the loader `libvulkan.so.1` (package `libvulkan1` on Ubuntu and Debian). |
| GPU, macOS | `libMoltenVK.dylib`, which is in the package. |

The Linux x86-64 libraries were built on Ubuntu 24.04, which is why they ask for a recent
glibc. On an older distribution, such as Debian 12 or RHEL 9, they do not load; build the
library from source on that system ([Building from source](building.md)).

No package needs Python, a server process or an internet connection.

## Shipping the library with an application

The library is one file. Put it where your program finds it.

### Windows

Put `laya.dll` in the same folder as the `.exe`. Windows looks there first.

### Linux

A Linux program does not look in its own folder unless it is told to. Either:

* link your program with `-Wl,-rpath,'$ORIGIN'` and put `liblaya.so` next to the executable;
* or install `liblaya.so` in a system library folder such as `/usr/local/lib` and run
  `ldconfig`;
* or set `LD_LIBRARY_PATH` to the folder that holds it, which is fine for testing.

The test tools in the package were not linked with `$ORIGIN`, which is why they are run as
`LD_LIBRARY_PATH=. ./test-capi`.

### macOS

The library identifies itself as `@rpath/liblaya.dylib`. Link your program with
`-Wl,-rpath,@executable_path` and put `liblaya.dylib` next to the executable. In an
application bundle that is `Contents/MacOS/`.

For the GPU package, put `libMoltenVK.dylib` in the same folder as `liblaya.dylib`. If it is
missing, the library still loads and the CPU backend works; only the `vulkan` backend
reports that Vulkan is not available.

Files downloaded with a browser are quarantined by macOS. Remove the mark once after
unzipping (`xattr -dr com.apple.quarantine .`). An application you distribute to others needs
to be signed, and the libraries with it, like any other macOS software.

### Linking from C and C++

| Compiler | How |
|---|---|
| GCC or Clang on Linux and macOS | `-L<folder> -llaya` |
| MinGW on Windows | `-L<folder> -llaya` |
| Visual C++ | add `laya.lib` to the link; `lib /def:laya.def /machine:x64 /out:laya.lib` makes it again if needed |

`laya.lib` was checked by linking the test suite against it with the LLVM linker. It has not
yet been tried with Visual C++ itself.

### The model

The model folder is separate from the library and about 800 MB for the English model. An
application can ship it, download it on first run, or ask the user where it is. The library
only needs a path to a folder with the five model files
([Getting started](getting-started.md#2-get-the-model)).

## Memory and start-up

| | English model |
|---|---|
| Model files on disk | about 800 MB |
| Memory with the CPU backend | about 1.7 GB |
| Memory with a GPU backend | the equivalent in GPU memory |
| Time to load | about one second from a fast disk |

## What has been tested, and what has not

Every platform is delivered as version 1.0.14. This table lists what has actually run on real
hardware, with the real English model.

| Build | Version that ran | Machine |
|---|---|---|
| Windows x64 `avx2`, `compat-sse42` | 1.0.5 | Intel Core Ultra 9 275HX |
| Windows x64 `vulkan`, on the GPU | 1.0.6 | NVIDIA RTX 5080 Laptop GPU |
| Windows x64 `compat-sse42` | 1.0.9 | Windows 11 on ARM, x64 emulation (Parallels) |
| Windows ARM64 | 1.0.14 | Windows 11 on ARM (Parallels on Apple M3 Ultra) |
| Linux ARM64 | 1.0.14 | Ubuntu 24.04 ARM64 (Parallels on Apple Silicon) |
| macOS arm64, CPU | 1.0.5 | Apple M3 Ultra |
| macOS arm64, on the GPU | 1.0.9 | Apple M3 Ultra |
| macOS arm64, GPU package on its CPU backend | 1.0.14 | Apple M3 Ultra (through the Lua binding's test suite) |

Run only on the build machine, with a synthetic model (the real architecture with random
weights): Linux x86-64, all three folders, the `vulkan` one on its CPU backend.

Not run anywhere yet:

* the macOS Intel builds;
* the GPU backend on Linux;
* AMD and Intel GPUs on any system;
* a build with Visual C++, and the CUDA backend;
* the `multilingual` and `typed-decisions` models.

Between 1.0.9 and 1.0.14 nothing changed in the engine for the platforms that were tested at
an older version; those versions added the Windows ARM64 fix, the debug trace and
`laya-probe` ([History](history.md)). The cross-built binaries were also run before delivery
under Wine (Windows x64), QEMU (Linux ARM64) and ARM64 Wine under QEMU (Windows ARM64). macOS
binaries cannot be run on the Linux build machine at all.

Both ARM64 runs gave the reference answer (`noul` 0.8364 for the refund example), 54 checks
and 0 failures, with floating-point exceptions switched on by the test and found unchanged
afterwards.

If you run the library on a platform from the "not run" list, the output of `test-capi` and
`laya-bench` is a useful report.
