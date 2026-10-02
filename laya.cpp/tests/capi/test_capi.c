/* test_capi.c - tests for the laya C ABI (laya.dll / liblaya.so).
 *
 * Plain C11 on purpose: it proves the header is consumable from C and exercises the library the
 * way an FFI host (Delphi, C#, ...) does. Runs on Linux and Windows (also under Wine).
 *
 *   test-capi                      API-contract tests that need no model
 *   LAYA_TEST_MODEL=dir test-capi  + end-to-end inference tests on a checkpoint
 *   LAYA_TEST_DUMP=file            also write the reference predictions there (for cross-platform
 *                                  and laya-cli parity checks, see test_parity.py)
 *   LAYA_TEST_BACKEND=cpu|cuda|vulkan (default cpu)
 *   LAYA_TEST_QUICK=1              fewer repetitions in the thread test (slow emulators)
 * Exit code: 0 pass, 1 failure, 77 model tests skipped (contract tests still passed).
 */
#ifndef _WIN32
#define _GNU_SOURCE /* feenableexcept */
#endif
#include "laya_c.h"

#include <fenv.h>
#include <float.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
typedef DWORD(WINAPI* thread_fn)(void*);
#define THREAD_RET DWORD WINAPI
static int thread_start(thread_t* t, thread_fn fn, void* arg) { *t = CreateThread(NULL, 0, fn, arg, 0, NULL); return *t ? 0 : -1; }
static void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
typedef pthread_t thread_t;
typedef void* (*thread_fn)(void*);
#define THREAD_RET void*
static int thread_start(thread_t* t, thread_fn fn, void* arg) { return pthread_create(t, NULL, fn, arg); }
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#endif

/* Emulate Delphi/Free Pascal hosts, which unmask invalid/zero-divide/overflow FP exceptions. */
static void unmask_fp_exceptions(void) {
#ifdef _WIN32
    unsigned int cw;
    _controlfp_s(&cw, 0, 0);
    _controlfp_s(&cw, cw & ~(unsigned)(_EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW), _MCW_EM);
#elif defined(__APPLE__) && defined(__x86_64__)
    /* macOS has no feenableexcept: clear the masks in both control registers directly. */
    unsigned short cw;
    unsigned csr;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw &= (unsigned short)~(0x01 | 0x04 | 0x08); /* invalid, divide by zero, overflow */
    __asm__ volatile("fnclex; fldcw %0" : : "m"(cw));
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr &= ~(0x0080u | 0x0200u | 0x0400u);        /* IM, ZM, OM */
    csr &= ~0x3Fu;
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
#elif defined(__APPLE__)
    /* Apple Silicon does not implement floating-point exception traps; nothing to unmask. */
#else
    feenableexcept(FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW);
#endif
}
/* Raw control registers, to tell an x87 from an SSE problem. */
static unsigned x87_control(void) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    unsigned short cw; __asm__ volatile("fnstcw %0" : "=m"(cw)); return cw;
#else
    return 0;
#endif
}
static unsigned sse_control(void) {
#if defined(__GNUC__) && defined(__x86_64__)
    unsigned csr; __asm__ volatile("stmxcsr %0" : "=m"(csr)); return csr & ~0x3Fu; /* ignore sticky flags */
#else
    return 0;
#endif
}
static unsigned fp_exception_state(void) {
#ifdef _WIN32
    unsigned int cw;
    _controlfp_s(&cw, 0, 0);
    return cw & _MCW_EM;
#elif defined(__APPLE__) && defined(__aarch64__)
    unsigned long long fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    return (unsigned)fpcr;
#elif defined(__APPLE__)
    return sse_control();
#else
    return (unsigned)fegetexcept();
#endif
}

static int failures = 0, checks = 0;
#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++checks;                                                              \
        if (!(cond)) {                                                         \
            ++failures;                                                        \
            fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                      \
            fputc('\n', stderr);                                               \
        }                                                                      \
    } while (0)

#define STEP(name) do { printf("[step] %s\n", name); fflush(stdout); } while (0)

static int contains(const char* text, const char* needle) { return text && strstr(text, needle) != NULL; }
static int is_error(const char* json) { return json && strncmp(json, "{\"error\":", 9) == 0; }

/* The "results" member only (drops elapsed_ms, which varies run to run). Caller frees. */
static char* results_of(const char* json) {
    const char* start = json ? strstr(json, "\"results\":") : NULL;
    const char* end = start ? strstr(start, ",\"elapsed_ms\":") : NULL;
    if (!start || !end) return NULL;
    size_t n = (size_t)(end - start);
    char* out = (char*)malloc(n + 1);
    memcpy(out, start, n);
    out[n] = 0;
    return out;
}

