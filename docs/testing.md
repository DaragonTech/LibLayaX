# Testing

Every binary package comes with small command-line tools that check the library on the
machine in front of you. They need nothing but the library next to them and, for most
checks, a model folder.

| Tool | What it answers | Needs the model |
|---|---|---|
| `laya-probe` | Does this machine's C++ runtime behave as the library expects? | no |
| `test-capi` | Does the library keep its contract, and does it give the reference answers? | optional |
| `laya-bench` | How fast is it here, on each backend? | yes |
| `laya-diag` | Does the GPU give the same answers as the CPU? | yes |
| `laya-trace` | Where inside the model do two backends start to differ? | yes |

On Linux, run them from the package folder with `LD_LIBRARY_PATH=.` in front, so that they
find `liblaya.so`; on macOS and Windows they find the library next to themselves. On Linux
and macOS, make them executable first
(`chmod +x test-capi laya-bench laya-diag laya-trace laya-probe`).

## On Windows: one command

```
run-tests.bat C:\models\laya            CPU
run-tests.bat C:\models\laya vulkan     GPU
```

It runs `laya-probe`, `test-capi`, `LayaTests.exe` if it is there, and `laya-bench`, and
prints everything as it happens. To keep a copy: `run-tests.bat C:\models\laya > log.txt 2>&1`.

## test-capi: the test suite

```
test-capi                                        26 contract checks, no model needed
LAYA_TEST_MODEL=/models/laya test-capi           plus the tests with the model: 54 checks
LAYA_TEST_BACKEND=vulkan …                       the same on the GPU, plus a GPU-against-CPU check
```

| Variable | Effect |
|---|---|
| `LAYA_TEST_MODEL` | The model folder. Without it only the contract checks run. |
| `LAYA_TEST_BACKEND` | `cpu` (default), `vulkan` or `cuda`. |
| `LAYA_TEST_QUICK=1` | Fewer repetitions in the thread test, for slow machines and emulators. |
| `LAYA_TEST_DUMP=file` | Also writes the answers to a file, for comparing two platforms. Adds one check. |

The last line is the result, for example `54 checks, 0 failures`. The exit code is 0 when
everything passed.

What it covers:

* **The contract**, without a model: wrong paths, wrong options, `NULL` arguments, error
  messages, the version functions.
* **With a model**: the three question types, batches, Unicode text, JSON objects as text,
  that the same request gives the same answer, malformed requests, text that is too long,
  four threads sharing one model, unloading and loading again.
* **Floating-point settings**: the test switches floating-point exceptions on, the way a
  Delphi program does, and checks that they are unchanged after the calls.

With the English model the suite also checks the reference answer: `noul` 0.8364 for the
refund example, on every platform.

## laya-bench: speed

```
laya-bench /models/laya                                         every backend the library has
laya-bench /models/laya '{"backend":"vulkan","precision":"fp16"}'    one setting
```

For each setting it loads the model, times one question alone and a batch of 16, and checks
the answers against the CPU's:

```
laya_c 1.0.6 (api 1; backends: cpu,vulkan)

== {"backend":"cpu"}
   device: {"backend":"CPU","device":"Apple M3 Ultra", …}
   load: 586 ms
   single question:   129.7 ms
   batch of 16:        65.9 ms per question  (15 questions/s)
   answers: noul=0.8364 cancel=0.9900  (reference)
```

A GPU setting whose answers differ from the reference is reported, and the exit code is then
not 0. The speed table in the project README was made with this tool.

## laya-diag: is the GPU right?

```
laya-diag /models/laya                                          GPU, full precision
laya-diag /models/laya '{"backend":"vulkan","precision":"fp16"}'
```

It loads the model twice, on the CPU and with the options given, sends 21 kinds of request
to both (one question, several, equal and different lengths, small and large batches) and
compares the answers. Every case must say `OK`.

Run it once on any GPU you have not used with the library before. It is what found the macOS
problem described under [Known problems](troubleshooting.md#known-problems). With
`LAYA_DIAG_VERBOSE=1` it prints both answers for a case that differs.

## laya-probe: the machine itself

```
laya-probe
```

It does not load the library or a model. It checks, one numbered step at a time, the things
the library relies on: atomics, thread-local storage, the floating-point environment, the
arithmetic the engine does when it starts, mutexes, C++ exceptions, threads. Each step is
announced before it runs, so if the program stops, the last line shows where. It ends with
`probe finished: everything works`.

It is the first thing to run when the library misbehaves on a new kind of machine: it tells
apart "the library has a fault" from "this system's runtime does".

## laya-trace: for engine problems

```
laya-trace /models/laya [SEQUENCES] [OPTIONS_JSON]
```

Runs the same request on the CPU and on another backend and compares the model's internal
values layer by layer, to find the first operation that differs. It is meant for people
working on the engine. It is not in the Windows packages.

## Tests in the source tree

| File in `laya.cpp/tests/capi/` | Purpose |
|---|---|
| `test_capi.c` | The suite above. With CMake: `ctest -R capi`. |
| `test_parity.py` | Loads the library from Python and checks that its answers are byte-identical to the `laya-cli` tool of laya.cpp. With `--compare-dump` it also compares with a dump made on another platform. |
| `make_synthetic_model.py` | Writes a model with the real architecture and a real tokenizer but random weights: `python tests/capi/make_synthetic_model.py DIR`. It exercises the whole pipeline without the 800 MB download. Its answers mean nothing. |

The language bindings have their own tests, in their own repositories
([Language bindings](bindings.md)).
