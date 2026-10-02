/* bench_capi.c - compare backends/precisions of the laya C API on one machine.
 *
 *   laya-bench MODEL_DIR [OPTIONS_JSON ...]
 *   laya-bench C:\models\laya {"backend":"cpu"} {"backend":"vulkan"} {"backend":"vulkan","precision":"fp16"}
 *
 * Without options it runs cpu, vulkan (fp32), vulkan + tensor_core, vulkan fp16 and vulkan bf16.
 * For each configuration: load time, ms per single question, ms per question in a batch of 8,
 * and whether the answers match the first configuration (to 2 decimals: GPU and CPU round
 * differently in the last digits).
 */
#include "laya_c.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
static double now_ms(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
}
#else
static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
#endif

static const char* SINGLE =
    "{\"state\":\"Please refund the duplicate charge.\",\"questions\":{\"refund\":{\"type\":\"noul\","
    "\"instructions\":\"Does the customer ask for a refund?\"}}}";
static const char* ONE_OF_BATCH =
    "{\"state\":\"I want to cancel my subscription, this is the third time I am writing.\",\"questions\":{"
    "\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\",\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]},"
    "\"anger\":{\"type\":\"score\",\"instructions\":\"How angry is the customer?\",\"criteria\":[\"calm\",\"annoyed\",\"furious\"]}}}";

/* First number after `key` in `json`, or -1. */
static double number_after(const char* json, const char* key) {
    const char* p = json ? strstr(json, key) : NULL;
    return p ? atof(p + strlen(key)) : -1.0;
}

static int run(const char* model, const char* options, double* ref_noul, double* ref_cancel) {
    printf("\n== %s\n", options);
    fflush(stdout);
    double t0 = now_ms();
    laya_agent* agent = laya_create(model, options);
    if (!agent) {
        printf("   load failed: %s\n", laya_last_error());
        return 0;
    }
    char* info = laya_info(agent);
    printf("   device: %s\n", info);
    laya_free_string(info);
    printf("   load: %.0f ms\n", now_ms() - t0);

    /* batch of 8 requests = 16 questions */
    size_t cap = 8 * strlen(ONE_OF_BATCH) + 16;
    char* batch = (char*)malloc(cap);
    strcpy(batch, "[");
    for (int i = 0; i < 8; ++i) { if (i) strcat(batch, ","); strcat(batch, ONE_OF_BATCH); }
    strcat(batch, "]");

    char* r = laya_predict(agent, SINGLE);
    if (!r || strncmp(r, "{\"error\"", 8) == 0) {
        printf("   predict failed: %s\n", r ? r : "(null)");
        laya_free_string(r);
        free(batch);
        laya_destroy(agent);
        return 0;
    }
    double noul = number_after(r, "\"noul\":");
    laya_free_string(r);
    for (int i = 0; i < 2; ++i) laya_free_string(laya_predict(agent, SINGLE));  /* warm-up */
    r = laya_predict(agent, batch);
    double cancel = number_after(r, "\"cancel\":");
    laya_free_string(r);

    const int n1 = 10, nb = 5;
    t0 = now_ms();
    for (int i = 0; i < n1; ++i) laya_free_string(laya_predict(agent, SINGLE));
    double single = (now_ms() - t0) / n1;
    t0 = now_ms();
    for (int i = 0; i < nb; ++i) laya_free_string(laya_predict(agent, batch));
    double per_question = (now_ms() - t0) / nb / 16.0;

    printf("   single question: %7.1f ms\n", single);
    printf("   batch of 16:     %7.1f ms per question  (%.0f questions/s)\n", per_question, 1000.0 / per_question);
    printf("   answers: noul=%.4f cancel=%.4f", noul, cancel);
    int ok = 1;
    if (*ref_noul < 0) {
        *ref_noul = noul;
        *ref_cancel = cancel;
        printf("  (reference)\n");
    } else {
        double d1 = noul - *ref_noul, d2 = cancel - *ref_cancel;
        ok = d1 < 0.01 && d1 > -0.01 && d2 < 0.01 && d2 > -0.01;
        printf("  %s\n", ok ? "(matches reference)" : "(DIFFERS from reference)");
    }
    free(batch);
    laya_destroy(agent);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s MODEL_DIR [OPTIONS_JSON ...]\n", argv[0]);
        return 2;
    }
    static const char* defaults[] = {
        "{\"backend\":\"cpu\"}",
        "{\"backend\":\"vulkan\"}",
        "{\"backend\":\"vulkan\",\"tensor_core\":true}",
        "{\"backend\":\"vulkan\",\"precision\":\"fp16\"}",
        "{\"backend\":\"vulkan\",\"precision\":\"bf16\"}",
    };
    printf("%s\n", laya_version());
    double ref_noul = -1, ref_cancel = -1;
    int differ = 0;
    if (argc > 2)
        for (int i = 2; i < argc; ++i) differ += !run(argv[1], argv[i], &ref_noul, &ref_cancel);
    else
        for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; ++i) differ += !run(argv[1], defaults[i], &ref_noul, &ref_cancel);
    return differ ? 1 : 0;
}
