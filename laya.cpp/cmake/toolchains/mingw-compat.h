/* Force-included by the MinGW toolchain file. Older MinGW-w64 headers (< v12, e.g. Ubuntu 24.04)
 * lack the Windows 8+ thread power-throttling declarations that ggml-cpu uses. */
#ifdef _WIN32
#include <_mingw.h>
#if defined(__MINGW64_VERSION_MAJOR) && __MINGW64_VERSION_MAJOR < 12 && !defined(THREAD_POWER_THROTTLING_CURRENT_VERSION)
typedef struct _THREAD_POWER_THROTTLING_STATE {
    unsigned long Version;
    unsigned long ControlMask;
    unsigned long StateMask;
} THREAD_POWER_THROTTLING_STATE;
#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif
#endif
