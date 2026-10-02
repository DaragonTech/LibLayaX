# Language bindings

The library has a C interface, and almost every programming language can call C. A binding
is the small piece of code that does that for one language: it declares the ten functions and
usually adds a convenient type on top.

## Ready-made bindings

Three bindings exist as projects of their own. Their source is not in this repository and
not in the binary packages; each has its own README, tests and example.

| Language | Project | What you get |
|---|---|---|
| C, C++ | none needed | `laya_c.h`, in this repository and in every package. |
| Delphi, Free Pascal | **DLaya** | The unit `Laya.pas` with the class `TLayaAgent`. |
| Rust | **RLaya** | The crate `rlaya` with the type `Agent`. |
| Lua 5.1 to 5.4 | **LLaya** | The C module `llaya`; ready-built for Lua 5.1. |

All three load the same library files and use the same requests and answers as described in
[Requests and answers](requests.md). They add helpers for the common single question, so
that no JSON has to be written by hand for it.

Delphi and Free Pascal:

```pascal
Agent := TLayaAgent.Create('C:\models\laya', '{"backend":"cpu"}');
Json  := Agent.AskYesNo('Please refund the duplicate charge.',
                        'Does the customer ask for a refund?', 'refund');
```

Rust:

```rust
let agent = Agent::new("/models/laya", r#"{"backend":"cpu"}"#)?;
let json = agent.ask_yes_no("Please refund the duplicate charge.",
                            "Does the customer ask for a refund?", "refund")?;
```

Lua:

```lua
local agent = assert(llaya.new("/models/laya", { backend = "cpu" }))
local answer = assert(agent:ask_yes_no_table("Please refund the duplicate charge.",
                                             "Does the customer ask for a refund?", "refund"))
print(answer.results[1].answers.refund.noul)
```

How far each has been tested is stated in its own README. In short: DLaya has run with the
real model on Windows x64 and has been compiled with Free Pascal but not yet with Embarcadero
Delphi; RLaya has been built and tested on Linux only; LLaya has run with the real model on
Windows ARM64, Windows x64 (under emulation on Windows on ARM), macOS and Linux ARM64.

## Any other language

C#, Python, Go, Java, Swift, Zig, Nim and others can load a C library through their own
mechanism (P/Invoke, `ctypes`, cgo, the Foreign Function API, and so on). What has to be
declared is small:

```c
const char* laya_version(void);
int         laya_api_version(void);
laya_agent* laya_create(const char* model_dir_utf8, const char* options_json);
char*       laya_predict(laya_agent* agent, const char* request_json);
char*       laya_prepare(laya_agent* agent, const char* request_json);
char*       laya_info(laya_agent* agent);
void        laya_free_string(char* s);
void        laya_destroy(laya_agent* agent);
const char* laya_last_error(void);
void        laya_set_log_callback(laya_log_fn fn, void* user, int min_level);
```

`laya_agent*` is an opaque pointer: treat it as a pointer-sized handle. The exact rules for
each function are in [The API](api.md).

### A complete example: Python

This uses only Python's standard library. It was run against LibLayaX 1.0.14 on Linux.

```python
import ctypes, json, sys

lib = ctypes.CDLL("./liblaya.so")          # "laya.dll" on Windows, "./liblaya.dylib" on macOS

lib.laya_version.restype = ctypes.c_char_p
lib.laya_last_error.restype = ctypes.c_char_p
lib.laya_create.restype = ctypes.c_void_p
lib.laya_create.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
lib.laya_predict.restype = ctypes.c_void_p             # not c_char_p: the pointer must be freed
lib.laya_predict.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.laya_free_string.argtypes = [ctypes.c_void_p]
lib.laya_destroy.argtypes = [ctypes.c_void_p]

def predict(agent, request):
    pointer = lib.laya_predict(agent, json.dumps(request).encode("utf-8"))
    if not pointer:
        raise MemoryError("laya_predict returned NULL")
    try:
        reply = json.loads(ctypes.string_at(pointer).decode("utf-8"))
    finally:
        lib.laya_free_string(pointer)
    if "error" in reply:
        raise ValueError(reply["error"])
    return reply

print(lib.laya_version().decode())
agent = lib.laya_create(sys.argv[1].encode("utf-8"), b'{"backend":"cpu"}')
if not agent:
    sys.exit("cannot load the model: " + lib.laya_last_error().decode())

reply = predict(agent, {
    "state": "Please refund the duplicate charge.",
    "questions": {"refund": {"type": "noul",
                             "instructions": "Does the customer ask for a refund?"}}})
print(reply["results"][0]["answers"]["refund"]["noul"])
lib.laya_destroy(agent)
```

Python programs have the original Laya library as well, which is the reference
implementation. Calling LibLayaX from Python makes sense when PyTorch is not wanted on the
machine, or as a quick way to try the library.

## Writing a binding: the points that matter

The example above shows all of them.

1. **Strings are UTF-8.** Convert to UTF-8 on the way in and from UTF-8 on the way out. On
   Windows this applies to paths too: do not pass the system's ANSI encoding.
2. **Keep the pointer, then free it.** `laya_predict`, `laya_prepare` and `laya_info` return
   memory that must go back through `laya_free_string`. Many languages offer to convert a
   returned `char*` into a string automatically and then lose the pointer. Declare the
   result as a raw pointer, copy the text, then free it. Free it on error paths as well.
3. **Do not free the other two.** `laya_version` and `laya_last_error` return strings the
   library owns.
4. **Copy `laya_last_error` at once.** It is valid until the next `laya_` call on the same
   thread, and it is kept per thread: read it on the thread where the call failed.
5. **Check both kinds of failure.** `laya_create` fails with `NULL`. `laya_predict` fails
   with a JSON object that has an `error` key. A binding usually turns both into the
   language's own error mechanism.
6. **Build requests with a JSON library.** Text placed into a request by hand must have its
   quotes, backslashes and control characters escaped; a JSON library does that.
7. **Free the agent deterministically if you can.** A loaded model holds about 1.7 GB. Tie
   `laya_destroy` to the language's scope or dispose mechanism where there is one, with the
   garbage collector as a fallback only.
8. **Threads.** An agent can be shared; the library serializes the calls. In a language with
   a global interpreter lock, release it around `laya_predict` if the language allows
   (`ctypes` does this by default).
9. **Check the version.** `laya_api_version()` returns 1 for the interface described here.
   A binding can refuse to run against a library that reports something else.
10. **The log callback is called from other threads.** If the language cannot safely be
    entered from a foreign thread, do not offer the callback, or route it through a queue.
    None of the ready-made bindings wraps it: DLaya and RLaya expose only the raw function,
    and LLaya leaves it out.

On Windows the functions use the C calling convention. In a 64-bit program there is only one
calling convention, so a `stdcall` declaration works equally.
