![LibLayaX Logo](./liblayax-logo.png)

# LibLayaX

**Fast local AI decisions inside your application.** LibLayaX runs the Laya AI model
in-process, as an ordinary library, on six platforms: hundreds of decisions per second on a
laptop GPU, with no server and no cloud. An unofficial C API library built on laya.cpp.
Version 1.0.14.

## What this project is for

[Laya](https://github.com/NandhaKishorM/laya) is an AI model for decisions: a transformer
neural network (ModernBERT-large for English, mmBERT for 100+ languages) trained to read a
piece of text and answer typed questions about it. Is this a refund request, yes or no? Which
of these intents is it? How angry is the customer, on a scale? Unlike a chat model it does not
generate text. Its authors call it a "System 1" decision engine: one forward pass through the
network returns the answer with a probability.

**That single pass is what makes it fast.** A chat model writes its answer token by token;
Laya reads the text once and is done. Measured with this library and the English model, a
laptop GPU answers about 670 questions per second (1.5 milliseconds each), an Apple M3 Ultra
about 170 per second, and a fast desktop CPU alone 14 to 21 per second (see [Speed](#speed)).

That makes it the kind of AI that belongs *inside* software rather than in a chat window:
routing a support ticket, flagging a message, deciding whether a document needs review,
quickly enough to sit in the path of a request or to work through a backlog of thousands, with
the answer coming back as a label or a number rather than prose to parse.

The two existing implementations are not built to live inside another application:

* the original **Python library**, which needs Python and PyTorch wherever it runs;
* **[laya.cpp](https://github.com/lkarlslund/laya.cpp)**, the native C++ port on the ggml
  inference engine, which is a command-line tool and an HTTP server. An application has to
  start, monitor and talk to a separate process.

LibLayaX closes that gap. It turns the laya.cpp inference engine into one library file that an
application loads like any other library, so an AI decision becomes a function call:

* **Fast, with nothing in between.** A decision is a function call into the engine: no HTTP
  round trip, no serialization to another process. On a GPU, sending several questions in
  one call multiplies the throughput.
* **On-device AI, embedded.** The model runs on the user's own CPU or GPU, inside your
  process. No cloud API, no per-call fees, no Python, no server or second process to deploy.
  The text being analysed never leaves the machine, and it works offline.
* **From the language the application is already written in.** Ten plain C functions with
  JSON in and JSON out can be called from C, C++, Delphi, Rust, Lua, C#, Python, Go, Java and
  anything else that can load a shared library.
* **On the platforms real applications ship on.** Six targets from one source: Windows x64
  and ARM64, Linux x86-64 and ARM64, macOS on Apple Silicon and Intel. Inference runs on the
  CPU everywhere, and on the GPU through Vulkan on Windows, Linux and Apple Silicon.
* **Written once.** The same functions and the same JSON requests and responses on every
  platform, so an integration is done once and carried to the others.
* **Built to be a guest.** Errors come back as values instead of exceptions crossing into
  your code; one loaded model can be shared between threads; the host's floating-point
  settings are left as they were.

That is what the X stands for: cross-language and cross-platform. The aim is to make this kind
of AI practical in real-world desktop and server applications, not only in Python programs
and behind HTTP endpoints.

LibLayaX is an independent project. It is not part of Laya or of laya.cpp and is not endorsed
by their authors; see [Credits](#credits). What has and has not been tested on real hardware
is listed under [What has been tested](#what-has-been-tested-and-what-has-not).

## Getting the model

LibLayaX does not include the model. The weights are published by the Laya project on Hugging
Face, in the repository **[convaiinnovations/laya](https://huggingface.co/convaiinnovations/laya)**.
laya.cpp's download script takes all three variants from that one repository: `english` at the top level, and `multilingual` and `typed-decisions`
in subfolders of those names.

For each variant, five files are needed, kept in this layout:

```
model.safetensors                 the weights (about 800 MB for english)
rl_agent_config.json
encoder/config.json
tokenizer/tokenizer.json
tokenizer/tokenizer_config.json
```

Three ways to get them:

* **The script in this tree** (from laya.cpp) downloads exactly those files, at the revision
  laya.cpp pins, into `laya.cpp/models/laya`:

  ```
  pip install huggingface_hub
  python laya.cpp/scripts/download_model.py --variant all      (or english, multilingual, typed-decisions)
  ```

* **The Hugging Face command-line tool**, into any folder:

  ```
  pip install huggingface_hub
  huggingface-cli download convaiinnovations/laya --local-dir /models/laya \
      --include "model.safetensors" "rl_agent_config.json" "encoder/*" "tokenizer/*"
  ```

* **A browser:** open the repository page, go to "Files", and download the five files into the
  same folder structure.

The folder you then give to `laya_create` (or to `TLayaAgent.Create`, `Agent::new`,
`llaya.new`) is the one that contains `rl_agent_config.json`: `/models/laya` for english. For
another variant either give its subfolder, or give the top folder together with the option
`{"variant":"multilingual"}`.

The model is the Laya project's work and comes with its own terms; see the model card on the
repository page. Loading takes about a second and about 1.7 GB of memory on the CPU.

## Speed

Measured with `laya-bench` from this project, the English model (ModernBERT-large), questions
sent in batches of 16. "Per question" is the batch time divided by 16.

| Machine | Backend and precision | Questions per second | Per question |
|---|---|---:|---:|
| NVIDIA RTX 5080 Laptop GPU (Windows 11) | Vulkan, fp16 | about 670 | 1.5 ms |
| NVIDIA RTX 5080 Laptop GPU (Windows 11) | Vulkan, fp32 | about 230 | 4.3 ms |
| Intel Core Ultra 9 275HX (Windows 11) | CPU | about 21 | 48 ms |
| Apple M3 Ultra (macOS) | Vulkan on Metal, bf16 | 170 | 5.9 ms |
| Apple M3 Ultra (macOS) | Vulkan on Metal, fp32 | 111 | 9.0 ms |
| Apple M3 Ultra (macOS) | CPU | 14 | 70 ms |
| Windows 11 on ARM virtual machine, 4 virtual CPUs | CPU | 4 | 250 ms |
| Ubuntu 24.04 ARM64 virtual machine, 2 virtual CPUs | CPU | 2 | 490 ms |

A single question sent on its own takes 29 ms on the M3 Ultra's GPU and 76 ms on its CPU.
Loading the model takes about one second from a fast disk. The half-precision modes (`fp16`,
`bf16`) are the fastest; in these runs their probabilities stayed within about 0.003 of the
CPU's. Your numbers will differ with the hardware, the length of the texts and the
batch size; `laya-bench /path/to/model` prints them for your machine.

## Using it from your language

| Language | What to use | Where it lives |
|---|---|---|
| C, C++ | `laya_c.h` and the library (`laya.lib` for Visual C++) | this repository and the binary packages |
| Delphi, Free Pascal | `Laya.pas`, with the `TLayaAgent` class | the **DLaya** repository |
| Rust | the `rlaya` crate, with the `Agent` type | the **RLaya** repository |
| Lua | the `llaya` module (binaries for Lua 5.1) | the **LLaya** repository |
| C#, Python, Go, Java, ... | the language's own way of loading a C library | declare the ten functions from `laya_c.h` |

DLaya, RLaya and LLaya are small projects that sit on top of this library; each has its own
README, tests and example. Their source is not in this repository or in the binary
packages. The Windows and Linux x86-64 packages do include `LayaTests`, DLaya's test program
already built, so that the Delphi binding can be checked against each library.

**Names.** LibLayaX is the name of the project. The library files it produces are `laya.dll`
on Windows, `liblaya.so` on Linux and `liblaya.dylib` on macOS, and its functions start with
`laya_`.

This repository is the source: upstream laya.cpp plus everything added for the library, its
tests, its diagnostic tools and the scripts that cross-build every platform from one Linux
machine.

## What is in here

```
LibLayaX/
├── README.md      this file: what the project is, how to build, test and debug it
├── LICENSE        MIT, covering LibLayaX and the laya.cpp code it is built on
├── NOTICE         acknowledgements: Laya, laya.cpp and the libraries used
├── laya.cpp/      the complete source tree, ready to build
│                  (upstream laya.cpp at commit e632da0 + LibLayaX; ggml and cpp-httplib
│                  included, unmodified)
└── patches/       the same additions as 30 patches for `git am`, for applying to your own
                   checkout of upstream instead of using the tree above
```

### Which README is which

There are several README files in here and they are about different things:

| File | Written by | About |
|---|---|---|
| `README.md` (this one) | LibLayaX | The project as a whole: layout, building, testing, history, status. Start here. |
| `laya.cpp/capi/README.md` | LibLayaX | The API reference: every function, the options, behaviour a host must know, diagnostics. |
| `laya.cpp/README.md` | upstream, unchanged | laya.cpp itself: the command-line tool, the HTTP server, benchmarks. It does not mention LibLayaX. |

## What was added to upstream

46 files, about 3,200 lines. Upstream's own code is touched in four places only
(`CMakeLists.txt`, `include/laya/runtime.hpp`, `src/runtime.cpp`, `src/main.cpp`).

| Path (inside `laya.cpp/`) | What it is |
|---|---|
| `capi/laya_c.h` | The public header: the whole contract in 94 lines. |
| `capi/laya_c.cpp` | The implementation: error handling, floating-point guard, CPU check, crash reporter, debug trace. |
| `capi/CMakeLists.txt` | The `laya_c` target and the tools; enabled with `-DLAYA_C_API=ON`. |
| `capi/vulkan_lazy.c` | Linux: opens the Vulkan loader on first use, so the library loads on machines without Vulkan. |
| `capi/linux-exports.map`, `capi/macos-exports.txt` | Export lists: only the ten `laya_*` functions are visible. |
| `capi/laya.def` | The Windows export list; the build scripts make the import library `laya.lib` from it for Visual C++. |
| `capi/patches/ggml-trace.diff` | Optional patch for ggml that extends the debug trace into the CPU engine start-up. |
| `tests/capi/test_capi.c` | The test suite (`test-capi`). |
| `tests/capi/bench_capi.c` | `laya-bench`: load time, speed, and whether a backend gives the reference answers. |
| `tests/capi/diag_capi.c` | `laya-diag`: 21 cases comparing a GPU backend with the CPU. |
| `tests/capi/trace_capi.c` | `laya-trace`: per-tensor comparison between two backends. |
| `tests/capi/probe.cpp` | `laya-probe`: checks the C++ runtime features the library needs, without the library. |
| `tests/capi/test_parity.py` | Checks the library's answers are byte-identical to `laya-cli`. |
| `tests/capi/make_synthetic_model.py` | Writes a checkpoint with the real architecture and random weights, for testing without the 800 MB download. |
| `scripts/capi/build-*.sh` | One cross-build script per platform family (see below). |
| `scripts/capi/run-tests.bat` | One-step confirmation run on Windows. |
| `cmake/toolchains/` | Toolchain files for MinGW, llvm-mingw and Zig. |
| `.github/workflows/capi-windows.yml` | MSVC builds (CPU, Vulkan, CUDA 12). Written but never run. |
| `src/runtime.cpp`, `include/laya/runtime.hpp` | Thread count and GPU choice settable by the library; a clean error instead of an abort when no Vulkan GPU exists; discrete GPU preferred; the macOS Vulkan pass limit; load trace. |
| `src/main.cpp` | `--threads N` for `laya-cli`. |

## The API in one screen

```c
laya_agent* a = laya_create("C:\\models\\laya", "{\"backend\":\"cpu\"}");
if (!a) { puts(laya_last_error()); return 1; }
char* out = laya_predict(a,
    "{\"state\":\"Please refund the duplicate charge.\","
    " \"questions\":{\"refund\":{\"type\":\"noul\","
    "                \"instructions\":\"Does the customer ask for a refund?\"}}}");
puts(out);   /* {"results":[{"answers":{"refund":{"type":"noul","noul":0.8364,...}}}],...} */
laya_free_string(out);
laya_destroy(a);
```

| Function | Purpose |
|---|---|
| `laya_create(dir, options_json)` | Load a model. `NULL` on failure. |
| `laya_predict(agent, request_json)` | Answer one request or an array of them. |
| `laya_prepare(agent, request_json)` | Tokenized inputs only, for debugging. |
| `laya_info(agent)` | Backend, device, model and limits. |
| `laya_free_string(s)` | Free a string returned by the three calls above. |
| `laya_destroy(agent)` | Unload. |
| `laya_last_error()` | Last failure on the calling thread. |
| `laya_set_log_callback(fn, user, level)` | Receive the engine's log lines. |
| `laya_version()`, `laya_api_version()` | Identification; the API version is 1. |

Options for `laya_create`: `backend` (`cpu`, `vulkan`, `cuda`), `variant`, `precision`
(`fp32`, `fp16`, `bf16`), `threads`, `device`, `flash`, `tensor_core`, `allow_truncation`.
Details, and what a host must know about threads, memory and floating-point state, are in
`laya.cpp/capi/README.md`.

## Building

Everything below runs on Linux (Ubuntu 24.04 was used) and produces binaries for all
platforms. Each script downloads and builds ICU statically on first use, then writes
`build-<platform>-<variant>/bin/` inside `laya.cpp/`. Run them from `laya.cpp/`.

Common needs: `cmake ninja-build g++ nlohmann-json3-dev curl`.

| Target | Command | Extra tools |
|---|---|---|
| Windows x64, CPU (AVX2 and SSE4.2 builds) | `scripts/capi/build-windows-mingw.sh` | `mingw-w64` |
| Windows x64, Vulkan | `VARIANTS=vulkan VULKAN_HEADERS=… SPIRV_HEADERS=… GLSLC=… VULKAN_DEF=… scripts/capi/build-windows-mingw.sh` | Vulkan-Headers, SPIRV-Headers, shaderc ≥ 2025.3 |
| Windows ARM64, native | `LLVM_MINGW=/path/to/llvm-mingw scripts/capi/build-arm64.sh windows` | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) |
| Linux x86-64, CPU (AVX2 and SSE4.2) | `scripts/capi/build-linux.sh` | — |
| Linux x86-64, Vulkan | `VARIANTS=vulkan VULKAN_HEADERS=… EXTRA_CMAKE_ARGS="-DVulkan_GLSLC_EXECUTABLE=…" scripts/capi/build-linux.sh` | as Windows Vulkan |
| Linux ARM64 | `scripts/capi/build-arm64.sh linux` | `pip install ziglang` |
| macOS arm64 and x86-64, CPU | `scripts/capi/build-macos.sh` | `pip install ziglang`, `lld` |
| macOS arm64, Vulkan on Metal | `MOLTENVK=…/libMoltenVK.dylib VULKAN_HEADERS=… GLSLC=… scripts/capi/build-macos.sh arm64-vulkan` | MoltenVK release |

The header of each script documents its variables. A native build on any platform is the
ordinary CMake one: `cmake -S . -B build -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF` and
`cmake --build build --target laya_c`.

**CPU baselines.** x86-64 comes in two builds: `avx2` (AVX2, FMA, F16C, BMI2: Intel since
2013, AMD since Zen) and `compat` (SSE4.2: any 64-bit PC since about 2009, and x64 emulation
on Windows on ARM, which has no AVX). The library checks the CPU when a model is loaded and
reports a clear error instead of crashing on an older one.

**Linking from C or C++.** `-llaya` on Linux, macOS and MinGW. Visual C++ links `laya.lib`,
which is in the Windows packages together with `laya.def`.

**What the binaries depend on.** Windows: system DLLs only (the C++ runtime and ICU are linked
in). Linux: glibc only. macOS: system libraries only. Vulkan builds open the Vulkan loader
only when the `vulkan` backend is requested, so they also work as CPU builds on machines
without it.

## Testing

```
test-capi                                   26 contract checks, no model needed
LAYA_TEST_MODEL=/path/to/model test-capi    + end-to-end tests: 54 checks
LAYA_TEST_BACKEND=vulkan ...                same on a GPU, plus a GPU-versus-CPU answer check
laya-bench /path/to/model                   load time, speed, answers
laya-probe                                  C++ runtime check, no library, no model
```

On Linux the tools do not look next to themselves for the library: run them from the package
folder as `LD_LIBRARY_PATH=. ./test-capi`. After unzipping on Linux or macOS, make them
executable first (`chmod +x test-capi laya-bench laya-diag laya-trace laya-probe`).

On Windows, `run-tests.bat MODEL_FOLDER [cpu|vulkan]` runs the probe, the suite, DLaya's test
program if `LayaTests.exe` is present, and the benchmark, printing everything as it happens.

The suite covers the three question types, batches, Unicode and JSON-object inputs,
determinism, malformed requests, over-long input, four threads sharing one model, reload, and
that the host's floating-point settings come back unchanged after it unmasked exceptions the
way a Delphi program does. The reference answer for the refund example with the english model
is `noul` 0.8364 on every platform.

Without the real model, `python tests/capi/make_synthetic_model.py DIR` writes a checkpoint
that exercises the whole pipeline (its answers are meaningless).

Cross-built binaries were run before delivery with Wine (Windows x64), QEMU (Linux ARM64) and
ARM64 Wine under QEMU (Windows ARM64). macOS binaries cannot be run on Linux at all.

## When something goes wrong

| Set | Effect |
|---|---|
| `LAYA_DEBUG=1` | The library prints `[laya] …` on stderr at each stage of each call, including every step of a model load. If a call never returns, the last line says where it is. |
| `LAYA_CRASH_REPORT=1` | Windows: fatal exceptions are written to stderr and `laya-crash.txt` with module offsets and a stack trace. |
| `LAYA_MAX_PASS_TOKENS=n` | Caps the tokens evaluated per pass (0 = no cap). Default: 512 for FP32 Vulkan on macOS, unlimited elsewhere. |
| `GGML_VK_VISIBLE_DEVICES=n` | Restricts Vulkan to one GPU. The `device` option is the usual way. |

## History

| Version | Change |
|---|---|
| 1.0.0 | First library, Delphi binding, tests. (Until 1.0.14 the project was called "laya C API"; the name LibLayaX came afterwards.) |
| 1.0.1 | Refuses to load on a CPU without the build's instruction set (was an access violation). Crash reporter. |
| 1.0.2 – 1.0.3 | Floating-point control saved and restored through the registers directly; MinGW's `fenv` did not mask or restore SSE. |
| 1.0.4 | Floating-point exceptions also masked on the engine's worker threads. |
| 1.0.5 | Heap corruption at host-thread exit fixed (a `thread_local std::string`). First version that passed everywhere. Linux and macOS builds added. |
| 1.0.6 | Vulkan: clean error without a GPU, loader opened on demand, discrete GPU preferred, `device` option. |
| 1.0.7 – 1.0.8 | macOS Vulkan investigation; `laya-diag` and `laya-trace`; an endless loop in the test's answer comparison fixed. |
| 1.0.9 | macOS Vulkan (MoltenVK) gave wrong FP32 answers for passes above 512 tokens; batches are now split there. Linux Vulkan build. Linux ARM64 build. |
| 1.0.10 – 1.0.12 | Windows ARM64: rebuilt with llvm-mingw after the Zig build hung; `LAYA_DEBUG` trace; `laya-probe`. |
| 1.0.13 | Windows ARM64: the library clears the FPCR trap enables itself. MinGW's `feholdexcept` leaves them set, and with a host that unmasks exceptions the engine stalled forever. |
| 1.0.14 | 1.0.13 without the temporary trace inside ggml. All platforms rebuilt at this version, with `laya-probe` in every package; the Linux libraries export exactly the ten API functions. |

## What has been tested, and what has not

Run on real hardware:

| Build | Version | Machine |
|---|---|---|
| Windows x64 avx2, compat | 1.0.5 | Intel Core Ultra 9 275HX |
| Windows x64 Vulkan | 1.0.6 | NVIDIA RTX 5080 Laptop GPU |
| Windows x64 compat | 1.0.9 | Windows 11 on ARM, x64 emulation (Parallels) |
| Windows ARM64 | 1.0.14 | Windows 11 on ARM (Parallels on Apple M3 Ultra) |
| Linux ARM64 | 1.0.14 | Ubuntu 24.04 ARM64 (Parallels on Apple Silicon) |
| macOS arm64, CPU | 1.0.5 | Apple M3 Ultra |
| macOS arm64, Vulkan | 1.0.9 | Apple M3 Ultra |
| macOS arm64, Vulkan package on its CPU backend | 1.0.14 | Apple M3 Ultra (through LLaya's test suite, not `test-capi`) |

Run only on the build machine, with the synthetic model: Linux x86-64 (avx2, compat, and the
Vulkan build on its CPU path).

Not run anywhere yet: the macOS x86-64 builds, Linux Vulkan on an actual GPU, the MSVC and
CUDA builds, DLaya in Embarcadero Delphi (it has only been compiled with Free Pascal),
and the multilingual and typed-decisions models on any platform.

Every platform is delivered as 1.0.14. The table above lists the version that actually ran on
real hardware: apart from Windows ARM64 and Linux ARM64, 1.0.14 itself has so far only run on
the build machine (Linux x86-64) and under Wine (Windows x64); on macOS only the Vulkan package
has run, on its CPU backend, and its GPU path has not been re-run since 1.0.9. For
those platforms nothing in the engine changed after 1.0.9; 1.0.14 adds the `LAYA_DEBUG` trace
and `laya-probe`.

Both ARM64 runs were with the real english model and gave the reference answer (`noul` 0.8364),
54 checks and 0 failures, with floating-point traps switched on by the test and restored
afterwards.

## Known problems

* **macOS Vulkan.** The 512-token split works around an out-of-bounds write somewhere between
  ggml's Vulkan backend and MoltenVK; the cause itself is not fixed or reported upstream.
* **Zig and Windows ARM64.** A DLL built with Zig 0.16 for this target hung on real hardware
  in the first call that reports an error. The cause was never found; llvm-mingw is used
  instead.
* **Emulators hide floating-point traps.** QEMU ignores the trap-enable bits, so the 1.0.13
  problem could not be reproduced before delivery and a similar one would not be either. On
  real ARM64 hardware the trap case has now passed on Windows (after the fix) and on Linux.
* **Engine asserts** still end the process (after sending the message to the log callback).

## Using the patches instead of the tree

```
git clone --recurse-submodules https://github.com/lkarlslund/laya.cpp
cd laya.cpp && git checkout e632da0
git am /path/to/patches/*.patch
```

## Credits

* **[Laya](https://github.com/NandhaKishorM/laya)** by NandhaKishorM is the original project:
  the model, the typed-decision primitives (`choice`, `score`, `noul`) and the Python
  reference implementation on PyTorch and Transformers. Apache-2.0. The weights are published
  on Hugging Face under `convaiinnovations`.
* **[laya.cpp](https://github.com/lkarlslund/laya.cpp)** by Lars Karlslund is the native C++
  port of Laya inference, built on ggml, with CPU, CUDA, Vulkan and Core ML backends. MIT.
  LibLayaX is a layer on top of it and contains its source.
* **LibLayaX**, its tests, tools and build scripts were written by Claude (Anthropic), under
  the direction of **Felipe Daragon** of **DaragonTech**, who set the goals, guided the work
  and ran every test on real Windows and Mac hardware.
* The binaries also contain [ggml](https://github.com/ggml-org/ggml) (MIT),
  [ICU](https://icu.unicode.org) (Unicode License) and
  [nlohmann/json](https://github.com/nlohmann/json) (MIT).

## License

LibLayaX is released under the MIT License; see [LICENSE](LICENSE). It is the same license as
laya.cpp, whose code this project contains and whose copyright notice the file keeps
(Copyright (c) 2026 Lars Karlslund; the original is also at `laya.cpp/LICENSE`).

Other parts keep their own terms:

* ggml and cpp-httplib, included in `laya.cpp/third_party/`, are MIT licensed; their license
  files are in their folders.
* The released binaries also contain ICU (Unicode License) and nlohmann/json (MIT), and the
  macOS Vulkan package bundles MoltenVK (Apache-2.0, license included in that package).
* Laya itself, the Python project, is Apache-2.0. None of its code is in this repository, which
  is why it has no copyright line in `LICENSE`; it is credited first in [NOTICE](NOTICE) and
  under [Credits](#credits).
* The model weights are not part of this project; they are published on Hugging Face under
  their own terms.
