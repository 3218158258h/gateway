#ifndef __DAEMON_CONFIG_H__
#define __DAEMON_CONFIG_H__

#include "app_config.h"

#define APP_DAEMON_CONFIG_FILE "config/daemon.ini"
#define DAEMON_ARG_MAX_LEN 32

typedef struct DaemonConfigStruct {
    char program_path[CONFIG_MAX_PATH_LEN];
    char log_file[CONFIG_MAX_PATH_LEN];
    char error_marker_file[CONFIG_MAX_PATH_LEN];
    char app_arg[DAEMON_ARG_MAX_LEN];
    char ota_arg[DAEMON_ARG_MAX_LEN];
    int max_crash_count;
    int crash_count_reset_interval_sec;
    int monitor_interval_ms;
    int restart_backoff_ms;
    int restart_backoff_max_ms;
    int restart_on_normal_exit;
} DaemonConfig;

void daemon_config_set_defaults(DaemonConfig *config);
int daemon_config_load(DaemonConfig *config, const char *config_file);

#endif /* __DAEMON_CONFIG_H__ */

