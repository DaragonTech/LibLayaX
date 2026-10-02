/* laya_c.h - flat C ABI over laya.cpp for FFI consumers (Delphi, C#, Python ctypes, ...).
 *
 * Design rules
 *  - Everything crosses the boundary as UTF-8, NUL-terminated JSON text. No C++ types leak.
 *  - Strings returned by laya_predict / laya_prepare / laya_info are heap-allocated by the
 *    library and MUST be released with laya_free_string. `const char*` results are owned by the
 *    library.
 *  - No C++ exception ever escapes; failures are reported as NULL (+ laya_last_error) or as a
 *    JSON object {"error": "..."}.
 *  - One agent may be shared between threads: calls on the same agent are serialized inside the
 *    library (upstream requirement). Separate agents run independently.
 *  - 64-bit only. On Win64 there is a single calling convention, so cdecl/stdcall declarations
 *    on the caller side are equivalent.
 */
#ifndef LAYA_C_H
#define LAYA_C_H

#ifdef _WIN32
#  define LAYA_CALL __cdecl
#  ifdef LAYA_C_BUILD
#    define LAYA_API __declspec(dllexport)
#  else
#    define LAYA_API __declspec(dllimport)
#  endif
#else
#  define LAYA_CALL
#  define LAYA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LAYA_C_API_VERSION 1

typedef struct laya_agent laya_agent;

/* Log levels match ggml: 1=debug 2=info 3=warn 4=error 5=continuation of previous line. */
typedef void (LAYA_CALL *laya_log_fn)(int level, const char* text_utf8, void* user);

/* Library identification, e.g. "laya_c 1.0.0 (api 1; backends: cpu,vulkan)". Never NULL. */
LAYA_API const char* LAYA_CALL laya_version(void);

/* Returns LAYA_C_API_VERSION of the binary; compare with the header you compiled against. */
LAYA_API int LAYA_CALL laya_api_version(void);

/* Route library and engine log output. NULL restores the default (warnings+errors to stderr).
 * Messages below min_level are dropped. Process-wide; call before laya_create. The callback may
 * be invoked from worker threads. */
LAYA_API void LAYA_CALL laya_set_log_callback(laya_log_fn fn, void* user, int min_level);

/* Load a checkpoint. model_dir is a UTF-8 path to either a checkpoint directory (contains
 * rl_agent_config.json) or a model store root combined with the "variant" option.
 * options_json may be NULL or "" for defaults:
 *   "backend":   "cpu" (default) | "cuda" | "vulkan"
 *   "variant":   "english" | "multilingual" | "typed-decisions"  (appends the variant folder
 *                when model_dir is a store root; ignored when model_dir is itself a checkpoint)
 *   "precision": "fp32" (default) | "fp16" | "bf16"
 *   "flash": bool, "tensor_core": bool   (CUDA fast path: both true)
 *   "allow_truncation": bool             (default false: over-long requests are rejected)
 *   "threads": int                       (CPU backend worker threads; default = all cores)
 *   "device": int | "name part"          (Vulkan GPU: index, or part of its name such as "RTX";
 *                                         default = first discrete GPU, else the first GPU)
 * Returns NULL on failure; call laya_last_error() for the reason. */
LAYA_API laya_agent* LAYA_CALL laya_create(const char* model_dir_utf8, const char* options_json);

/* Run inference. request_json is one request object or an array of them:
 *   {"state": "...", "questions": {"id": {"type": "noul|choice|score", "instructions": "...",
 *                                         "criteria": ...}}}
 * Returns {"results":[...], "elapsed_ms":n, "backend":"...", "device":"..."} on success or
 * {"error":"..."} on failure (the same shapes laya-cli prints). Free with laya_free_string.
 * Returns NULL only if memory allocation fails. */
LAYA_API char* LAYA_CALL laya_predict(laya_agent* agent, const char* request_json);

/* Tokenize/prepare only (no inference): the model input tensors as JSON. For debugging. */
LAYA_API char* LAYA_CALL laya_prepare(laya_agent* agent, const char* request_json);

/* {"backend":"...","device":"...","model_dir":"...","model_name":"...","variant":"...",
 *  "max_len":n,"head_max_len":n}. Free with laya_free_string. */
LAYA_API char* LAYA_CALL laya_info(laya_agent* agent);

LAYA_API void LAYA_CALL laya_free_string(char* s);

/* Release the model. Safe with NULL. Do not call while another thread is using the agent. */
LAYA_API void LAYA_CALL laya_destroy(laya_agent* agent);

/* Message for the most recent failure on the calling thread ("" if none). Owned by the library;
 * valid until the next laya_* call on the same thread. */
LAYA_API const char* LAYA_CALL laya_last_error(void);

#ifdef __cplusplus
}
#endif
#endif /* LAYA_C_H */
