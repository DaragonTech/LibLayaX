/* vulkan_lazy.c - load the Vulkan loader on first use instead of at library load (Linux).
 *
 * ggml's Vulkan backend resolves almost everything through vkGetInstanceProcAddr and links only
 * the four functions below directly. Linking libvulkan.so.1 normally would make liblaya.so refuse
 * to load on machines without Vulkan; with this shim linked in its place (as Vulkan_LIBRARY) the
 * library always loads, the cpu backend always works, and laya_create reports a clear error for
 * the vulkan backend when the loader is missing (see laya_vulkan_loader_available).
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stddef.h>
#include <vulkan/vulkan.h>

static void* library;
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void open_library(void) {
    library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!library) library = dlopen("libvulkan.so", RTLD_NOW | RTLD_GLOBAL);
}
static void* symbol(const char* name) {
    pthread_once(&once, open_library);
    return library ? dlsym(library, name) : NULL;
}

/* 1 if the system Vulkan loader can be opened. */
int laya_vulkan_loader_available(void) {
    pthread_once(&once, open_library);
    return library != NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    static PFN_vkGetInstanceProcAddr real;
    if (!real) real = (PFN_vkGetInstanceProcAddr)symbol("vkGetInstanceProcAddr");
    return real ? real(instance, name) : NULL;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* name) {
    static PFN_vkGetDeviceProcAddr real;
    if (!real) real = (PFN_vkGetDeviceProcAddr)symbol("vkGetDeviceProcAddr");
    return real ? real(device, name) : NULL;
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice device, VkPhysicalDeviceFeatures2* features) {
    static PFN_vkGetPhysicalDeviceFeatures2 real;
    if (!real) real = (PFN_vkGetPhysicalDeviceFeatures2)symbol("vkGetPhysicalDeviceFeatures2");
    if (real) real(device, features);
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer commands, VkBuffer source, VkBuffer destination,
                                           uint32_t count, const VkBufferCopy* regions) {
    static PFN_vkCmdCopyBuffer real;
    if (!real) real = (PFN_vkCmdCopyBuffer)symbol("vkCmdCopyBuffer");
    if (real) real(commands, source, destination, count, regions);
}
