# Options

The second argument of `laya_create` is a JSON object with settings. `NULL`, an empty string
or `{}` means the defaults: run on the CPU, full precision, all cores.

```c
laya_create("/models/laya", "{\"backend\":\"vulkan\",\"precision\":\"fp16\"}");
```

| Key | Values | Default |
|---|---|---|
| [`backend`](#backend-cpu-or-gpu) | `"cpu"`, `"vulkan"`, `"cuda"` | `"cpu"` |
| [`precision`](#precision) | `"fp32"`, `"fp16"`, `"bf16"` | `"fp32"` |
| [`device`](#device-which-gpu) | a GPU number, or part of its name | the first discrete GPU |
| [`threads`](#threads) | 0 to 1024 | 0, meaning all cores |
| [`variant`](#variant-which-model) | `"english"`, `"multilingual"`, `"typed-decisions"` | the folder as given |
| [`allow_truncation`](#allow_truncation) | `true`, `false` | `false` |
| [`flash`](#flash-and-tensor_core) | `true`, `false` | `true` with `fp16` or `bf16`, otherwise `false` |
| [`tensor_core`](#flash-and-tensor_core) | `true`, `false` | `false` |

A key that is not in this table is refused (`Unknown option: …`), so a typing mistake is
reported and not silently ignored.

Options are fixed when the model is loaded. To change one, destroy the agent and create
another.

## backend: CPU or GPU

| Value | Runs on | Available in |
|---|---|---|
| `"cpu"` | The processor. | Every package. |
| `"vulkan"` | The GPU, through Vulkan: NVIDIA, AMD and Intel GPUs on Windows and Linux, Apple GPUs on macOS (through MoltenVK, which is included in that package). | The `vulkan` packages: Windows x64, Linux x86-64, macOS on Apple Silicon. |
| `"cuda"` | NVIDIA GPUs through CUDA. | No released package. The code is there for a build from source, which has not been tried. |

A library only has the backends that were compiled into it. `laya_version()` lists them, for
example `laya_c 1.0.14 (api 1; backends: cpu,vulkan)`. Asking for one that is missing fails
with `This build has no Vulkan backend`.

A `vulkan` package also works as a CPU library: it opens the system's Vulkan loader only
when the `vulkan` backend is asked for. On a machine without a GPU or without Vulkan,
`laya_create` then fails with a clear message and you can fall back to `"cpu"`:

```c
laya_agent* agent = laya_create(dir, "{\"backend\":\"vulkan\",\"precision\":\"fp16\"}");
if (!agent) agent = laya_create(dir, "{\"backend\":\"cpu\"}");
```

## precision

How many bits the model's arithmetic uses on the GPU.

| Value | Meaning | Where it works |
|---|---|---|
| `"fp32"` | Full precision, 32-bit. | CPU and GPU. The only choice on the CPU. |
| `"fp16"` | Half precision, 16-bit. | GPU through Vulkan. |
| `"bf16"` | Half precision in the "bfloat16" format. | GPU. |

**On a GPU, use a half precision.** It is the fastest by a wide margin, and in the measured
runs its probabilities stayed within about 0.003 of the CPU's:

| Machine | `fp32` | Half precision |
|---|---:|---:|
| NVIDIA RTX 5080 Laptop GPU | about 230 questions per second | about 670 (`fp16`) |
| Apple M3 Ultra | 111 | 170 (`bf16`) |

On macOS, prefer `bf16` or `fp16` for a second reason: with `fp32` the library has to split
large batches to work around a fault in the Vulkan layer there (see
[Troubleshooting](troubleshooting.md#known-problems)).

Combinations that are refused:

| Request | Message |
|---|---|
| `fp16` with the `cpu` or `cuda` backend | `FP16 currently requires Vulkan` |
| `bf16` with the `cpu` backend | `BF16 mode requires a GPU; CUDA requires fused attention` |

## device: which GPU

Only for the `vulkan` backend, on machines with more than one GPU.

```json
{"backend": "vulkan", "device": 1}
{"backend": "vulkan", "device": "RTX"}
```

A number selects a GPU by its position in the system's list, starting at 0. A string selects
the first GPU whose name contains it. Without this option the library takes the first
discrete GPU, and the first GPU of any kind when there is no discrete one, so a laptop with
an integrated and a dedicated GPU uses the dedicated one.

| Message | Cause |
|---|---|
| `Vulkan device 2 does not exist (2 found)` | The number is too high. |
| `No Vulkan GPU matches "RTX" (found …)` | No GPU has that text in its name; the message lists the names that exist. |
| `No usable Vulkan GPU found` | Vulkan is installed but offers no GPU. |

`laya_info` shows which GPU was chosen.

## threads

Only for the `cpu` backend: how many processor threads the engine uses. 0, the default, means
all of them.

```json
{"backend": "cpu", "threads": 4}
```

Lower it when the library must share the machine with other work, for example a server that
also handles requests on the same cores.

## variant: which model

The Hugging Face repository holds three models: `english` in the top folder, `multilingual`
and `typed-decisions` in subfolders. There are two ways to say which one to load.

Give the model's own folder and no `variant`:

```c
laya_create("/models/laya/multilingual", "");
```

Or give the top folder and name the variant, which is convenient when the user chooses:

```c
laya_create("/models/laya", "{\"variant\":\"multilingual\"}");
```

`"english"` leaves the folder as it is. The other two add their name to the path, unless the
folder you gave is already a model folder and has no subfolder of that name.

Only the English model has been run through LibLayaX so far.

## allow_truncation

By default a request whose question or text is too long for the model is refused with an
error. With `"allow_truncation": true` the library cuts what does not fit and answers anyway.
The answer is then based on the beginning of the text only. See
[Requests and answers](requests.md#size-limits).

## flash and tensor_core

Two settings of the engine's GPU code that rarely need touching.

* `flash` turns on fused attention. It is switched on automatically with `fp16` and `bf16`
  and off with `fp32`. With the `vulkan` backend, `fp32` together with `flash` is refused
  (`Vulkan currently supports FP32 without fused attention`).
* `tensor_core` is for the `cuda` backend with `fp32`. It is refused on the CPU and with a
  half precision (`Compensated matrix operations require a GPU and FP32 mode`).

## Choosing settings

| Situation | Options |
|---|---|
| Works everywhere, no GPU needed | `{}` |
| Desktop PC or laptop with an NVIDIA, AMD or Intel GPU | `{"backend":"vulkan","precision":"fp16"}` |
| Mac with Apple Silicon | `{"backend":"vulkan","precision":"bf16"}` |
| Server shared with other work | `{"backend":"cpu","threads":4}` |
| Texts of unknown length, an answer wanted in every case | add `"allow_truncation":true` |

The GPUs the library has actually been run on are an NVIDIA RTX 5080 Laptop GPU and an NVIDIA
GeForce RTX 3050 (both on Windows) and an Apple M3 Ultra (macOS). `laya-diag`, in every package, compares the GPU's answers
with the CPU's on your machine; run it once before relying on a GPU you have not used before
([Testing](testing.md)).
