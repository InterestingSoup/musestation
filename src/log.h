/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_LOG_H
#define MUSE_LOG_H
#include "client.h"
#ifdef __cplusplus
extern "C" {
#endif
struct muse_log { struct muse_client *client; char path[1024]; unsigned pending; int fd_mode, spool_fd, marker_fd, local_only, recovery_pending; };
/* Persist recovered crash event before first POST. An offline durable logger
 * opens successfully with recovery_pending set; operation_begin fails until
 * delivery succeeds. Failure/close records still append durably while offline. */
int muse_log_open(struct muse_log *log, struct muse_client *client);
/* Borrowed writable regular-file descriptors; caller retains them through close.
 * This path never reopens the spool/marker by pathname. The supplier must fsync
 * the parent directory after creating descriptor-backed journal files. */
int muse_log_open_fds(struct muse_log *, struct muse_client *, int spool, int marker);
/* fsync every event; failure/crash/session_end trigger immediate best-effort POST. */
int muse_log_event(struct muse_log *log, const char *level, const char *event, const char *detail);
/* Upload snapshot then truncate only after confirmed 2xx. Failures retain data. */
int muse_log_flush(struct muse_log *log);
/* Failed upload retains marker and spool. Local mode never uploads records. */
void muse_log_close(struct muse_log *log);
#ifdef __cplusplus
}
#endif
#endif
