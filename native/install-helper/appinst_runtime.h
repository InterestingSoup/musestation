/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_APPINST_RUNTIME_H
#define MUSE_APPINST_RUNTIME_H
#include "appinst_abi.h"
#include <stddef.h>

/* A complete table is published only after every required symbol resolves. */
struct muse_appinst_api {
    int (*initialize)(void);
    int (*terminate)(void);
    int (*app_exists)(const char *, int *);
    int (*install_package)(const muse_pkg_metadata *, muse_pkg_info *, muse_playgo_info *);
    int (*install_status)(const char *, muse_native_install_status *);
};
int muse_appinst_load(struct muse_appinst_api *, char *detail, size_t capacity);
#endif
