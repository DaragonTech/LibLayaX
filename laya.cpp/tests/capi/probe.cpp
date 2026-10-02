// laya-probe: checks, one step at a time, the C++ runtime features laya.dll depends on (atomics,
// thread-local storage, floating-point environment, call_once, mutexes, exceptions, threads).
// It does not use laya.dll. Every step is announced before it runs, so if the program stops,
// the last line shows which feature is broken on this machine.
#include <atomic>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static int step_number = 0;
static void step(const char* name) { std::printf("%2d %s ... ", ++step_number, name); std::fflush(stdout); }
static void ok() { std::printf("ok\n"); std::fflush(stdout); }

static std::atomic<int> counter{0};
thread_local char thread_text[2048];
static std::mutex lock;

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE __declspec(noinline)
#endif
NOINLINE static void thrower(int depth, const std::string& text) {
    std::string local = text + "!";  // an object with a destructor in every frame (cleanup landing pads)
    if (depth > 0) thrower(depth - 1, local);
    else throw std::invalid_argument(local);
}

// The start-up work of the CPU engine: a 65536-entry table over every half-float bit pattern
// (NaNs, infinities and denormals included), using tanhf/expf and float<->half conversions.
static volatile float table_sink;
NOINLINE static void math_table() {
    for (int i = 0; i < (1 << 16); ++i) {
        float f;
#if defined(__aarch64__) && !defined(_MSC_VER)
        std::uint16_t bits = static_cast<std::uint16_t>(i);
        __fp16 half;
        std::memcpy(&half, &bits, sizeof half);
        f = static_cast<float>(half);
#else
        f = static_cast<float>(i - 32768) / 64.0f;
#endif
        const float gelu = 0.5f * f * (1.0f + std::tanh(0.79788456f * f * (1.0f + 0.044715f * f * f)));
        const float quick = f * (1.0f / (1.0f + std::exp(-1.702f * f)));
#if defined(__aarch64__) && !defined(_MSC_VER)
        __fp16 back = static_cast<__fp16>(gelu + quick);
        table_sink = static_cast<float>(back);
#else
        table_sink = gelu + quick;
#endif
    }
}
static void print_fp_registers() {
#if defined(__aarch64__) && !defined(_MSC_VER)
    unsigned long long fpcr = 0, fpsr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    __asm__ volatile("mrs %0, fpsr" : "=r"(fpsr));
    std::printf("(fpcr=%08llx fpsr=%08llx) ", fpcr, fpsr);
#endif
}

int main() {
    std::printf("laya-probe (%s)\n",
#if defined(__aarch64__) || defined(_M_ARM64)
                "arm64"
#else
                "x86-64"
#endif
    );
    step("atomic increment"); ++counter; --counter; ok();
    step("thread-local storage"); std::strcpy(thread_text, "hello"); ok();
    step("floating-point environment"); { std::fenv_t saved; std::feholdexcept(&saved); std::fesetenv(&saved); } ok();
    step("math table (65536 x tanhf/expf/half-float)"); print_fp_registers(); math_table(); print_fp_registers(); ok();
    step("math table inside feholdexcept"); { std::fenv_t saved; std::feholdexcept(&saved); print_fp_registers(); math_table(); std::fesetenv(&saved); print_fp_registers(); } ok();
    step("math table after the guard"); math_table(); ok();
    step("call_once"); { static std::once_flag once; std::call_once(once, [] { ++counter; }); } ok();
    step("mutex"); { std::lock_guard<std::mutex> guard(lock); ++counter; } ok();
    step("throw int, catch in the same function");
    try { throw 42; } catch (int) {}
    ok();
    step("throw std::exception through 3 frames");
    try { thrower(3, "x"); } catch (const std::exception& e) { if (std::strcmp(e.what(), "x!!!!") != 0) std::printf("(wrong text) "); }
    ok();
    step("100 exceptions in a row");
    for (int i = 0; i < 100; ++i) { try { thrower(1, "y"); } catch (...) {} }
    ok();
    step("4 threads");
    {
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back([] { for (int k = 0; k < 1000; ++k) { ++counter; try { thrower(1, "z"); } catch (...) {} } });
        for (auto& t : threads) t.join();
    }
    ok();
    std::printf("probe finished: everything works\n");
    return 0;
}
