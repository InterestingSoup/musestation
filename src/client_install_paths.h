/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_INSTALL_PATHS_H
#define MUSE_INSTALL_PATHS_H
#include <stddef.h>
#include <stdint.h>
/* Portable IPC description; no native struct statfs crosses the socket. */
struct muse_path_locator {
    char direct[1024], relative[1024];
    unsigned char fsid[8];
    uint32_t has_mount;
};
struct muse_mount_record { unsigned char fsid[8]; char path[1024]; };
typedef int (*muse_path_probe)(const char *, void *);
int muse_path_describe(const char *local, const char *override,
                      struct muse_path_locator *);
/* Probe direct path, then at most 512 matching mount records; never scan files. */
int muse_path_resolve(const struct muse_path_locator *, muse_path_probe, void *,
                      char *out, size_t capacity);
/* Separated for synthetic namespace tests using the production mapping logic. */
int muse_path_resolve_records(const struct muse_path_locator *,
                      const struct muse_mount_record *, size_t count,
                      muse_path_probe, void *, char *out, size_t capacity);
int muse_path_valid(const char *);
#endif