static char* predict_results(laya_agent* agent, const char* request) {
    char* json = laya_predict(agent, request);
    char* results = results_of(json);
    if (!results) fprintf(stderr, "  predict returned: %s\n", json ? json : "(null)");
    laya_free_string(json);
    return results;
}


/* Compare two JSON texts that have the same structure: every number must agree within tol.
 * Used to check a GPU backend against the CPU (GPUs round differently in the last digits). */
static int same_numbers(const char* a, const char* b, double tol, double* worst) {
    *worst = 0;
    while (*a && *b) {
        /* a number starts with a digit, or '-' followed by a digit ("laya-rl-agent" is text) */
        int na = (*a >= '0' && *a <= '9') || (*a == '-' && a[1] >= '0' && a[1] <= '9');
        int nb = (*b >= '0' && *b <= '9') || (*b == '-' && b[1] >= '0' && b[1] <= '9');
        if (na && nb) {
            char *ea, *eb;
            double x = strtod(a, &ea), y = strtod(b, &eb);
            double d = x > y ? x - y : y - x;
            if (d > *worst) *worst = d;
            if (d > tol) return 0;
            a = ea; b = eb;
        } else if (na != nb || *a != *b) {
            return 0;
        } else {
            ++a; ++b;
        }
    }
    return *a == *b;
}

/* ---------------------------------------------------------------- contract tests (no model) */

static int log_calls = 0;
static void LAYA_CALL count_log(int level, const char* text, void* user) {
    (void)level; (void)text;
    if (user == &log_calls) ++log_calls;
}

static void test_contract(void) {
    const char* v = laya_version();
    CHECK(v && strncmp(v, "laya_c ", 7) == 0, "version=%s", v ? v : "(null)");
    CHECK(laya_api_version() == LAYA_C_API_VERSION, "api=%d", laya_api_version());
    printf("library: %s\n", v);

    /* The first call that fails inside the library: it throws and catches a C++ exception. */
    STEP("contract: errors are reported, not thrown");
    CHECK(laya_create(NULL, NULL) == NULL, "NULL dir must fail");
    CHECK(contains(laya_last_error(), "model_dir"), "err=%s", laya_last_error());
    CHECK(laya_create("", NULL) == NULL, "empty dir must fail");

    CHECK(laya_create("/definitely/not/here", NULL) == NULL, "missing dir must fail");
    CHECK(contains(laya_last_error(), "rl_agent_config.json"), "err=%s", laya_last_error());

    const char* bad_options[][2] = {
        {"{not json", "parse"},
        {"[1,2]", "JSON object"},
        {"{\"bogus\":1}", "Unknown option: bogus"},
        {"{\"backend\":\"tpu\"}", "Unknown backend"},
        {"{\"precision\":\"int4\"}", "Unknown precision"},
        {"{\"variant\":\"klingon\"}", "Unknown model variant"},
        {"{\"threads\":-2}", "threads"},
    };
    for (size_t i = 0; i < sizeof bad_options / sizeof bad_options[0]; ++i) {
        CHECK(laya_create("/definitely/not/here", bad_options[i][0]) == NULL, "options %s", bad_options[i][0]);
        CHECK(contains(laya_last_error(), bad_options[i][1]), "options %s -> err=%s", bad_options[i][0], laya_last_error());
    }

    STEP("contract: NULL agent");
    char* r = laya_predict(NULL, "{}");
    CHECK(is_error(r) && contains(r, "agent is NULL"), "predict(NULL)=%s", r ? r : "(null)");
    CHECK(contains(laya_last_error(), "agent is NULL"), "err=%s", laya_last_error());
    laya_free_string(r);
    r = laya_prepare(NULL, "{}");
    CHECK(is_error(r), "prepare(NULL)=%s", r ? r : "(null)");
    laya_free_string(r);
    r = laya_info(NULL);
    CHECK(is_error(r), "info(NULL)=%s", r ? r : "(null)");
    laya_free_string(r);
    laya_free_string(NULL);
    laya_destroy(NULL);

    /* A successful call clears the thread's last error. */
    (void)laya_create(NULL, NULL);
    r = laya_predict(NULL, "{}");
    laya_free_string(r);
    CHECK(strlen(laya_last_error()) > 0, "error expected");

    laya_set_log_callback(count_log, &log_calls, 1);
    laya_set_log_callback(NULL, NULL, 0);
    STEP("contract: done");
}

/* ---------------------------------------------------------------- model tests */

static const char* REQ_NOUL =
    "{\"state\":\"Please refund the duplicate charge.\",\"questions\":{\"refund\":{\"type\":\"noul\","
    "\"instructions\":\"Does the customer ask for a refund?\"}}}";
