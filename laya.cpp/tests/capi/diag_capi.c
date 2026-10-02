/* diag_capi.c - find which kind of request a backend gets wrong, compared with the CPU.
 *   laya-diag MODEL_DIR [OPTIONS_JSON]      (default {"backend":"vulkan"})
 * Cases separate batching (several sequences in one forward pass) from padding (sequences of
 * different lengths in one batch) and from question types. Both agents stay loaded at once. */
#include "laya_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define Q_NOUL "{\"refund\":{\"type\":\"noul\",\"instructions\":\"Does the customer ask for a refund?\"}}"
#define Q_CHOICE "{\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\",\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]}}"
#define S1 "\"Please refund the duplicate charge.\""
#define S2 "\"I want to cancel my subscription, this is the third time I am writing and nobody answers.\""

static const char* names[] = {
    "A  1 sequence            (noul)",
    "B  1 sequence            (choice)",
    "C  2 sequences, same length, no padding",
    "D  2 sequences, different lengths (padding)",
    "E  1 request, 2 questions (padding)",
    "F  8 sequences, same length, no padding",
};
static const char* cases[] = {
    "{\"state\":" S1 ",\"questions\":" Q_NOUL "}",
    "{\"state\":" S2 ",\"questions\":" Q_CHOICE "}",
    "[{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "}]",
    "[{\"state\":" S1 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "}]",
    "{\"state\":" S2 ",\"questions\":{\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\",\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]},\"refund\":{\"type\":\"noul\",\"instructions\":\"Does the customer ask for a refund?\"}}}",
    "[{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "},"
     "{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "},{\"state\":" S2 ",\"questions\":" Q_CHOICE "}]",
};

/* largest absolute difference between numbers of two same-shaped JSON texts; -1 if shapes differ */
static double diff(const char* a, const char* b) {
    double worst = 0;
    while (*a && *b) {
        int na = (*a >= '0' && *a <= '9') || (*a == '-' && a[1] >= '0' && a[1] <= '9');
        int nb = (*b >= '0' && *b <= '9') || (*b == '-' && b[1] >= '0' && b[1] <= '9');
        if (na && nb) {
            char *ea, *eb;
            double d = strtod(a, &ea) - strtod(b, &eb);
            if (d < 0) d = -d;
            if (d > worst) worst = d;
            a = ea; b = eb;
        } else if (na != nb || *a != *b) return -1;
        else { ++a; ++b; }
    }
    return *a == *b ? worst : -1;
}
static char* results(laya_agent* a, const char* req) {
    char* r = laya_predict(a, req);
    const char* s = r ? strstr(r, "\"results\":") : NULL;
    const char* e = s ? strstr(s, ",\"elapsed_ms\"") : NULL;
    char* out = NULL;
    if (s && e) { out = (char*)malloc(e - s + 1); memcpy(out, s, e - s); out[e - s] = 0; }
    else { out = strdup(r ? r : "(null)"); }
    laya_free_string(r);
    return out;
}

/* N copies of one request, as a JSON array. Caller frees. */
static char* repeat(const char* one, int n) {
    size_t len = strlen(one);
    char* out = (char*)malloc(n * (len + 1) + 3);
    char* p = out;
    *p++ = '[';
    for (int i = 0; i < n; ++i) { if (i) *p++ = ','; memcpy(p, one, len); p += len; }
    *p++ = ']'; *p = 0;
    return out;
}
#define BENCH_ONE "{\"state\":\"I want to cancel my subscription, this is the third time I am writing.\",\"questions\":{" \
    "\"intent\":{\"type\":\"choice\",\"instructions\":\"What does the customer want?\",\"criteria\":[\"cancel\",\"upgrade\",\"refund\"]}," \
    "\"anger\":{\"type\":\"score\",\"instructions\":\"How angry is the customer?\",\"criteria\":[\"calm\",\"annoyed\",\"furious\"]}}}"

