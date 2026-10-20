/* SPDX-License-Identifier: GPL-3.0-or-later
 * Public AppInstUtil ABI: itsPLK/ps5-pkg-manager 914311f945d14a555e1c09635346bb069c3368e8,
 * include/install_appinst.h and include/install_service.h (GPL-3.0).
 * Launch ABI: john-tornblom/ps5-payload-hbldr hbldr.c; title query:
 * itsPLK/ps5-payload-manager 3f881e8e569eba3054d1784a1962439b2eee92c2 src/ps5_launcher.c.
 * AppExists output-flag ABI: flatz/ps4_remote_pkg_installer installer.c.
 * An unchanged output sentinel is rejected as an unsupported runtime ABI.
 */
#ifndef MUSE_APPINST_ABI_H
#define MUSE_APPINST_ABI_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { const char *uri,*ex_uri,*playgo_scenario_id,*content_id,*content_name,*icon_url; } muse_pkg_metadata;
typedef struct { char content_id[48]; int type,platform; } muse_pkg_info;
typedef struct { char languages[30][8],playgo_scenario_ids[64][3],content_ids[64][48]; unsigned char unknown[6480]; } muse_playgo_info;
typedef struct { int32_t error_code,version; char description[512],type[9]; } muse_install_error;
typedef struct { char status[16],src_type[8]; uint32_t remain_time; uint64_t downloaded_size,initial_chunk_size,total_size; uint32_t promote_progress; muse_install_error error_info; int32_t local_copy_percent; bool is_copy_only; } muse_native_install_status;
typedef struct { uint32_t structsize,user_id,app_opt; uint64_t crash_report; uint32_t check_flag; } muse_launch_context;
_Static_assert(sizeof(muse_pkg_info)==56,"AppInst package ABI");
_Static_assert(sizeof(muse_native_install_status)==600,"AppInst status ABI");
_Static_assert(sizeof(muse_launch_context)==32,"SystemService launch ABI");
int sceSystemServiceLaunchApp(const char *,const char **,muse_launch_context *);
int sceSystemServiceGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t,char *);
int sceUserServiceGetForegroundUser(uint32_t *);
int sceUserServiceInitialize(int *);
int sceNetInit(void);
int sceNetCtlInit(void);
#endif
