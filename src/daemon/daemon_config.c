#include "../../include/daemon_config.h"
#include "../../thirdparty/log.c/log.h"

#include <string.h>
#include <stdio.h>

void daemon_config_set_defaults(DaemonConfig *config)
{
    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    snprintf(config->program_path, sizeof(config->program_path), "%s", "/home/root/gateway/gateway");
    snprintf(config->log_file, sizeof(config->log_file), "%s", "/var/log/gateway.log");
    snprintf(config->error_marker_file, sizeof(config->error_marker_file), "%s", "/tmp/gateway.error");
    snprintf(config->app_arg, sizeof(config->app_arg), "%s", "app");
    snprintf(config->ota_arg, sizeof(config->ota_arg), "%s", "ota");

    config->max_crash_count = 10;
    config->crash_count_reset_interval_sec = 300;
    config->monitor_interval_ms = 100;
    config->restart_backoff_ms = 300;
    config->restart_backoff_max_ms = 10000;
    config->restart_on_normal_exit = 1;
}

static void daemon_config_sanitize(DaemonConfig *config)
{
    if (!config) {
        return;
    }
    if (config->program_path[0] == '\0') {
        snprintf(config->program_path, sizeof(config->program_path), "%s", "/home/root/gateway/gateway");
    }
    if (config->log_file[0] == '\0') {
        snprintf(config->log_file, sizeof(config->log_file), "%s", "/var/log/gateway.log");
    }
    if (config->error_marker_file[0] == '\0') {
        snprintf(config->error_marker_file, sizeof(config->error_marker_file), "%s", "/tmp/gateway.error");
    }
    if (config->app_arg[0] == '\0') {
        snprintf(config->app_arg, sizeof(config->app_arg), "%s", "app");
    }
    if (config->ota_arg[0] == '\0') {
        snprintf(config->ota_arg, sizeof(config->ota_arg), "%s", "ota");
    }

    if (config->max_crash_count <= 0) {
        config->max_crash_count = 10;
    }
    if (config->crash_count_reset_interval_sec <= 0) {
        config->crash_count_reset_interval_sec = 300;
    }
    if (config->monitor_interval_ms <= 0) {
        config->monitor_interval_ms = 100;
    }
    if (config->restart_backoff_ms < 0) {
        config->restart_backoff_ms = 300;
    }
    if (config->restart_backoff_max_ms < config->restart_backoff_ms) {
        config->restart_backoff_max_ms = config->restart_backoff_ms;
    }
}

int daemon_config_load(DaemonConfig *config, const char *config_file)
{
    if (!config) {
        return -1;
    }

    daemon_config_set_defaults(config);

    const char *cfg_file = (config_file && config_file[0]) ? config_file : APP_DAEMON_CONFIG_FILE;
    ConfigManager cfg = {0};
    if (config_init(&cfg, cfg_file) != 0) {
        log_warn("daemon config init failed: %s, use defaults", cfg_file);
        return 0;
    }
    if (config_load(&cfg) != 0) {
        log_warn("daemon config load failed: %s, use defaults", cfg_file);
        config_destroy(&cfg);
        return 0;
    }

    config_get_string(&cfg, "daemon", "program_path", config->program_path,
                      config->program_path, sizeof(config->program_path));
    config_get_string(&cfg, "daemon", "log_file", config->log_file,
                      config->log_file, sizeof(config->log_file));
    config_get_string(&cfg, "daemon", "error_marker_file", config->error_marker_file,
                      config->error_marker_file, sizeof(config->error_marker_file));
    config_get_string(&cfg, "daemon", "app_arg", config->app_arg,
                      config->app_arg, sizeof(config->app_arg));
    config_get_string(&cfg, "daemon", "ota_arg", config->ota_arg,
                      config->ota_arg, sizeof(config->ota_arg));

    config->max_crash_count = config_get_int(&cfg, "daemon", "max_crash_count",
                                             config->max_crash_count);
    config->crash_count_reset_interval_sec = config_get_int(&cfg, "daemon",
                                                            "crash_count_reset_interval_sec",
                                                            config->crash_count_reset_interval_sec);
    config->monitor_interval_ms = config_get_int(&cfg, "daemon", "monitor_interval_ms",
                                                 config->monitor_interval_ms);
    config->restart_backoff_ms = config_get_int(&cfg, "daemon", "restart_backoff_ms",
                                                config->restart_backoff_ms);
    config->restart_backoff_max_ms = config_get_int(&cfg, "daemon", "restart_backoff_max_ms",
                                                    config->restart_backoff_max_ms);
    config->restart_on_normal_exit = config_get_bool(&cfg, "daemon", "restart_on_normal_exit",
                                                     config->restart_on_normal_exit);

    config_destroy(&cfg);
    daemon_config_sanitize(config);

    return 0;
}
