# Getting started

Three things are needed: the library for your system, the model, and a program that calls it.
This page uses C because the library's interface is C; for Delphi, Rust and Lua there are
ready-made bindings ([Language bindings](bindings.md)), and the steps for the library and the
model are the same.

## 1. Get the library

Download the package for your system from the releases page and unzip it.

| Your program runs on | Package |
|---|---|
| Windows x64, processor with AVX2 (Intel since 2013, AMD since Zen) | `laya-windows-…-avx2` |
| Windows x64, any processor; or an x64 program on Windows on ARM | `laya-windows-…-compat-sse42` |
| Windows x64 with a GPU | `laya-windows-…-vulkan` |
| Windows ARM64, native program | `laya-windows-arm64-…` |
| Linux x86-64 | `laya-linux-…` (folders `avx2`, `compat-sse42`, `vulkan`) |
| Linux ARM64 | `laya-linux-arm64-…` |
| macOS, Apple Silicon | `laya-macos-…-arm64`, or `laya-macos-vulkan-…-arm64` for the GPU |
| macOS, Intel | `laya-macos-…-x86_64-avx2` or `…-x86_64-compat-sse42` |

Choose by the architecture of **your program**, not of the machine: an x64 program running
on Windows on ARM needs the x64 `compat-sse42` library. [Platforms and packages](platforms.md)
has the details.

Each package contains the library (`laya.dll`, `liblaya.so` or `liblaya.dylib`), the header
`laya_c.h`, and a few test tools. The Windows packages also contain `laya.lib` and `laya.def`
for linking.

On Linux and macOS, make the tools executable after unzipping. On macOS also remove the
download quarantine, or the system refuses to load the library:

```
chmod +x test-capi laya-bench laya-diag laya-trace laya-probe
xattr -dr com.apple.quarantine .          # macOS only
```

## 2. Get the model

The library does not include the model. The weights are published by the Laya project on
Hugging Face, in the repository
[convaiinnovations/laya](https://huggingface.co/convaiinnovations/laya). The English model is
a download of about 800 MB.

With the Hugging Face command-line tool:

```
pip install huggingface_hub
huggingface-cli download convaiinnovations/laya --local-dir /models/laya \
    --include "model.safetensors" "rl_agent_config.json" "encoder/*" "tokenizer/*"
```

Or with a browser: open the repository page, go to "Files", and download these five files,
keeping the folders:

```
model.safetensors                 the weights (about 800 MB)
rl_agent_config.json
encoder/config.json
tokenizer/tokenizer.json
tokenizer/tokenizer_config.json
```

The folder that contains `rl_agent_config.json` is the **model folder**. That is the path
you give to the library. In the examples below it is `/models/laya`.

The repository also holds two other variants, `multilingual` and `typed-decisions`, in
subfolders of those names, with the same five files each. See `variant` in
[Options](options.md).

The model is the Laya project's work and comes with its own terms; see the model card on the
repository page.

## 3. Check that it works

Before writing any code, run the test suite that came in the package against your model.

Windows:

```
run-tests.bat C:\models\laya
```

Linux and macOS, from the package folder:

```
LAYA_TEST_MODEL=/models/laya LD_LIBRARY_PATH=. ./test-capi      Linux
LAYA_TEST_MODEL=/models/laya ./test-capi                        macOS
```

It should finish with `54 checks, 0 failures`. If it does not, see
[Troubleshooting](troubleshooting.md).

## 4. A first program

Save this as `first.c` in the folder where you unzipped the package:

```c
#include <stdio.h>
#include "laya_c.h"

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: first MODEL_FOLDER\n"); return 2; }
    printf("%s\n", laya_version());

    laya_agent* agent = laya_create(argv[1], "{\"backend\":\"cpu\"}");
    if (!agent) { fprintf(stderr, "cannot load the model: %s\n", laya_last_error()); return 1; }

    char* answer = laya_predict(agent,
        "{\"state\":\"Please refund the duplicate charge.\","
        " \"questions\":{\"refund\":{\"type\":\"noul\","
        "   \"instructions\":\"Does the customer ask for a refund?\"}}}");
    if (answer) { puts(answer); laya_free_string(answer); }

    laya_destroy(agent);
    return 0;
}
```

Compile and run it:

| System | Compile | Run |
|---|---|---|
| Linux | `cc first.c -I. -L. -llaya -o first` | `LD_LIBRARY_PATH=. ./first /models/laya` |
| macOS | `cc first.c -I. -L. -llaya -Wl,-rpath,@executable_path -o first` | `./first /models/laya` |
| Windows, MinGW | `gcc first.c -I. -L. -llaya -o first.exe` | `first.exe C:\models\laya` |
| Windows, Visual C++ | `cl first.c laya.lib` | `first.exe C:\models\laya` |

It prints the library version and then the answer:

```json
{"results":[{"model":"laya-rl-agent",
             "answers":{"refund":{"type":"noul","confidence":0.8364,
                                  "action":{"act_probability":1.0},"noul":0.8364}},
             "usage":{"input_tokens":40,"output_tokens":0}}],
 "elapsed_ms":117.8,"backend":"CPU","device":"Intel(R) Core(TM) Ultra 9 275HX"}
```

`noul` is the probability that the answer is yes: 0.8364 for this text with the English
model, on every platform.

## What the program did

1. **`laya_create`** loaded the model. This takes about a second from a fast disk and about
   1.7 GB of memory. A real application does it once, at start-up, and keeps the agent for as
   long as it runs.
2. **`laya_predict`** took a request (a text and one or more questions about it) and returned
   the answers, both as JSON text.
3. **`laya_free_string`** released the answer. Every string that `laya_predict`,
   `laya_prepare` or `laya_info` returns must be released this way.
4. **`laya_destroy`** unloaded the model.

## Where to go next

* [Requests and answers](requests.md): multiple-choice and score questions, several questions
  about one text, many texts in one call.
* [Options](options.md): running on the GPU.
* [The API](api.md): the exact rules for each function.
* [Platforms and packages](platforms.md): shipping the library with your application.
