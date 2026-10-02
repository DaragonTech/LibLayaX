# LibLayaX: the C API

This is the API reference of LibLayaX (`laya.dll` / `liblaya.so` / `liblaya.dylib`).

A flat C ABI around [laya.cpp](https://github.com/lkarlslund/laya.cpp) so Laya typed-decision
inference can be called in-process from Delphi, C#, Python (ctypes) or anything else that can
load a DLL. Everything crosses the boundary as UTF-8 JSON, the same request and response format
`laya-cli` uses, so no C++ types leak and the binding stays tiny.

```c
laya_agent* a = laya_create("C:\\models\\laya", "{\"backend\":\"cpu\"}");
char* out = laya_predict(a, "{\"state\":\"Please refund the duplicate charge.\","
                            "\"questions\":{\"refund\":{\"type\":\"noul\","
                            "\"instructions\":\"Does the customer ask for a refund?\"}}}");
/* {"results":[{"model":"laya-rl-agent","answers":{"refund":{"type":"noul","noul":0.97,...}}}],
    "elapsed_ms":3.1,"backend":"CPU","device":"..."} */
laya_free_string(out);
laya_destroy(a);
```

## Functions

| Function | Purpose |
|---|---|
| `laya_create(dir, options)` | Load a checkpoint. Returns `NULL` on failure; see `laya_last_error()`. |
| `laya_predict(agent, request)` | Run one request or an array of requests. Returns JSON (`results` or `error`). |
| `laya_prepare(agent, request)` | Tokenized model inputs only (debugging). |
| `laya_info(agent)` | Backend, device, model name, variant and limits as JSON. |
| `laya_free_string(s)` | Free any string returned by the three functions above. |
| `laya_destroy(agent)` | Unload. Safe with `NULL`. |
| `laya_last_error()` | Last failure on the calling thread. |
| `laya_set_log_callback(fn, user, min_level)` | Route engine logs (default: warnings to stderr). |
| `laya_version()`, `laya_api_version()` | Identification. |

Options for `laya_create` (all optional): `backend` `cpu`|`cuda`|`vulkan`; `variant`
`english`|`multilingual`|`typed-decisions` (picks `<dir>/<variant>` when `dir` is a model-store
root); `precision` `fp32`|`fp16`|`bf16`; `flash`, `tensor_core` (the fast CUDA path is both
`true`); `allow_truncation`; `threads` (CPU worker threads, default all cores); `device`
(Vulkan GPU: an index, or part of its name such as `"RTX"`; default is the first discrete
GPU). Unknown keys are rejected, so typos fail loudly.

## Behaviour that matters to a host application

* **No exceptions escape.** Errors come back as `NULL` + `laya_last_error()`, or as
  `{"error":"..."}`. Bad requests leave the agent usable.
* **Thread safe.** One agent can be shared between threads, and calls on it are serialized
  inside the library. For parallel throughput, batch requests (pass an array) rather than
  calling from many threads.
* **Floating-point state is protected.** Delphi and Free Pascal unmask FP exceptions, which
  would otherwise make the engine's IEEE arithmetic raise `EInvalidOp`. Every entry point masks
  exceptions (x87 and SSE/MXCSR on x86-64, the FPCR trap enables on ARM64) and restores the
  caller's exact state on return. You don't need `SetExceptionMask` in your Delphi code.
* **Memory.** A loaded model takes about 1.7 GB of RAM (CPU, fp32) or the equivalent in VRAM.
  Create one agent per model and keep it for the life of the process.
* **Paths are UTF-8.** Non-ASCII Windows paths work.
* **Fatal engine asserts** (a ggml internal invariant failing, which shouldn't happen with
  validated checkpoints) still terminate the process. The message is sent to your log callback
  first.

## Linking from C or C++

Include `laya_c.h` and link the library: `-llaya` on Linux, macOS and MinGW. Visual C++ needs
the import library `laya.lib`, which the Windows build scripts make from `capi/laya.def`
(`lib /def:laya.def /machine:x64 /out:laya.lib` does the same by hand).

## Diagnostics (environment variables)

* `LAYA_DEBUG=1`: every entry point prints its progress on stderr as `[laya] ...` lines,
  flushed one by one: entry, the floating-point registers before and after the guard (ARM64),
  each stage of a model load down to every tensor, and the reason for a failure. If a call
  never returns, the last line shows where it is. Works in any host program.
* `LAYA_CRASH_REPORT=1` (Windows): hardware exceptions are reported on stderr and in
  `laya-crash.txt` with module offsets and a stack trace before the host handles them.
* `capi/patches/ggml-trace.diff`: optional patch for `third_party/ggml` that extends the
  `LAYA_DEBUG` trace into the start-up of the CPU engine (apply with `git apply` in
  `third_party/ggml`). It located the Windows-on-ARM stall described below.

## Building

The library is an opt-in target: `-DLAYA_C_API=ON`. Build ggml statically so you get one
self-contained file.

**Windows, MSVC (CPU/Vulkan/CUDA)**: see `.github/workflows/capi-windows.yml`, which builds and
tests all three variants on GitHub Actions. Locally, from a VS 2022 x64 prompt with vcpkg:

```
vcpkg install icu:x64-windows-static nlohmann-json:x64-windows-static
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAYA_PORTABLE=ON -DLAYA_C_API=ON ^
  -DLAYA_CUDA=OFF -DLAYA_VULKAN=ON ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --target laya_c
```

**Windows from Linux (MinGW-w64, CPU)**: how the shipped `laya.dll` was made. It needs a
static ICU cross-built with MinGW (see `BUILD-NOTES.md` in the package).

```
cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=Release -DLAYA_CUDA=OFF -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF \
  -DGGML_NATIVE=OFF -DGGML_AVX2=ON -DGGML_FMA=ON -DGGML_F16C=ON -DGGML_OPENMP=OFF \
  -DICU_ROOT=... -DCMAKE_CXX_FLAGS=-DU_STATIC_IMPLEMENTATION
cmake --build build-win --target laya_c test-capi
```

**Linux**: `cmake -S . -B build -DLAYA_C_API=ON -DBUILD_SHARED_LIBS=OFF [-DLAYA_CUDA=OFF]`
produces `build/bin/liblaya.so`.

**Windows on ARM64 (native)**: `LLVM_MINGW=/path/to/llvm-mingw scripts/capi/build-arm64.sh windows`
cross-builds with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw). Two things were learnt
on real hardware (Windows 11 on ARM under Parallels, Apple M3 Ultra):
* A DLL built with Zig 0.16 for `aarch64-windows-gnu` hung in the first call that reports an
  error (cause not identified; the same code built with llvm-mingw works). Use llvm-mingw.
