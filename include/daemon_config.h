#ifndef __DAEMON_CONFIG_H__
#define __DAEMON_CONFIG_H__

#include "app_config.h"

#define APP_DAEMON_CONFIG_FILE "config/daemon.ini"
#define DAEMON_ARG_MAX_LEN 32

typedef struct DaemonConfigStruct {
    /* 被守护进程拉起的可执行程序路径。 */
    char program_path[CONFIG_MAX_PATH_LEN];
    /* 子进程标准输出/标准错误重定向日志文件。 */
    char log_file[CONFIG_MAX_PATH_LEN];
    /* 子进程异常退出时用于外部检测的标记文件。 */
    char error_marker_file[CONFIG_MAX_PATH_LEN];
    /* 主应用运行参数（例如 app）。 */
    char app_arg[DAEMON_ARG_MAX_LEN];
    /* OTA 子命令参数（例如 ota）。 */
    char ota_arg[DAEMON_ARG_MAX_LEN];
    /* 在重置窗口内允许的最大崩溃次数。 */
    int max_crash_count;
    /* 崩溃计数重置窗口（秒）。 */
    int crash_count_reset_interval_sec;
    /* 守护循环监控间隔（毫秒）。 */
    int monitor_interval_ms;
    /* 重启退避初始值（毫秒）。 */
    int restart_backoff_ms;
    /* 重启退避上限（毫秒）。 */
    int restart_backoff_max_ms;
    /* 子进程正常退出后是否仍自动重启。 */
    int restart_on_normal_exit;
} DaemonConfig;

/* 写入默认守护参数，作为配置缺失或读取失败时的兜底。 */
void daemon_config_set_defaults(DaemonConfig *config);
/* 从 daemon.ini 加载配置并做安全归一化。 */
int daemon_config_load(DaemonConfig *config, const char *config_file);

#endif /* __DAEMON_CONFIG_H__ */
