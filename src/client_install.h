/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_INSTALL_H
#define MUSE_INSTALL_H
#include "client.h"
#ifdef __cplusplus
extern "C" {
#endif
struct muse_install;
struct muse_install_status {
    char state[32], detail[256], title_id[10], content_id[48];
    int submitted, installed, completed, launch_requested, running_observed;
    int progress_percent, native_error, app_id;
};
/* Fresh elfldr helper per approved generated package. No existing title removal. */
struct muse_install *muse_install_begin(struct muse_client *, const char *path,
    const struct muse_build_status *, struct muse_install_status *);
int muse_install_poll(struct muse_install *, struct muse_install_status *);
/* Requires matching native completed status; playable alone cannot launch. */
int muse_install_launch(struct muse_install *, struct muse_install_status *);
/* Stops monitoring and closes helper. Does not cancel an OS-accepted installation. */
void muse_install_close(struct muse_install *);
/* Retries a retired helper's spool. A live helper holds an exclusive file lock. */
int muse_install_recover_logs(struct muse_client *);
#ifdef __cplusplus
}
#endif
#endif
