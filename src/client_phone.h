/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_CLIENT_PHONE_H
#define MUSE_CLIENT_PHONE_H
#include "config.h"
#ifdef __cplusplus
extern "C" {
#endif
struct muse_phone;
/* submit runs on phone thread; queue work and return 0, or -1 when busy.
 * The receiver owns accepted WAV files. Never approve or launch from this callback. */
struct muse_phone *muse_phone_start(const struct muse_config *config,
    int (*submit)(void *context,const char *wav_path),void *context,char *error,size_t capacity);
const char *muse_phone_token(const struct muse_phone *phone);
void muse_phone_stop(struct muse_phone *phone);
#ifdef __cplusplus
}
#endif
#endif
