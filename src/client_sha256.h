/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_SHA256_H
#define MUSE_SHA256_H
#include <stddef.h>
#include <stdint.h>
struct muse_sha256 { uint32_t h[8]; uint64_t length; size_t used; unsigned char tail[64]; };
void muse_sha256_init(struct muse_sha256 *);
void muse_sha256_update(struct muse_sha256 *, const void *, size_t);
void muse_sha256_final(struct muse_sha256 *, char hex[65]);
void muse_sha256_hex(const void *data,size_t length,char hex[65]);
#endif