static const char* REQ_BATCH =
    "[{\"state\":\"I want to cancel my subscription.\",\"questions\":{"
    "\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\",\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]},"
    "\"anger\":{\"type\":\"score\",\"instructions\":\"How angry is the customer?\",\"criteria\":[\"calm\",\"annoyed\",\"furious\"]}}},"
    "{\"state\":{\"ticket\":42,\"text\":\"Caf\xC3\xA9 na\xC3\xAFve \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xE2\x80\x94 money back now!\"},"
    "\"questions\":{\"refund\":{\"type\":\"noul\",\"instructions\":\"Does the customer ask for a refund?\","
    "\"criteria\":{\"true\":\"asks for money back\",\"false\":\"does not\"}}}}]";

typedef struct { laya_agent* agent; const char* reference; int runs, mismatches; } worker_arg;

static THREAD_RET worker(void* p) {
    worker_arg* w = (worker_arg*)p;
    for (int i = 0; i < w->runs; ++i) {
        char* got = predict_results(w->agent, REQ_BATCH);
        if (!got || strcmp(got, w->reference) != 0) ++w->mismatches;
        free(got);
    }
    return 0;
}

static void test_model(const char* model, const char* backend, const char* dump_path) {
    char options[128];
    snprintf(options, sizeof options, "{\"backend\":\"%s\"}", backend);
    STEP("loading the model");
    laya_agent* agent = laya_create(model, options);
    CHECK(agent != NULL, "create(%s): %s", model, laya_last_error());
    if (!agent) return;

    char* info = laya_info(agent);
    CHECK(info && contains(info, "\"backend\":") && contains(info, "\"max_len\":"), "info=%s", info ? info : "(null)");
    printf("info: %s\n", info ? info : "(null)");
    laya_free_string(info);

    STEP("single yes/no question");
    /* single boolean question */
    char* full = laya_predict(agent, REQ_NOUL);
    CHECK(full && !is_error(full), "noul: %s", full ? full : "(null)");
    CHECK(contains(full, "\"refund\":{\"type\":\"noul\"") && contains(full, "\"noul\":") &&
          contains(full, "\"act_probability\":") && contains(full, "\"elapsed_ms\":"), "noul shape: %s", full ? full : "(null)");
    printf("noul: %s\n", full ? full : "(null)");
    laya_free_string(full);

    STEP("batch: choice + score + noul, padded");
    /* batch of two requests, choice + score + noul with criteria, nested JSON state, UTF-8 */
    char* reference = predict_results(agent, REQ_BATCH);
    CHECK(reference != NULL, "batch failed");
    if (reference) {
        CHECK(contains(reference, "\"choice\":") && contains(reference, "\"score\":") && contains(reference, "\"legend\":"),
              "batch shape: %s", reference);
        CHECK(contains(reference, "\"probabilities\":{\"cancel\":"), "choice keys: %s", reference);
        const char* second = strstr(reference, "},{\"model\"");
        CHECK(second && contains(second, "\"refund\""), "second request answered: %s", reference);
    }

    /* a GPU backend must give the CPU's answers (catches miscompiled shaders, e.g. fast math) */
    if (reference && strcmp(backend, "cpu") != 0) {
        STEP("answers match the cpu backend");
        laya_agent* cpu = laya_create(model, "{\"backend\":\"cpu\"}");
        CHECK(cpu != NULL, "cpu create: %s", laya_last_error());
        char* expected = cpu ? predict_results(cpu, REQ_BATCH) : NULL;
        double worst = 0;
        int same = expected && same_numbers(reference, expected, 0.02, &worst);
        CHECK(same, "%s answers differ from cpu (largest difference %.4f):\n  %s\n  cpu: %s", backend, worst,
              reference, expected ? expected : "(null)");
        if (same) printf("largest difference from cpu: %.4f\n", worst);
        free(expected);
        laya_destroy(cpu);
    }
    STEP("determinism");
    /* determinism: same input, same output */
    char* again = predict_results(agent, REQ_BATCH);
    CHECK(reference && again && strcmp(reference, again) == 0, "nondeterministic:\n%s\n%s", reference ? reference : "(null)", again ? again : "(null)");
    free(again);

    STEP("prepare");
    /* prepare */
    char* prep = laya_prepare(agent, REQ_NOUL);
    CHECK(prep && contains(prep, "\"ids\":[") && contains(prep, "\"batch\":1"), "prepare: %.200s", prep ? prep : "(null)");
    laya_free_string(prep);

    STEP("malformed requests");
    /* bad requests produce {"error":...} and leave the agent usable */
    const char* bad_requests[][2] = {
        {"{oops", "parse"},
        {"[]", "nonempty array"},
        {"{\"questions\":{\"a\":{\"type\":\"noul\",\"instructions\":\"x\"}}}", "state"},
        {"{\"state\":\"x\",\"questions\":{}}", "nonempty object"},
        {"{\"state\":\"x\",\"questions\":{\"a\":{\"type\":\"essay\",\"instructions\":\"x\"}}}", "Unsupported question type"},
        {"{\"state\":\"x\",\"questions\":{\"a\":{\"type\":\"choice\",\"instructions\":\"x\",\"criteria\":[\"only\"]}}}", "2 through 255"},
    };
    for (size_t i = 0; i < sizeof bad_requests / sizeof bad_requests[0]; ++i) {
        char* r = laya_predict(agent, bad_requests[i][0]);
        CHECK(is_error(r) && contains(r, bad_requests[i][1]), "request %s -> %s", bad_requests[i][0], r ? r : "(null)");
        laya_free_string(r);
    }
    {
        char* r = laya_predict(agent, NULL);
        CHECK(is_error(r) && contains(laya_last_error(), "NULL"), "NULL request -> %s", r ? r : "(null)");
        laya_free_string(r);
    }

    STEP("over-long state");
    /* over-long state is rejected (no silent truncation) */
    {
        size_t n = 20000;
        char* request = (char*)malloc(n + 200);
        strcpy(request, "{\"state\":\"");
        size_t len = strlen(request);
        for (size_t i = 0; i < n; i += 5) memcpy(request + len + i, "word ", 5);
        strcpy(request + len + n, "\",\"questions\":{\"q\":{\"type\":\"noul\",\"instructions\":\"long?\"}}}");
        char* r = laya_predict(agent, request);
        CHECK(is_error(r) && contains(r, "exceeds state context limit"), "long state -> %.200s", r ? r : "(null)");
        laya_free_string(r);
        free(request);
    }
    char* after = predict_results(agent, REQ_BATCH);
    CHECK(reference && after && strcmp(reference, after) == 0, "agent changed after errors");
    free(after);

    STEP("4 threads share one agent");
    /* concurrency: four threads share one agent; every answer must equal the reference */
    if (reference) {
        enum { N = 4 };
        worker_arg args[N];
        thread_t threads[N];
        for (int i = 0; i < N; ++i) {
            args[i] = (worker_arg){agent, reference, getenv("LAYA_TEST_QUICK") ? 1 : 3, 0};  /* quick: for emulators */
            CHECK(thread_start(&threads[i], worker, &args[i]) == 0, "thread start");
        }
        int mismatches = 0;
        for (int i = 0; i < N; ++i) { thread_join(threads[i]); mismatches += args[i].mismatches; }
        CHECK(mismatches == 0, "%d concurrent results differed", mismatches);
    }

    if (dump_path && reference) {
        FILE* f = fopen(dump_path, "wb");
        CHECK(f != NULL, "cannot write %s", dump_path);
        if (f) { fputs(reference, f); fclose(f); }
    }
    free(reference);
    laya_destroy(agent);

    STEP("destroy + reload");
    /* reload after destroy, with explicit thread count and the variant option */
    snprintf(options, sizeof options, "{\"backend\":\"%s\",\"threads\":1,\"variant\":\"english\"}", backend);
    agent = laya_create(model, options);
    CHECK(agent != NULL, "re-create: %s", laya_last_error());
    char* single = agent ? predict_results(agent, REQ_NOUL) : NULL;
    CHECK(single && contains(single, "\"noul\":"), "re-created agent predict");
    free(single);
    laya_destroy(agent);
}

