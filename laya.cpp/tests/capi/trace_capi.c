/* trace_capi.c - locate the first operation where a backend diverges from the CPU.
 *
 *   laya-trace MODEL_DIR [SEQUENCES=15] [OPTIONS_JSON={"backend":"vulkan"}]
 *
 * Runs one batch of N identical choice questions on the CPU and on the other backend with
 * laya's LAYA_TRACE_DIR tensor dumps enabled (./laya-trace-cpu, ./laya-trace-gpu; a few hundred
 * MB, deleted on the next run), then compares every dumped tensor in execution order and prints
 * the first ones that differ, with the position of the first wrong value.
 */
#include "laya_c.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ONE "{\"state\":\"I want to cancel my subscription, this is the third time I am writing and nobody answers.\"," \
            "\"questions\":{\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\"," \
            "\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]}}}"

static const char* order[] = {".qkv-input", ".attn.Wqkv", ".q", ".k", ".attn.Wo.input", ".attn.Wo", ".attn.residual",
                              ".mlp.Wi.input", ".mlp.Wi", ".mlp.Wo.input", ".mlp.Wo", ".mlp.residual",
                              ".linear1", ".linear2", ".linear2-residual"};

typedef struct { char name[160]; long key; } entry;

static long sort_key(const char* name) {
    /* execution order: embedding, encoder layer 0 tensors, encoder-0 (its output), ..., final-norm, head */
    if (strncmp(name, "embedding", 9) == 0) return -1;
    if (strncmp(name, "final-norm", 10) == 0) return 900000;
    if (strncmp(name, "encoder-", 8) == 0) return atol(name + 8) * 1000 + 500;
    if (strncmp(name, "head-", 5) == 0) return 1000000 + atol(name + 5) * 1000 + 500;
    long stage = strncmp(name, "head", 4) == 0 ? 1000000 : 0, layer = 0, suffix = 99;
    const char* p = strstr(name, "layers.");
    if (p) layer = atol(p + 7);
    for (size_t i = 0; i < sizeof order / sizeof order[0]; ++i) {
        size_t n = strlen(name), m = strlen(order[i]);
        if (n > m + 4 && strncmp(name + n - m - 4, order[i], m) == 0) { suffix = (long)i; break; }
    }
    return stage + layer * 1000 + suffix;
}
static int by_key(const void* a, const void* b) {
    long x = ((const entry*)a)->key, y = ((const entry*)b)->key;
    return x < y ? -1 : x > y ? 1 : strcmp(((const entry*)a)->name, ((const entry*)b)->name);
}
static float* load(const char* dir, const char* name, size_t* count) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if (!f) { *count = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    float* data = (float*)malloc(bytes > 0 ? bytes : 4);
    *count = fread(data, 4, bytes / 4, f);
    fclose(f);
    return data;
}
static void clear_dir(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return;
    struct dirent* e;
    char path[512];
    while ((e = readdir(d))) {
        size_t n = strlen(e->d_name);
        if (n > 4 && strcmp(e->d_name + n - 4, ".f32") == 0) { snprintf(path, sizeof path, "%s/%s", dir, e->d_name); remove(path); }
    }
    closedir(d);
}
static char* run(const char* model, const char* options, const char* dir, const char* request, int* length) {
    clear_dir(dir);
    setenv("LAYA_TRACE_DIR", dir, 1);
    laya_agent* agent = laya_create(model, options);
    if (!agent) { printf("load failed (%s): %s\n", options, laya_last_error()); exit(1); }
    char* prep = laya_prepare(agent, request);
    const char* lp = prep ? strstr(prep, "\"length\":") : NULL;
    *length = lp ? atoi(lp + 9) : 0;
    laya_free_string(prep);
    char* out = laya_predict(agent, request);
    laya_destroy(agent);
    unsetenv("LAYA_TRACE_DIR");
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s MODEL_DIR [SEQUENCES] [OPTIONS_JSON]\n", argv[0]); return 2; }
    int n = argc > 2 ? atoi(argv[2]) : 15;
    const char* options = argc > 3 ? argv[3] : "{\"backend\":\"vulkan\"}";
    if (n < 1 || n > 64) n = 15;
    size_t one = strlen(ONE);
    char* request = (char*)malloc(n * (one + 1) + 3), *p = request;
    *p++ = '[';
    for (int i = 0; i < n; ++i) { if (i) *p++ = ','; memcpy(p, ONE, one); p += one; }
    *p++ = ']'; *p = 0;

    int length = 0, gl = 0;
    printf("%s\n%d sequences: cpu vs %s\n", laya_version(), n, options); fflush(stdout);
    if (argc > 4) {  /* compare existing dumps only: laya-trace MODEL N OPTIONS LENGTH */
        length = atoi(argv[4]);
    } else {
        char* cpu = run(argv[1], "{\"backend\":\"cpu\"}", "laya-trace-cpu", request, &length);
        char* gpu = run(argv[1], options, "laya-trace-gpu", request, &gl);
        const char* a = strstr(cpu, "\"probabilities\""), *b = strstr(gpu, "\"probabilities\"");
        printf("cpu first answer: %.70s\ngpu first answer: %.70s\n", a ? a : cpu, b ? b : gpu);
    }
    printf("sequence length %d tokens, %d tokens in the batch\n\n", length, length * n);

    entry* list = NULL; int count = 0;
    DIR* d = opendir("laya-trace-cpu");
    if (!d) { printf("no trace files written\n"); return 1; }
    struct dirent* e;
    while ((e = readdir(d))) {
        size_t len = strlen(e->d_name);
        if (len < 5 || strcmp(e->d_name + len - 4, ".f32") != 0) continue;
        list = (entry*)realloc(list, (count + 1) * sizeof(entry));
        snprintf(list[count].name, sizeof list[count].name, "%s", e->d_name);
        list[count].key = sort_key(e->d_name);
        ++count;
    }
    closedir(d);
    qsort(list, count, sizeof(entry), by_key);
    printf("%d traced tensors. First divergences (tolerance 0.05 x typical magnitude):\n", count);
    printf("%-44s %10s %10s %9s %9s  %s\n", "tensor", "max diff", "cpu scale", "wrong", "nan/inf", "first wrong value at");
    int shown = 0, first = -1;
    for (int i = 0; i < count; ++i) {
        size_t ca = 0, cb = 0;
        float* x = load("laya-trace-cpu", list[i].name, &ca);
        float* y = load("laya-trace-gpu", list[i].name, &cb);
        double scale = 0, worst = 0; size_t wrong = 0, nonfinite = 0, where = 0;
        size_t m = ca < cb ? ca : cb;
        for (size_t k = 0; k < m; ++k) scale += fabs(x[k]);
        scale = m ? scale / m : 0;
        double tol = 0.05 * (scale > 1e-6 ? scale : 1e-6);
        for (size_t k = 0; k < m; ++k) {
            if (!isfinite(y[k])) { if (!nonfinite && !wrong) where = k; ++nonfinite; continue; }
            double diff = fabs((double)x[k] - y[k]);
            if (diff > worst) worst = diff;
            if (diff > tol) { if (!wrong && !nonfinite) where = k; ++wrong; }
        }
        int bad = wrong || nonfinite || ca != cb;
        if (bad && first < 0) first = i;
        if ((bad && shown < 14) || (first < 0 && i >= count - 1)) {
            char at[96] = "";
            if (ca != cb) snprintf(at, sizeof at, "sizes differ: %zu vs %zu", ca, cb);
            else if (length && m % (size_t)(length * n) == 0) {
                size_t per_token = m / (size_t)(length * n);
                size_t token = where / per_token;
                snprintf(at, sizeof at, "element %zu = sequence %zu, token %zu (of %d)", where, token / length, token % length, length);
            } else snprintf(at, sizeof at, "element %zu of %zu", where, m);
            printf("%-44.44s %10.4g %10.4g %9zu %9zu  %s\n", list[i].name, worst, scale, wrong, nonfinite, bad ? at : "-");
            if (bad) ++shown;
        }
        free(x); free(y);
    }
    if (first < 0) printf("\nno tensor differs: the trace run is correct on this backend\n");
    else printf("\nfirst divergent tensor: %s  (tensor %d of %d in execution order; the one before it was still correct)\n",
                list[first].name, first + 1, count);
    printf("(dumps kept in ./laya-trace-cpu and ./laya-trace-gpu; delete them when done)\n");
    return first < 0 ? 0 : 1;
}
