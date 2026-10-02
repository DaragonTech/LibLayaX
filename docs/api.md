# The API

The whole interface is ten C functions, declared in `laya_c.h` (94 lines; in the source tree
it is `laya.cpp/capi/laya_c.h`, and every binary package has a copy). This is API version 1.

| Function | Purpose |
|---|---|
| [`laya_create`](#laya_create) | Load a model. |
| [`laya_predict`](#laya_predict) | Answer one request or several. |
| [`laya_prepare`](#laya_prepare) | Show how a request is turned into model input, without running the model. |
| [`laya_info`](#laya_info) | Describe a loaded model: backend, device, limits. |
| [`laya_free_string`](#laya_free_string) | Release a string returned by the three functions above. |
| [`laya_destroy`](#laya_destroy) | Unload a model. |
| [`laya_last_error`](#laya_last_error) | The reason for the last failure on this thread. |
| [`laya_set_log_callback`](#laya_set_log_callback) | Receive the library's log messages. |
| [`laya_version`](#laya_version-and-laya_api_version), [`laya_api_version`](#laya_version-and-laya_api_version) | Identify the library. |

## Rules that apply to every function

* **Text is UTF-8.** Every string that goes in or comes out is UTF-8 and ends with a zero
  byte. This includes file paths, on Windows too: non-ASCII paths work.
* **Requests and answers are JSON text.** No structures cross the boundary, which is what
  keeps bindings small.
* **Nothing is thrown.** A failure is reported as a `NULL` result plus `laya_last_error()`,
  or as a JSON object `{"error":"..."}`. No C++ exception leaves the library. A failed call
  leaves the agent usable.
* **Who owns a string.** Strings returned as `char*` (from `laya_predict`, `laya_prepare`,
  `laya_info`) are yours: release each one with `laya_free_string`, not with your own
  `free`. Strings returned as `const char*` (`laya_version`, `laya_last_error`) belong to the
  library: do not free them.
* **64-bit only.** There is no 32-bit build. On Windows the functions are `__cdecl`; in a
  64-bit program there is only one calling convention, so `cdecl` and `stdcall` declarations
  on the caller's side are the same thing.
* **Your floating-point settings are safe.** Each call masks floating-point exceptions while
  it runs and puts your settings back before it returns. Programs that run with those
  exceptions enabled (Delphi and Free Pascal programs do) need no special code.

## laya_create

```c
laya_agent* laya_create(const char* model_dir_utf8, const char* options_json);
```

Loads a model and returns a handle to it, called an agent.

| Argument | Meaning |
|---|---|
| `model_dir_utf8` | The model folder: the one that contains `rl_agent_config.json`. With the `variant` option it can also be the folder above the variants. |
| `options_json` | A JSON object with settings, or `NULL` or `""` for the defaults (CPU, full precision, all cores). See [Options](options.md). |

**Returns** the agent, or `NULL` when the model could not be loaded. After a `NULL`, call
`laya_last_error()` for the reason.

Loading takes about a second from a fast disk and about 1.7 GB of memory for the English
model on the CPU. Load once and keep the agent for the life of the program; do not create an
agent per question.

Several agents can exist at the same time (two models, or one model on the CPU and one on
the GPU). Each takes its own memory.

Typical reasons for `NULL`:

| `laya_last_error()` | Cause |
|---|---|
| `model_dir is empty` | `NULL` or empty path. |
| `Cannot open …/rl_agent_config.json` | The folder is not a model folder. |
| `Unknown option: …` | A key in `options_json` that the library does not know. Typing mistakes are reported, not ignored. |
| `This build has no Vulkan backend` | A GPU was requested from a CPU-only library. |
| `This laya build needs CPU instructions your processor … does not provide: …` | An `avx2` library on a processor without AVX2. Use the `compat-sse42` one. |

The full list is in [Troubleshooting](troubleshooting.md).

## laya_predict

```c
char* laya_predict(laya_agent* agent, const char* request_json);
```

Runs the model.

| Argument | Meaning |
|---|---|
| `agent` | An agent from `laya_create`. |
| `request_json` | One request object, or an array of them. The format is described in [Requests and answers](requests.md). |

**Returns** a JSON string that you release with `laya_free_string`:

* on success, `{"results":[...], "elapsed_ms":…, "backend":"…", "device":"…"}`, with one
  entry in `results` per request, in the same order;
* on failure, `{"error":"..."}`. The same message is then also available from
  `laya_last_error()`.

It returns `NULL` only when the library cannot allocate memory for the answer.

A failed request (malformed JSON, an unknown question type, a text that is too long) does not
damage the agent; the next call works normally. When an array of requests contains one bad
request, the whole call fails and no results are returned.

## laya_prepare

```c
char* laya_prepare(laya_agent* agent, const char* request_json);
```

Takes the same requests as `laya_predict` but stops before running the model. It returns the
token numbers the model would have received:

```json
{"batch":1,"length":38,"options":2,"ids":[1,285,366, …],
 "lengths":[38],"markers":[16,26],"counts":[2],"types":[2]}
```

This is a debugging aid. It is useful for two things: seeing how many tokens a text takes
(`lengths`), and checking whether a request is accepted without paying for inference. The
result is released with `laya_free_string`; failures come back as `{"error":"..."}`.

## laya_info

```c
char* laya_info(laya_agent* agent);
```

Describes a loaded model:

```json
{"backend":"CPU","device":"Intel(R) Core(TM) Ultra 9 275HX",
 "model_dir":"D:\\models\\laya","model_name":"rl-agent","variant":"english",
 "max_len":512,"head_max_len":192}
```

| Field | Meaning |
|---|---|
| `backend` | What runs the model, as the engine names it: `CPU`, or for example `Vulkan0` for the first GPU. |
| `device` | The processor or GPU, by name. |
| `model_dir` | The folder the model was loaded from, after the `variant` option was applied. |
| `model_name` | The name in the model's own configuration. |
| `variant` | `english`, `multilingual` or `typed-decisions`, worked out from the model's configuration. |
| `max_len` | The most tokens one question plus its text may take. |
| `head_max_len` | The most tokens the question part (instructions and options) may take. |

The limits are explained in [Requests and answers](requests.md#size-limits). The result is
released with `laya_free_string`.

## laya_free_string

```c
void laya_free_string(char* s);
```

Releases a string returned by `laya_predict`, `laya_prepare` or `laya_info`. Passing `NULL`
is allowed and does nothing. Do not pass strings from `laya_version` or `laya_last_error`.

Use this function and not your language's own `free`: the library and your program may use
different memory allocators.

## laya_destroy

```c
void laya_destroy(laya_agent* agent);
```

Unloads the model and releases its memory. Passing `NULL` is allowed. Do not call it while
another thread is still inside a call on the same agent.

## laya_last_error

```c
const char* laya_last_error(void);
```

The message for the most recent failure **on the calling thread**, or an empty string when
the last call on this thread succeeded. It is never `NULL`.

The string belongs to the library and stays valid only until the next `laya_` call on the
same thread, so copy it if you want to keep it. Because it is kept per thread, one thread's
failure never overwrites another's message.

## laya_set_log_callback

```c
typedef void (*laya_log_fn)(int level, const char* text_utf8, void* user);
void laya_set_log_callback(laya_log_fn fn, void* user, int min_level);
```

By default the library writes warnings and errors to the standard error stream. This function
sends them to your own function instead, for example to put them in your application's log.

| Argument | Meaning |
|---|---|
| `fn` | Your function. `NULL` restores the default. |
| `user` | Any pointer; it is handed back to `fn` unchanged. |
| `min_level` | Messages below this level are dropped. |

Levels: 1 debug, 2 info, 3 warning, 4 error. Level 5 means "continuation of the previous
line" and is always delivered.

The setting applies to the whole process, not to one agent. Call it before `laya_create`.
Your function may be called from the engine's worker threads, so it must be safe to call from
any thread, and it should return quickly.

## laya_version and laya_api_version

```c
const char* laya_version(void);
int laya_api_version(void);
```

`laya_version` returns a line such as `laya_c 1.0.14 (api 1; backends: cpu,vulkan)`: the
library version, the API version, and the backends compiled into this particular file. It is
the quickest way to find out which library a program actually loaded.

`laya_api_version` returns the API version as a number, 1 for everything described here.
A binding can compare it with the version it was written for (`LAYA_C_API_VERSION` in the
header) and refuse to continue when they differ.

## Threads

* One agent may be shared between threads. The library lets one call at a time run on an
  agent and makes the others wait, so sharing is safe but does not make things faster.
* For speed, put several requests in one `laya_predict` call (an array). On a GPU this
  multiplies the throughput; see [Requests and answers](requests.md#batches).
* Different agents run independently of each other.
* On the CPU the engine already uses all cores for one call (the `threads` option changes
  that), so calling from many threads at once gains nothing there either.
* In a program with a user interface, call the library from a background thread. Loading a
  model takes about a second and a CPU answer takes tens of milliseconds or more.

## When the library ends the process

One case is not reported as an error: an internal check of the engine failing (a ggml
"assert"). The message is sent to the log callback first, then the process ends. This should
not happen with a model that loaded correctly; it is listed here so it is not a surprise.
