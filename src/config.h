/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_CONFIG_H
#define MUSE_CONFIG_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct muse_config {
    char build_mode[8], hybrid_provider[8];
    char console_ip[128], server_url[256], ps5lm_url[256], speech_url[256];
    char relay_token[65], shadowmount_url[256];
    char log_dir[512], artifact_dir[512], model_dir[512], audio_in_module[512];
    char install_helper_path[512];
    char coding_model_path[512]; /* Optional downloaded GGUF, outside the app bundle. */
    char installer_artifact_dir[512], installer_log_dir[512];
    char inference_backend[24], speech_mode[16];
    char model[128], firmware_reported[32];
    char elfldr_status[128], etahen_status[128], kstuff_lite_status[128];
    char shadowmountplus_status[128], ftpsrv_status[128];
    int voice_gate_verified, timeout_ms;
    unsigned short loader_port, phone_port;
};
void muse_config_defaults(struct muse_config *c);
int muse_config_local(const struct muse_config *c);
/* HTTP relay origins are restricted to numeric private/loopback/link-local IPs. */
int muse_config_private_origin(const char *url);
int muse_config_token_valid(const char *token);
/* Inventory must come from loopback or this configured console, never another LAN host. */
int muse_config_console_origin(const struct muse_config *c, const char *url);
/* Local permits blank IP/origins/AudioIn. Nonempty origins and all required paths
 * are validated. Missing build_mode while loading preserves legacy hybrid mode. */
int muse_config_validate(const struct muse_config *c, char *error, size_t capacity);
int muse_config_load(const char *path, struct muse_config *c, char *error, size_t capacity);
/* Atomic write, mode 0600; does not create missing parent directories. */
int muse_config_save(const char *path, const struct muse_config *c, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
