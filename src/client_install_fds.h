/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_INSTALL_FDS_H
#define MUSE_INSTALL_FDS_H
int muse_unix_listen(const char *path);
int muse_unix_connect(const char *path);
int muse_fds_send(int socket, const int fds[3], const unsigned char token[32]);
/* Receives exactly three writable regular files and authenticates the sender. */
int muse_fds_receive(int socket, int fds[3], const unsigned char token[32]);
#endif