static int compare(laya_agent* cpu, laya_agent* gpu, const char* name, const char* req) {
    char* c = results(cpu, req);
    char* g = results(gpu, req);
    double d = diff(g, c);
    int ok = d >= 0 && d <= 0.02;
    printf("%-45s %s  (largest difference %.4f)\n", name, ok ? "OK  " : "WRONG", d);
    if (!ok && getenv("LAYA_DIAG_VERBOSE")) printf("-> gpu: %.160s\n-> cpu: %.160s\n", g, c);
    fflush(stdout);
    free(c); free(g);
    return ok;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s MODEL_DIR [OPTIONS_JSON]\n", argv[0]); return 2; }
    const char* opts = argc > 2 ? argv[2] : "{\"backend\":\"vulkan\"}";
    printf("%s\ncpu vs %s\n", laya_version(), opts); fflush(stdout);
    laya_agent* cpu = laya_create(argv[1], "{\"backend\":\"cpu\"}");
    if (!cpu) { printf("cpu load failed: %s\n", laya_last_error()); return 1; }
    printf("cpu loaded\n"); fflush(stdout);
    laya_agent* gpu = laya_create(argv[1], opts);
    if (!gpu) { printf("gpu load failed: %s\n", laya_last_error()); return 1; }
    char* info = laya_info(gpu); printf("gpu loaded: %s\n\n", info); laya_free_string(info); fflush(stdout);
    int bad = 0;
    for (int i = 0; i < 6; ++i) {
        char* c = results(cpu, cases[i]);
        char* g = results(gpu, cases[i]);
        double d = diff(g, c);
        int ok = d >= 0 && d <= 0.02;
        bad += !ok;
        printf("%-45s %s  (largest difference %.4f)\n", names[i], ok ? "OK  " : "WRONG", d);
        if (!ok) printf("-> gpu: %.160s\n-> cpu: %.160s\n", g, c);
        fflush(stdout);
        free(c); free(g);
    }
    /* batch size: same request repeated (no padding), then the laya-bench batch (padding) */
    static const int sizes[] = {9, 12, 15, 16, 17, 24, 32};
    const char* one = "{\"state\":" S2 ",\"questions\":" Q_CHOICE "}";
    int total = 6;
    for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) {
        char name[64], *req = repeat(one, sizes[i]);
        snprintf(name, sizeof name, "%c  %d sequences, same length, no padding", 'G' + (int)i, sizes[i]);
        bad += !compare(cpu, gpu, name, req);
        free(req); ++total;
    }
    for (int n = 1; n <= 16; n *= 2) {
        char name[64], *req = repeat(BENCH_ONE, n);
        snprintf(name, sizeof name, "%c  laya-bench batch x%-2d (%d sequences)", 'N' + (n == 1 ? 0 : n == 2 ? 1 : n == 4 ? 2 : n == 8 ? 3 : 4), n, 2 * n);
        bad += !compare(cpu, gpu, name, req);
        free(req); ++total;
    }
    /* one long sequence: tests whether size, not batch count, is the trigger */
    static const int sentences[] = {10, 20, 32};
    const char* sentence = "My order arrived broken and I would like my money back please. ";
    for (size_t i = 0; i < sizeof sentences / sizeof sentences[0]; ++i) {
        size_t sl = strlen(sentence);
        char* state = (char*)malloc(sl * sentences[i] + 1);
        state[0] = 0;
        for (int k = 0; k < sentences[i]; ++k) strcat(state, sentence);
        char* req = (char*)malloc(strlen(state) + 256);
        sprintf(req, "{\"state\":\"%s\",\"questions\":" Q_CHOICE "}", state);
        char* prep = laya_prepare(cpu, req);
        const char* lp = prep ? strstr(prep, "\"length\":") : NULL;
        int tokens = lp ? atoi(lp + 9) : -1;
        laya_free_string(prep);
        char name[64];
        snprintf(name, sizeof name, "%c  1 long sequence (%d tokens)", 'S' + (int)i, tokens);
        if (tokens < 0) printf("%-45s skipped (longer than the model accepts)\n", name);
        else bad += !compare(cpu, gpu, name, req);
        free(state); free(req); ++total;
    }
    laya_destroy(gpu);
    laya_destroy(cpu);
    printf("\n%d of %d cases wrong\n", bad, total);
    return bad ? 1 : 0;
}
