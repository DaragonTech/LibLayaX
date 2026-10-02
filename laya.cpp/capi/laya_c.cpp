// laya_c.cpp - implementation of the flat C ABI declared in laya_c.h.
#define LAYA_C_BUILD
#include "laya_c.h"
#include "laya/runtime.hpp"
#include "ggml.h"

#include <algorithm>
#include <cfenv>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#ifndef LAYA_C_VERSION
#define LAYA_C_VERSION "1.0.0"
#endif

#if defined(__linux__) && defined(LAYA_VULKAN_LAZY)
extern "C" int laya_vulkan_loader_available(void);
#endif

struct laya_agent {
    laya::agent impl;
    std::mutex lock;
    std::string directory, variant, model_name;
    int max_len = 0, head_max_len = 0;
};

namespace {
// Per-thread error message. Deliberately a plain char array: a thread_local with a destructor
// (std::string) is freed in the wrong order by MinGW's thread-exit cleanup on threads the host
// created with CreateThread/TThread, which corrupts the heap (double free at thread exit).
constexpr size_t error_capacity = 2048;
thread_local char last_error_text[error_capacity];
void set_last_error(const std::string& message) {
    const size_t n = std::min(message.size(), error_capacity - 1);
    std::memcpy(last_error_text, message.data(), n);
    last_error_text[n] = '\0';
}
void clear_last_error() { last_error_text[0] = '\0'; }

// Hosts such as Delphi and Free Pascal unmask floating-point exceptions (invalid, divide by zero,
// overflow). ggml relies on IEEE non-stop arithmetic (e.g. -inf attention masks), so every entry
// point masks all FP exceptions and restores the caller's exact environment, flags included, on
// exit. Threads ggml starts during a call inherit the masked state (POSIX) or start with the OS
// default, which is masked (Windows).
// feholdexcept covers x87 everywhere, but MinGW's version leaves the SSE control register (MXCSR)
// unmasked, and SSE is what ggml uses on x86-64, so MXCSR is handled explicitly.
// Number of laya calls currently in progress (any thread). Threads that start while it is
// non-zero are ggml workers spawned for that call; they get FP exceptions masked too (DllMain).
std::atomic<int> calls_in_progress{0};

#ifdef _WIN32  // used by DllMain only
inline void mask_fp_exceptions_on_this_thread() {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    unsigned short cw;
    unsigned int csr;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    cw |= 0x3F;
    csr = (csr | 0x1F80u) & ~0x3Fu;
    __asm__ volatile("fnclex");
    __asm__ volatile("fldcw %0" : : "m"(cw));
    __asm__ volatile("ldmxcsr %0" : : "m"(csr));
#elif defined(_M_X64)
    _mm_setcsr((_mm_getcsr() | 0x1F80u) & ~0x3Fu);
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    unsigned long long fpcr;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr &= ~0x9F00ull;  // IOE, DZE, OFE, UFE, IXE, IDE: trap enables off
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}
#endif

struct fp_guard {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    // Save and restore the control registers directly rather than through <cfenv> (MinGW's fenv
    // goes through the C runtime) and with the simplest instructions (fnstcw/fldcw rather than
    // fnstenv/fldenv), which emulators such as Windows-on-ARM's Prism also handle faithfully.
    unsigned short cw;
    unsigned int csr;
    fp_guard() {
        __asm__ volatile("fnstcw %0" : "=m"(cw));
        __asm__ volatile("stmxcsr %0" : "=m"(csr));
        unsigned short masked_cw = cw | 0x3F;               // mask all six x87 exceptions
        unsigned int masked_csr = (csr | 0x1F80u) & ~0x3Fu;  // mask all six SSE exceptions, clear flags
        __asm__ volatile("fnclex");
        __asm__ volatile("fldcw %0" : : "m"(masked_cw));
        __asm__ volatile("ldmxcsr %0" : : "m"(masked_csr));
    }
    ~fp_guard() {
        __asm__ volatile("fnclex");  // drop flags raised while masked so restoring cannot trap
        __asm__ volatile("fldcw %0" : : "m"(cw));
        __asm__ volatile("ldmxcsr %0" : : "m"(csr));
    }
#elif defined(_M_X64)
    // MSVC x64 code uses SSE only; MXCSR is the whole floating-point state.
    unsigned int csr;
    fp_guard() : csr(_mm_getcsr()) { _mm_setcsr((csr | 0x1F80u) & ~0x3Fu); }
    ~fp_guard() { _mm_setcsr(csr); }
#elif defined(_WIN32) && defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    // Windows on ARM64: MinGW's feholdexcept saves the environment and clears the flags but
    // leaves the trap-enable bits of FPCR as the host set them. On CPUs that implement trapping
    // (Apple Silicon under Parallels does) the first overflow inside the engine then stalls the
    // thread forever instead of raising anything. Clear the enables directly for the call.
    unsigned long long fpcr, fpsr;
    fp_guard() {
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
        const unsigned long long masked_fpcr = fpcr & ~0x9F00ull;  // IOE DZE OFE UFE IXE IDE off
        const unsigned long long clear_fpsr = fpsr & ~0x9Full;     // cumulative flags cleared
        __asm__ volatile("msr fpcr, %0" : : "r"(masked_fpcr));
        __asm__ volatile("msr fpsr, %0" : : "r"(clear_fpsr));
    }
    ~fp_guard() {
        // Flags first (dropping those raised while masked), then the caller's trap enables.
        __asm__ volatile("msr fpsr, %0" : : "r"(fpsr));
        __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
    }
#else
    std::fenv_t saved;
    fp_guard() { std::feholdexcept(&saved); }
    ~fp_guard() { std::fesetenv(&saved); }
#endif
    fp_guard(const fp_guard&) = delete;
    fp_guard& operator=(const fp_guard&) = delete;
};
// Entry-point guard: masks FP exceptions for the call (fp_guard) and marks the call as in
// progress so worker threads created meanwhile are masked as well.
struct call_guard {
    fp_guard fp;
    call_guard() { ++calls_in_progress; }
    ~call_guard() { --calls_in_progress; }
};

// Diagnostics: with LAYA_DEBUG=1 in the environment every entry point reports its progress on
// stderr ("[laya] ..."), flushed line by line, so a hang or a crash can be located from outside.
bool debug_enabled() {
    static const bool enabled = [] { const char* flag = std::getenv("LAYA_DEBUG"); return flag && *flag && *flag != '0'; }();
    return enabled;
}
// The floating-point control and status registers, for the trace ("" where not implemented).
std::string fp_registers() {
#if defined(__aarch64__) && !defined(_MSC_VER)
    unsigned long long fpcr = 0, fpsr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    char text[64];
    std::snprintf(text, sizeof text, "fpcr=%08llx fpsr=%08llx", fpcr, fpsr);
    return text;
#else
    return "";
#endif
}
void debug(const char* text, const char* detail = nullptr) {
    if (!debug_enabled()) return;
    std::fprintf(stderr, detail ? "[laya] %s: %s\n" : "[laya] %s\n", text, detail);
    std::fflush(stderr);
}

// Logging: one process-wide sink shared by the library and ggml.
std::mutex log_lock;
laya_log_fn log_fn = nullptr;
void* log_user = nullptr;
std::atomic<int> log_min{GGML_LOG_LEVEL_WARN};

void forward_log(ggml_log_level level, const char* text, void*) {
    if (int(level) != GGML_LOG_LEVEL_CONT && int(level) < log_min.load()) return;
    std::lock_guard<std::mutex> guard(log_lock);
    if (log_fn) log_fn(int(level), text, log_user);
    else if (int(level) >= GGML_LOG_LEVEL_WARN) std::fputs(text, stderr);
}
void forward_abort(const char* message) {  // ggml aborts the process afterwards; at least say why
    forward_log(GGML_LOG_LEVEL_ERROR, message, nullptr);
    forward_log(GGML_LOG_LEVEL_CONT, "\n", nullptr);
}
// The engine is compiled for a fixed instruction-set baseline (see LAYA_REQUIRE_* from CMake).
// Running it on an older CPU would die on an illegal instruction, which hosts such as Delphi
// report as a bare "Access violation". Check up front and fail with a clear message instead.
std::string missing_cpu_features() {
    std::string missing;
#if defined(__x86_64__) || defined(_M_X64)
    unsigned r1[4] = {}, r7[4] = {};
#if defined(_MSC_VER)
    int a[4];
    __cpuid(a, 0); const unsigned max_leaf = unsigned(a[0]);
    __cpuidex(a, 1, 0); for (int i = 0; i < 4; ++i) r1[i] = unsigned(a[i]);
    if (max_leaf >= 7) { __cpuidex(a, 7, 0); for (int i = 0; i < 4; ++i) r7[i] = unsigned(a[i]); }
#else
    const unsigned max_leaf = __get_cpuid_max(0, nullptr);
    __cpuid_count(1, 0, r1[0], r1[1], r1[2], r1[3]);
    if (max_leaf >= 7) __cpuid_count(7, 0, r7[0], r7[1], r7[2], r7[3]);
#endif
    const unsigned ecx1 = r1[2], ebx7 = r7[1];
    bool ymm_os = false, zmm_os = false;  // the OS must save the wider registers too
    if (ecx1 & (1u << 27)) {              // OSXSAVE
#if defined(_MSC_VER)
        const unsigned long long xcr0 = _xgetbv(0);
#else
        unsigned lo, hi;
        __asm__ volatile("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        const unsigned long long xcr0 = (static_cast<unsigned long long>(hi) << 32) | lo;
#endif
        ymm_os = (xcr0 & 0x6) == 0x6;
        zmm_os = (xcr0 & 0xE6) == 0xE6;
    }
    auto need = [&](bool have, const char* name) { if (!have) missing += missing.empty() ? name : std::string(", ") + name; };
#ifdef LAYA_REQUIRE_AVX
    need((ecx1 & (1u << 28)) && ymm_os, "AVX");
#endif
#ifdef LAYA_REQUIRE_AVX2
    need((ebx7 & (1u << 5)) && ymm_os, "AVX2");
#endif
#ifdef LAYA_REQUIRE_FMA
    need((ecx1 & (1u << 12)) && ymm_os, "FMA");
#endif
#ifdef LAYA_REQUIRE_F16C
    need(ecx1 & (1u << 29), "F16C");
#endif
#ifdef LAYA_REQUIRE_BMI2
    need(ebx7 & (1u << 8), "BMI2");
#endif
#ifdef LAYA_REQUIRE_AVX512F
    need((ebx7 & (1u << 16)) && zmm_os, "AVX512F");
#endif
    (void)ebx7; (void)zmm_os; (void)ymm_os; (void)need;
#endif
    return missing;
}

#ifdef _WIN32
// Diagnostics: with LAYA_CRASH_REPORT=1 in the environment, hardware exceptions (access violation,
// illegal instruction, stack overflow, ...) are reported to stderr and laya-crash.txt with module
// offsets and a stack trace before the host handles them. Offsets map to source lines with
// addr2line against the matching unstripped build.
void describe_address(char* out, size_t size, void* address) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) && module) {
        GetModuleFileNameA(module, name, sizeof name);
        const char* base = std::strrchr(name, '\\');
        std::snprintf(out, size, "%s+0x%llx", base ? base + 1 : name,
                      static_cast<unsigned long long>(static_cast<char*>(address) - reinterpret_cast<char*>(module)));
    } else {
        std::snprintf(out, size, "%p", address);
    }
}
LONG CALLBACK crash_reporter(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // Every error-severity code (access violations, illegal instructions, x87 and SSE
    // floating-point traps such as 0xC00002B4/0xC00002B5, stack overflow, fail-fast, ...),
    // but not C++ exceptions, which are thrown and caught as part of normal error handling.
    if ((code & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    static std::atomic<int> reported{0};
    if (reported++ > 2) return EXCEPTION_CONTINUE_SEARCH;
    char where[512], line[640];
    std::string report;
    describe_address(where, sizeof where, info->ExceptionRecord->ExceptionAddress);
    std::snprintf(line, sizeof line, "laya_c %s crash report: exception 0x%08lx at %s thread %lu\n", LAYA_C_VERSION,
                  static_cast<unsigned long>(code), where, static_cast<unsigned long>(GetCurrentThreadId()));
    report += line;
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && info->ExceptionRecord->NumberParameters >= 2) {
        std::snprintf(line, sizeof line, "  %s address 0x%llx\n",
                      info->ExceptionRecord->ExceptionInformation[0] == 0 ? "reading" :
                      info->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "executing",
                      static_cast<unsigned long long>(info->ExceptionRecord->ExceptionInformation[1]));
        report += line;
    }
    void* frames[48];
    const USHORT count = RtlCaptureStackBackTrace(0, 48, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        describe_address(where, sizeof where, frames[i]);
        std::snprintf(line, sizeof line, "  #%02u %s\n", unsigned(i), where);
        report += line;
    }
    std::fputs(report.c_str(), stderr);
    std::fflush(stderr);
    if (FILE* file = std::fopen("laya-crash.txt", "a")) { std::fputs(report.c_str(), file); std::fclose(file); }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

void install_hooks() {
    static std::once_flag once;
    std::call_once(once, [] {
        ggml_log_set(forward_log, nullptr);
        ggml_set_abort_callback(forward_abort);
#ifdef _WIN32
        if (const char* flag = std::getenv("LAYA_CRASH_REPORT"); flag && *flag && *flag != '0')
            AddVectoredExceptionHandler(1, crash_reporter);
#endif
    });
}

char* copy(const std::string& text) {
    auto* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (out) std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}
char* error_json(const std::string& message) {
    set_last_error(message);
    return copy(laya::json{{"error", message}}.dump(-1, ' ', false, laya::json::error_handler_t::replace));
}
std::filesystem::path utf8_path(const char* text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}
std::string path_utf8(const std::filesystem::path& path) {
    auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}
laya::json parse_requests(const char* text) {
    if (!text) throw std::invalid_argument("request_json is NULL");
    auto value = laya::json::parse(text);
    return value.is_array() ? value : laya::json::array({value});
}
std::string describe(const std::exception& e) { return e.what(); }
}  // namespace

#ifdef _WIN32
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    // ggml starts its worker threads inside our calls. Whatever FP state a new thread starts
    // with (an x64 emulator may hand it the host's unmasked state), mask exceptions on it.
    if (reason == DLL_THREAD_ATTACH && calls_in_progress.load() > 0) mask_fp_exceptions_on_this_thread();
    return TRUE;
}
#endif