* MinGW's `feholdexcept` leaves the FPCR trap-enable bits set. With a host that unmasks FP
  exceptions the first overflow in the engine then stalled the thread forever, so the library
  clears and restores those bits itself (`fp_guard` in `capi/laya_c.cpp`). Emulators (QEMU)
  ignore trap enables and cannot reproduce this.

## Tests

* `tests/capi/test_capi.c`: C11 test suite. Runs API-contract tests (bad paths, bad
  options, `NULL` handling, error reporting) and, when `LAYA_TEST_MODEL` points at a checkpoint,
  end-to-end tests: all question types, batches, UTF-8 and JSON-object states, determinism,
  malformed requests, over-long input rejection, four threads sharing one agent, reload, and FP
  state preservation with exceptions unmasked like a Delphi host. `ctest -R capi`.
* `tests/capi/test_parity.py`: loads the library with ctypes and checks that its answers are
  byte-identical to `laya-cli` on the same checkpoint. With `--compare-dump` it also compares
  against a dump made on another platform.
* `tests/capi/make_synthetic_model.py`: writes a checkpoint with the exact production
  architecture and a real BPE tokenizer but random weights, for testing without downloading
  the model. Its answers are meaningless; it only proves the pipeline end to end.
* `tests/capi/probe.cpp` (`laya-probe`): stand-alone check of the C++ runtime features the
  library relies on (atomics, thread-local storage, FP environment, the engine's start-up
  arithmetic, call_once, mutexes, exceptions, threads). No library, no model; each step is
  announced before it runs.
* The language bindings are separate projects with their own tests: DLaya (Delphi and Free
  Pascal) and RLaya (Rust). `run-tests.bat` also runs DLaya's `LayaTests.exe` when it is placed
  next to the DLL.

## Credits

* **[Laya](https://github.com/NandhaKishorM/laya)** by NandhaKishorM is the original project:
  the model, the typed-decision primitives (`choice`, `score`, `noul`) and the Python
  reference implementation on PyTorch and Transformers. Apache-2.0. The weights are published
  on Hugging Face under `convaiinnovations`.
* **[laya.cpp](https://github.com/lkarlslund/laya.cpp)** by Lars Karlslund is the native C++
  port of Laya inference, built on ggml, with CPU, CUDA, Vulkan and Core ML backends. MIT.
  This library is a layer on top of it.
* **The C API**, its tests, tools and build scripts were written by Claude (Anthropic), under
  the direction of **Felipe Daragon** of **DaragonTech**, who set the goals, guided the work
  and ran every test on real Windows and Mac hardware.