int main(void) {
    test_contract();
    const char* model = getenv("LAYA_TEST_MODEL");
    const char* backend = getenv("LAYA_TEST_BACKEND");
    int skipped = !model || !*model;
    if (!skipped) {
        /* Run like a Delphi host would; the library must neither trap nor change our FP state. */
        unmask_fp_exceptions();
        unsigned before = fp_exception_state(), x87_before = x87_control(), sse_before = sse_control();
        test_model(model, backend && *backend ? backend : "cpu", getenv("LAYA_TEST_DUMP"));
        unsigned x87_after = x87_control(), sse_after = sse_control();
        printf("fp state: crt %#x -> %#x, x87 cw %#x -> %#x, mxcsr %#x -> %#x\n", before, fp_exception_state(),
               x87_before, x87_after, sse_before, sse_after);
        /* SSE (MXCSR) is what 64-bit compilers, Delphi Win64 included, use for floating point. */
        CHECK(sse_after == sse_before, "caller MXCSR changed: %#x -> %#x", sse_before, sse_after);
        CHECK(x87_after == x87_before, "caller x87 control word changed: %#x -> %#x", x87_before, x87_after);
    }
    printf("%d checks, %d failures%s\n", checks, failures, skipped ? " (model tests skipped: set LAYA_TEST_MODEL)" : "");
    return failures ? 1 : skipped ? 77 : 0;
}
