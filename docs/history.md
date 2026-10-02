# History

LibLayaX uses three-part version numbers. The API version, a separate number returned by
`laya_api_version()`, has been 1 since the beginning: no function was added, removed or
changed in any of these versions. Only the `device` option is newer (1.0.6). A newer library
can replace an older one without changes to the program that uses it.

Until 1.0.14 the project was called "laya C API"; the name LibLayaX came afterwards. The
version text returned by `laya_version()` still begins with `laya_c`.

| Version | Change |
|---|---|
| 1.0.14 | First public release. The same as 1.0.13 without the temporary trace inside ggml. All platforms rebuilt at this version, with `laya-probe` in every package and `laya.lib` and `laya.def` in the Windows packages. The Linux libraries export exactly the ten API functions. |
| 1.0.13 | Windows ARM64: the library clears the processor's floating-point trap enables itself. MinGW's `feholdexcept` leaves them set, and in a program that enables floating-point exceptions the engine stalled forever at start-up. |
| 1.0.10 – 1.0.12 | Windows ARM64: rebuilt with llvm-mingw after the Zig build hung. The `LAYA_DEBUG` trace and `laya-probe` were added to find the stall fixed in 1.0.13. |
| 1.0.9 | macOS GPU: full-precision passes above 512 tokens gave wrong answers through MoltenVK; such batches are now split. Linux GPU build. Linux ARM64 build. |
| 1.0.7 – 1.0.8 | Investigation of the macOS GPU problem; `laya-diag` and `laya-trace` added. An endless loop in the test suite's answer comparison fixed. |
| 1.0.6 | GPU: a clean error when there is no GPU, the Vulkan loader opened only on demand, the discrete GPU preferred, the `device` option. |
| 1.0.5 | Heap corruption when a thread of the host program ended, fixed. The first version that passed everywhere. Linux and macOS builds added. |
| 1.0.4 | Floating-point exceptions also masked on the engine's worker threads. |
| 1.0.2 – 1.0.3 | The floating-point settings are saved and restored through the processor registers directly; MinGW's own functions did not cover SSE. |
| 1.0.1 | The library refuses to load a model on a processor without the instructions the build needs; before, that was a crash. Crash reporter added. |
| 1.0.0 | First library, with the Delphi binding and the tests. |

## What the engine is

Every version is built on [laya.cpp](https://github.com/lkarlslund/laya.cpp) at commit
`e632da0`, with ggml as it is pinned there. The CPU backend gives the same reference answer
in 1.0.5 and in 1.0.14: 0.8364 for the refund example.
