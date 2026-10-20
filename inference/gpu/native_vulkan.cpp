// SPDX-License-Identifier: GPL-3.0-or-later
// Direct ICD dispatch and once-only AGC initialization follow PS5LM's pinned
// native title recipe. Both the GL renderer and RADV share the system AGC.
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdint>
extern "C" {
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(VkInstance, const char *);
PFN_vkVoidFunction vk_common_GetDeviceProcAddr(VkDevice, const char *);
void vk_common_GetPhysicalDeviceFeatures2(VkPhysicalDevice, VkPhysicalDeviceFeatures2 *);
void vk_common_CmdCopyBuffer(VkCommandBuffer, VkBuffer, VkBuffer, uint32_t, const VkBufferCopy *);
PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance v, const char *n) { return vk_icdGetInstanceProcAddr(v,n); }
PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice v, const char *n) { return vk_common_GetDeviceProcAddr(v,n); }
void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice v, VkPhysicalDeviceFeatures2 *p) { vk_common_GetPhysicalDeviceFeatures2(v,p); }
void vkCmdCopyBuffer(VkCommandBuffer c,VkBuffer s,VkBuffer d,uint32_t n,const VkBufferCopy *r) { vk_common_CmdCopyBuffer(c,s,d,n,r); }
int __real_sceAgcInit(uint32_t);
int sceKernelUsleep(uint32_t);
int __wrap_sceAgcInit(uint32_t flags) {
    static std::atomic<int> state{0};
    static int result = 0;
    int expected = 0;
    if (state.compare_exchange_strong(expected,1,std::memory_order_acq_rel)) {
        result = __real_sceAgcInit(flags);
        state.store(2,std::memory_order_release);
    } else {
        while (state.load(std::memory_order_acquire) != 2) sceKernelUsleep(100);
    }
    return result;
}
}