extern "C" {

LAYA_API const char* LAYA_CALL laya_version(void) {
    static const std::string text = std::string("laya_c " LAYA_C_VERSION " (api ") +
        std::to_string(LAYA_C_API_VERSION) + "; backends: cpu"
#ifdef LAYA_CUDA
        ",cuda"
#endif
#ifdef LAYA_VULKAN
        ",vulkan"
#endif
        ")";
    return text.c_str();
}

LAYA_API int LAYA_CALL laya_api_version(void) { return LAYA_C_API_VERSION; }

LAYA_API void LAYA_CALL laya_set_log_callback(laya_log_fn fn, void* user, int min_level) {
    install_hooks();
    std::lock_guard<std::mutex> guard(log_lock);
    log_fn = fn;
    log_user = user;
    log_min = fn ? min_level : int(GGML_LOG_LEVEL_WARN);
}

LAYA_API laya_agent* LAYA_CALL laya_create(const char* model_dir, const char* options_json) {
    debug("create: enter", debug_enabled() ? fp_registers().c_str() : nullptr);
    call_guard fp;
    debug("create: floating-point guard set", debug_enabled() ? fp_registers().c_str() : nullptr);
    clear_last_error();
    debug("create: thread-local error text cleared");
    install_hooks();
    debug("create: checking arguments");
    try {
        if (!model_dir || !*model_dir) throw std::invalid_argument("model_dir is empty");
        const auto options = options_json && *options_json ? laya::json::parse(options_json) : laya::json::object();
        if (!options.is_object()) throw std::invalid_argument("options_json must be a JSON object");
        for (auto& [key, unused] : options.items())
            if (key != "backend" && key != "variant" && key != "precision" && key != "flash" &&
                key != "tensor_core" && key != "allow_truncation" && key != "threads" && key != "device")
                throw std::invalid_argument("Unknown option: " + key);

        const std::string backend_text = options.value("backend", "cpu");
        laya::backend_type backend;
        if (backend_text == "cpu") backend = laya::backend_type::cpu;
        else if (backend_text == "cuda") backend = laya::backend_type::cuda;
        else if (backend_text == "vulkan") {
            backend = laya::backend_type::vulkan;
#if defined(__APPLE__) && defined(LAYA_VULKAN)
            // liblaya.dylib weak-links MoltenVK (Vulkan on Metal); without it the symbols are absent.
            if (!dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr"))
                throw std::runtime_error("Vulkan is not available: put libMoltenVK.dylib next to liblaya.dylib, "
                                         "or use the cpu backend");
#endif
#if defined(__linux__) && defined(LAYA_VULKAN_LAZY)
            // liblaya.so opens libvulkan.so.1 on first use (capi/vulkan_lazy.c) rather than linking it.
            if (!laya_vulkan_loader_available())
                throw std::runtime_error("Vulkan is not installed on this computer (libvulkan.so.1 not found); "
                                         "install the GPU driver and Vulkan loader, or use the cpu backend");
#endif
#if defined(_WIN32) && defined(LAYA_VULKAN)
            // laya.dll delay-loads vulkan-1.dll (installed with GPU drivers), so the library and the
            // CPU backend work on machines without it; check before anything touches Vulkan.
            if (!LoadLibraryW(L"vulkan-1.dll"))
                throw std::runtime_error("Vulkan is not installed on this computer (vulkan-1.dll not found); "
                                         "install or update the GPU driver, or use the cpu backend");
#endif
        }
        else throw std::invalid_argument("Unknown backend: " + backend_text);

        const std::string precision_text = options.value("precision", "fp32");
        laya::precision_type precision;
        if (precision_text == "fp32") precision = laya::precision_type::fp32;
        else if (precision_text == "fp16") precision = laya::precision_type::fp16;
        else if (precision_text == "bf16") precision = laya::precision_type::bf16;
        else throw std::invalid_argument("Unknown precision: " + precision_text);

        // Same resolution as laya-cli: a store root plus a variant selects <root>/<variant>,
        // except "english", which lives at the root. A checkpoint directory is used as given.
        auto directory = utf8_path(model_dir);
        std::string variant = options.value("variant", "");
        if (!variant.empty() && variant != "english" && variant != "multilingual" && variant != "typed-decisions")
            throw std::invalid_argument("Unknown model variant: " + variant);
        if (!variant.empty() && variant != "english" &&
            (std::filesystem::exists(directory / variant / "rl_agent_config.json") ||
             !std::filesystem::exists(directory / "rl_agent_config.json")))
            directory /= variant;

        std::string device;
        if (options.contains("device")) {
            const auto& d = options.at("device");
            if (d.is_number_integer() && d.get<int>() >= 0) device = std::to_string(d.get<int>());
            else if (d.is_string()) device = d.get<std::string>();
            else throw std::invalid_argument("device must be a GPU index (0, 1, ...) or part of its name");
        }
        const int threads = options.value("threads", 0);
        if (threads < 0 || threads > 1024) throw std::invalid_argument("threads must be 0..1024");
        const bool flash = options.value("flash", precision != laya::precision_type::fp32);
        const bool tensor_core = options.value("tensor_core", false);
        const bool allow_truncation = options.value("allow_truncation", false);

        // Checked after the arguments so that bad input is reported as such.
        if (const auto missing = missing_cpu_features(); !missing.empty())
            throw std::runtime_error("This laya build needs CPU instructions your processor (or virtual machine) "
                                     "does not provide: " + missing + ". Use a build for an older CPU baseline.");
        static std::mutex create_lock;  // set_cpu_threads is process-wide; keep creation atomic
        std::lock_guard<std::mutex> guard(create_lock);
        debug("create: loading model", path_utf8(directory).c_str());
        laya::set_cpu_threads(threads ? threads : int(std::max(1u, std::thread::hardware_concurrency())));
        laya::set_gpu_device(device);
        auto* agent = new laya_agent{laya::agent(directory, backend, precision, flash, tensor_core, allow_truncation),
                                     {}, path_utf8(directory), {}, {}};
        laya::set_cpu_threads(0);
        laya::set_gpu_device("");
        debug("create: model loaded");

        std::ifstream config_file(directory / "rl_agent_config.json");
        const auto config = laya::json::parse(config_file, nullptr, false);
        if (!config.is_discarded()) {
            agent->model_name = config.value("model_name", "");
            agent->max_len = config.value("max_len", 512);
            agent->head_max_len = config.value("head_max_len", 192);
            agent->variant = agent->model_name == "laya-typed-decisions" ? "typed-decisions" :
                             config.value("encoder", "") == "jhu-clsp/mmBERT-base" ? "multilingual" : "english";
        }
        debug("create: done");
        return agent;
    } catch (const std::exception& e) {
        debug("create: failed", e.what());
        laya::set_cpu_threads(0);
        laya::set_gpu_device("");
        set_last_error(describe(e));
    } catch (...) {
        laya::set_cpu_threads(0);
        laya::set_gpu_device("");
        set_last_error("Unknown failure while loading the model");
    }
    debug("create: returning NULL");
    return nullptr;
}

LAYA_API char* LAYA_CALL laya_predict(laya_agent* agent, const char* request_json) {
    debug("predict: enter");
    call_guard fp;
    clear_last_error();
    try {
        if (!agent) throw std::invalid_argument("agent is NULL");
        auto requests = parse_requests(request_json);
        std::lock_guard<std::mutex> guard(agent->lock);  // direct calls on one agent must be serialized
        debug("predict: running the model");
        const auto start = std::chrono::steady_clock::now();
        auto results = agent->impl.predict(requests);
        debug("predict: done");
        const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return copy(laya::json{{"results", results}, {"elapsed_ms", elapsed}, {"backend", agent->impl.backend_name()},
                               {"device", agent->impl.device_name()}}
                        .dump(-1, ' ', false, laya::json::error_handler_t::replace));
    } catch (const std::bad_alloc&) {
        return error_json("Out of memory");
    } catch (const std::exception& e) {
        debug("predict: failed", e.what());
        return error_json(describe(e));
    } catch (...) {
        return error_json("Unknown failure during prediction");
    }
}

LAYA_API char* LAYA_CALL laya_prepare(laya_agent* agent, const char* request_json) {
    call_guard fp;
    clear_last_error();
    try {
        if (!agent) throw std::invalid_argument("agent is NULL");
        auto requests = parse_requests(request_json);
        std::lock_guard<std::mutex> guard(agent->lock);
        return copy(agent->impl.prepare_json(requests).dump());
    } catch (const std::exception& e) {
        return error_json(describe(e));
    } catch (...) {
        return error_json("Unknown failure during preparation");
    }
}

LAYA_API char* LAYA_CALL laya_info(laya_agent* agent) {
    call_guard fp;
    clear_last_error();
    try {
        if (!agent) throw std::invalid_argument("agent is NULL");
        return copy(laya::json{{"backend", agent->impl.backend_name()}, {"device", agent->impl.device_name()},
                               {"model_dir", agent->directory}, {"model_name", agent->model_name},
                               {"variant", agent->variant}, {"max_len", agent->max_len},
                               {"head_max_len", agent->head_max_len}}
                        .dump(-1, ' ', false, laya::json::error_handler_t::replace));
    } catch (const std::exception& e) {
        return error_json(describe(e));
    }
}

LAYA_API void LAYA_CALL laya_free_string(char* text) { std::free(text); }

LAYA_API void LAYA_CALL laya_destroy(laya_agent* agent) {
    debug("destroy: enter");
    call_guard fp;
    try { delete agent; } catch (...) {}
    debug("destroy: done");
}

LAYA_API const char* LAYA_CALL laya_last_error(void) { return last_error_text; }

}  // extern "C"
