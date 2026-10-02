# LibLayaX documentation

LibLayaX is a library that runs the Laya AI model inside your own program. These pages are
about using the library; the [project README](../README.md) says what the project is and why
it exists.

## Start here

| Page | Read it when |
|---|---|
| [Getting started](getting-started.md) | You want a first answer from the model: pick a package, download the model, compile ten lines of C. |
| [Requests and answers](requests.md) | You are writing the questions: the three question types, several questions at once, batches, every field of the answer, the size limits. |

## Reference

| Page | Content |
|---|---|
| [The API](api.md) | The ten functions, one by one: arguments, results, who frees what, threads, errors, logging. |
| [Options](options.md) | What can be passed to `laya_create`: CPU or GPU, precision, threads, which GPU, which model variant. |
| [Platforms and packages](platforms.md) | Which library file goes with which system, how to ship it with an application, what each one needs, what has been tested. |
| [Language bindings](bindings.md) | The ready-made bindings (Delphi, Rust, Lua) and how to call the library from any other language, with a Python example. |

## When you need it

| Page | Content |
|---|---|
| [Troubleshooting](troubleshooting.md) | Error messages and what they mean, the debug trace, crash reports, known problems. |
| [Testing](testing.md) | The test and benchmark tools that come in every package. |
| [Building from source](building.md) | Building the library for each platform, and what LibLayaX adds to laya.cpp. |
| [History](history.md) | What changed in each version. |

## The short version

```c
laya_agent* agent = laya_create("/models/laya", "{\"backend\":\"cpu\"}");
char* answer = laya_predict(agent,
    "{\"state\":\"Please refund the duplicate charge.\","
    " \"questions\":{\"refund\":{\"type\":\"noul\","
    "   \"instructions\":\"Does the customer ask for a refund?\"}}}");
/* {"results":[{"answers":{"refund":{"type":"noul","noul":0.8364, ...}}}], ...} */
laya_free_string(answer);
laya_destroy(agent);
```

Load the model once, ask as many questions as you like, get JSON back. Everything else in
these pages is detail around those four calls.

**Names.** LibLayaX is the name of the project. The library files are `laya.dll` (Windows),
`liblaya.so` (Linux) and `liblaya.dylib` (macOS), the header is `laya_c.h`, and every function
starts with `laya_`.
