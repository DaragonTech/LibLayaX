# Troubleshooting

Start with the message. `laya_create` gives its reason through `laya_last_error()`;
`laya_predict` returns `{"error":"..."}`. Most problems are named there.

## The library does not load

These happen before any `laya_` function runs, so the message comes from the system or from
your language, not from the library.

| Symptom | Cause | Fix |
|---|---|---|
| Windows: "The specified module could not be found" (error 126) | `laya.dll` is not where the program looks. | Put it in the same folder as the `.exe`. |
| Windows: "is not a valid Win32 application" (error 193) | The DLL is for another architecture than the program. | Use the library that matches the program: x64 for an x64 program, ARM64 for an ARM64 program. An x64 program on Windows on ARM needs the x64 `compat-sse42` DLL. |
| A 32-bit program cannot load the library | The library is 64-bit only. | Build the program as 64-bit. |
| Linux: `error while loading shared libraries: liblaya.so: cannot open shared object file` | Linux does not look in the program's own folder. | Run with `LD_LIBRARY_PATH=.`, or link with `-Wl,-rpath,'$ORIGIN'`, or install the library in a system folder. See [Platforms](platforms.md#linux). |
| Linux: ``version `GLIBC_2.38' not found`` | The system is older than the x86-64 package was built for. | Build from source on that system ([Building](building.md)). |
| macOS: the library "cannot be opened because the developer cannot be verified" | The download quarantine. | `xattr -dr com.apple.quarantine .` in the package folder. |
| macOS and Linux: "Permission denied" when starting a tool | Zip files do not keep the executable flag. | `chmod +x test-capi laya-bench laya-diag laya-trace laya-probe` |

To see which library a program really loaded, print `laya_version()`. It shows the version
and the backends in that file.

## laya_create returns NULL

| `laya_last_error()` | Cause | Fix |
|---|---|---|
| `model_dir is empty` | No path was given. | |
| `Cannot open …/rl_agent_config.json` | The folder is not a model folder, or the path is wrong. | Give the folder that contains `rl_agent_config.json`. Check that all five files are there ([Getting started](getting-started.md#2-get-the-model)). |
| `Cannot open model.safetensors`, `Cannot open tokenizer: …` | The download is incomplete. | Download the missing file, keeping the `encoder/` and `tokenizer/` folders. |
| `Truncated tensor payload: …`, `Invalid safetensors …` | The weights file is damaged or incomplete. | Download `model.safetensors` again; it should be about 803 MB for the English model. |
| `[json.exception.parse_error…]` | The options are not valid JSON. | |
| `options_json must be a JSON object` | The options are valid JSON but not an object. | Pass `{…}`, an empty string or `NULL`. |
| `Unknown option: …` | A key the library does not know, often a typing mistake. | See [Options](options.md). |
| `Unknown backend: …`, `Unknown precision: …`, `Unknown model variant: …` | A value that is not one of the allowed ones. | See [Options](options.md). |
| `This build has no Vulkan backend` | A GPU was asked of a CPU-only library. | Use a `vulkan` package. |
| `Vulkan is not installed on this computer (…)` | No Vulkan loader on the system. | Install or update the GPU driver, or use `"backend":"cpu"`. |
| `Vulkan is not available: put libMoltenVK.dylib next to liblaya.dylib, …` | macOS: the MoltenVK file from the package is missing. | Put it in the same folder as the library. |
| `No usable Vulkan GPU found` | Vulkan is there but lists no GPU. | Update the driver, or use the CPU. |
| `Vulkan device … does not exist`, `No Vulkan GPU matches "…"` | The `device` option points at a GPU that is not there. | The message lists what was found. |
| `FP16 currently requires Vulkan`, `BF16 mode requires a GPU; …` | A half precision on the CPU. | Use `fp32` on the CPU. |
| `This laya build needs CPU instructions your processor (or virtual machine) does not provide: …` | An `avx2` library on a processor without AVX2, or under an emulator. | Use the `compat-sse42` library. |
| `Insufficient device memory for model weights` | The GPU does not have enough free memory for the model. | Close other GPU programs, or use the CPU. |

## laya_predict returns an error

The messages about the request itself are listed in
[Requests and answers](requests.md#errors). The most common in practice:

| Message | Fix |
|---|---|
| `Question '…' exceeds state context limit (N tokens)` | The text is too long for one pass. Shorten or split it, or load the model with `"allow_truncation":true`. See [Size limits](requests.md#size-limits). |
| `[json.exception.parse_error…]` | The request is not valid JSON. The usual cause is text placed into the request without escaping quotes, backslashes or line breaks. Build the request with a JSON library, or with the helper your binding provides. |
| `Insufficient memory for this batch` | Send fewer requests per call. |
| `agent is NULL` | `laya_create` failed earlier and its result was not checked. |

## A call never returns, or the program crashes

### The debug trace

Set `LAYA_DEBUG=1` in the environment before starting the program. The library then prints a
line on the standard error stream at every stage of every call, in any host program. For
example (shortened):

```
[laya] create: enter
[laya] create: checking arguments
[laya] create: loading model: /models/laya
[laya] load: reading rl_agent_config.json
[laya] load: starting the cpu backend, threads=4
[laya] load: device: Apple Silicon
[laya] load: model.safetensors opened, 803 MB
[laya] load: reading 206 tensors
[laya] create: model loaded
[laya] predict: enter
[laya] predict: running the model
[laya] predict: done
```

Each line is written out immediately. If a call never returns, the last line shows where it
is; if a call fails, the reason is printed. On ARM64 the trace also shows the floating-point
control registers before and after the library adjusts them.

### Crash reports on Windows

Set `LAYA_CRASH_REPORT=1`. If the process then hits a fatal error (an access violation, an
illegal instruction), the library writes what happened to the standard error stream and to a
file `laya-crash.txt` in the current folder: the kind of error, the address as an offset
into the module it happened in, and a stack trace. `run-tests.bat` switches this on.

### Is it the machine?

Run `laya-probe` ([Testing](testing.md#laya-probe-the-machine-itself)). It checks the C++
runtime features the library depends on without loading the library, and tells apart a fault
in the library from one in the system underneath.

### All the environment variables

| Variable | Effect |
|---|---|
| `LAYA_DEBUG=1` | The trace described above. |
| `LAYA_CRASH_REPORT=1` | Windows: the crash report described above. |
| `LAYA_MAX_PASS_TOKENS=n` | The most tokens the engine evaluates in one pass; larger batches are split and give the same answers. 0 means no limit. The default is 512 for `fp32` on the macOS GPU and no limit elsewhere. |
| `GGML_VK_VISIBLE_DEVICES=n` | Restricts Vulkan to one GPU. The `device` option is the usual way. |

## It is slow

* **Loading takes about a second; answering does not.** If every question takes a second
  or more, the program is probably creating an agent per question. Create one and keep it.
* **CPU speed is tens of milliseconds per question** on a fast desktop processor and more on
  small ones: about 250 ms on a 4-core virtual machine, about 490 ms on a 2-core one. For
  more, use a GPU.
* **On a GPU, send batches.** One question per call wastes most of what the GPU can do. See
  [Batches](requests.md#batches).
* **On a GPU, use half precision**: `"precision":"fp16"` or `"bf16"` ([Options](options.md#precision)).
* **Check what is really in use**: `laya_info` or the `backend` and `device` fields of an
  answer show whether the GPU or the CPU did the work, and which GPU.
* **`compat-sse42` is slower than `avx2`.** Use `avx2` wherever the processors allow it.
* **`laya-bench`** prints the numbers for the machine it runs on ([Testing](testing.md#laya-bench-speed)).

## The GPU gives different answers from the CPU

Small differences are normal: within about 0.003 with half precision in the measured runs.
For anything larger, run `laya-diag` with the same options ([Testing](testing.md)). It
compares 21 kinds of request and shows which ones differ. If only large batches are wrong,
try `LAYA_MAX_PASS_TOKENS=512`, and please report what you found with the output of
`laya-diag`.

## Known problems

* **macOS, GPU, full precision.** Between the engine's Vulkan code and MoltenVK, a pass of
  more than 512 tokens with `fp32` writes outside its memory and gives wrong answers. The
  library works around it by splitting such batches, which gives the same answers as an
  unsplit pass would. The cause itself is not fixed and has not been reported upstream.
  `fp16` and `bf16` are not affected and are faster, so use those on a Mac.
* **Windows ARM64 and Zig.** A DLL built with Zig 0.16 for Windows ARM64 hung on real
  hardware in the first call that reports an error. The cause was never found. The released
  DLL is built with llvm-mingw, which works.
* **Emulators hide floating-point traps.** QEMU ignores the processor's trap-enable bits, so
  a problem like the one fixed in 1.0.13 cannot be seen before the library runs on real ARM
  hardware. On real ARM64 hardware the case now passes on Windows and on Linux.
* **Engine asserts end the process.** An internal check of the engine failing is not turned
  into an error value; the message goes to the log callback and the process ends. It should
  not happen with a model that loaded correctly.
* **Linux x86-64 needs glibc 2.38.** The released libraries were built on Ubuntu 24.04. On
  older distributions, build from source.
* **Not everything has run on real hardware.** See the list in
  [Platforms](platforms.md#what-has-been-tested-and-what-has-not).

## Reporting a problem

The most useful report contains:

1. the line printed by `laya_version()`;
2. the system, the processor or GPU, and whether it is a virtual machine;
3. the output of `laya-probe` and of `test-capi` with the model;
4. the output of the failing program with `LAYA_DEBUG=1`.
