/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native title compatibility. Neither function substitutes inference results.
 * ggml's optional crash backtrace tries to spawn addr2line; native titles have
 * no process-spawn service. Model prefetch advice is optional, never the read. */
#include <errno.h>
#include <stddef.h>
#include <time.h>

/* libc++'s filesystem object contains timestamp mutation code even when the
 * app only requests reads. There is no verified native-title adapter yet. */
int utimensat(int directory, const char *path, const struct timespec times[2], int flags) {
    (void)directory; (void)path; (void)times; (void)flags;
    errno = ENOSYS;
    return -1;
}

int execlp(const char *file, const char *arg, ...) {
    (void)file;
    (void)arg;
    errno = ENOSYS;
    return -1;
}

int posix_madvise(void *address, size_t size, int advice) {
    (void)address;
    (void)size;
    (void)advice;
    return 0;
}
