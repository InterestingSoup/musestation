/* SPDX-License-Identifier: GPL-3.0-or-later
 * Payload loader pattern: blackbearreloaded/ProsperoStore
 * e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4, helper/main.cpp:152-170.
 * That implementation reports an AppInstUtil startup-import failure on 12.70.
 * This changes loading, not privileges or the public AppInstUtil ABI. Firmware
 * 12.40 still requires the user's hardware test.
 */
#include "appinst_runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifndef MUSE_INSTALL_HELPER_TEST
#include <ps5/kernel.h>
#else
intptr_t kernel_dynlib_dlsym(pid_t, uint32_t, const char *);
#endif
int sceKernelLoadStartModule(const char *, size_t, const void *, unsigned, void *, int *);

int muse_appinst_load(struct muse_appinst_api *out, char *detail, size_t capacity) {
    struct muse_appinst_api candidate = {0};
    memset(out, 0, sizeof(*out));
    const int handle = sceKernelLoadStartModule(
        "/system/common/lib/libSceAppInstUtil.sprx", 0, NULL, 0, NULL, NULL);
    if (handle < 0) {
        snprintf(detail, capacity, "AppInstUtil module load failed (0x%08x); nothing installed", (unsigned)handle);
        return handle;
    }
    /* SDK resolver returns an address, or zero. Resolve in this payload process,
       not through the native-title dlsym interface. Do not initialize a partial
       table. The loaded system module stays alive for this one-shot process. */
#define RESOLVE(member, symbol) do { \
    const intptr_t address = kernel_dynlib_dlsym(getpid(), (uint32_t)handle, symbol); \
    _Static_assert(sizeof(candidate.member) == sizeof(address), "PS5 function address size"); \
    if (!address) { \
        snprintf(detail, capacity, "AppInstUtil symbol %s unavailable; nothing installed", symbol); \
        return -2010; \
    } \
    memcpy(&candidate.member, &address, sizeof(address)); \
} while (0)
    RESOLVE(initialize, "sceAppInstUtilInitialize");
    RESOLVE(terminate, "sceAppInstUtilTerminate");
    RESOLVE(app_exists, "sceAppInstUtilAppExists");
    RESOLVE(install_package, "sceAppInstUtilInstallByPackage");
    RESOLVE(install_status, "sceAppInstUtilGetInstallStatus");
#undef RESOLVE
    *out = candidate;
    if (capacity) detail[0] = 0;
    return 0;
}
