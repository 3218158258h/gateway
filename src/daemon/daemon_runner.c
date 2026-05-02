#include "../include/daemon_runner.h"
#include "../include/daemon_process.h"
#include "../include/daemon_config.h"
#include "../thirdparty/log.c/log.h"

#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>

/* 两个子进程：app + ota。 */
static SubProcess subprocess[2];
static volatile sig_atomic_t is_running = 1;
static int restart_enabled[2] = {1, 1};
static DaemonConfig g_daemon_cfg;

/* 崩溃窗口状态：用于熔断与退避。 */
static int crash_count = 0;
static time_t last_crash_time = 0;
static time_t restart_block_until = 0;

static void daemon_runner_close(int sig)
{
    (void)sig;
    is_running = 0;
}

static int sleep_ms(int ms)
{
    if (ms <= 0) {
        return 0;
    }
    return usleep((useconds_t)ms * 1000U);
}

static int redirect_stdio(const char *log_file)
{
    int fd_stdin = open("/dev/null", O_RDONLY);
    if (fd_stdin < 0) {
        return -1;
    }
    if (dup2(fd_stdin, STDIN_FILENO) < 0) {
        close(fd_stdin);
        return -1;
    }
    if (fd_stdin > STDERR_FILENO) {
        close(fd_stdin);
    }

    const char *log_path = (log_file && log_file[0]) ? log_file : "/tmp/gateway.log";
    int fd_log = open(log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_log < 0) {
        /* 兜底路径，避免标准输出悬空。 */
        fd_log = open("/tmp/gateway.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd_log < 0) {
            return -1;
        }
    }
    if (dup2(fd_log, STDOUT_FILENO) < 0 || dup2(fd_log, STDERR_FILENO) < 0) {
        close(fd_log);
        return -1;
    }
    if (fd_log > STDERR_FILENO) {
        close(fd_log);
    }
    return 0;
}

static void update_crash_window(time_t now)
{
    /* 超过重置窗口后清零，避免历史崩溃永久影响重启策略。 */
    if (last_crash_time > 0 &&
        (now - last_crash_time) > g_daemon_cfg.crash_count_reset_interval_sec) {
        crash_count = 0;
    }
}

static void write_error_marker(void)
{
    int fd = open(g_daemon_cfg.error_marker_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_warn("Failed to open crash marker: %s", strerror(errno));
        return;
    }

    char buf[128];
    int n = snprintf(buf, sizeof(buf), "crash_count=%d block_until=%ld\n",
                     crash_count, (long)restart_block_until);
    if (n > 0) {
        if (write(fd, buf, (size_t)n) < 0) {
            log_warn("Failed to write crash marker: %s", strerror(errno));
        }
    }
    close(fd);
}

static int check_restart_allowed(time_t now)
{
    /* 熔断窗口内直接拒绝重启请求。 */
    update_crash_window(now);
    if (restart_block_until > now) {
        return 0;
    }
    if (restart_block_until != 0) {
        restart_block_until = 0;
        crash_count = 0;
    }
    return 1;
}

static int compute_backoff_ms(void)
{
    /* 指数退避：崩溃越密集，重启间隔越长，直到上限。 */
    if (crash_count <= 0 || g_daemon_cfg.restart_backoff_ms <= 0) {
        return 0;
    }
    int backoff = g_daemon_cfg.restart_backoff_ms;
    int steps = crash_count - 1;
    while (steps-- > 0 && backoff < g_daemon_cfg.restart_backoff_max_ms) {
        backoff *= 2;
        if (backoff < 0 || backoff > g_daemon_cfg.restart_backoff_max_ms) {
            backoff = g_daemon_cfg.restart_backoff_max_ms;
            break;
        }
    }
    return backoff;
}

static void note_abnormal_exit(time_t now)
{
    /* 记录异常退出并在超阈值时进入熔断。 */
    crash_count++;
    last_crash_time = now;
    if (crash_count > g_daemon_cfg.max_crash_count) {
        restart_block_until = now + g_daemon_cfg.crash_count_reset_interval_sec;
        log_error("Crash threshold exceeded: crash_count=%d, block_until=%ld",
                  crash_count, (long)restart_block_until);
        write_error_marker();
    }
}

static int start_subprocess_at(int index, int is_retry)
{
    if (index < 0 || index >= 2) {
        return -1;
    }
    time_t now = time(NULL);
    if (!check_restart_allowed(now)) {
        return -1;
    }

    /* 重试启动路径应用退避，首次启动不延迟。 */
    int backoff_ms = is_retry ? compute_backoff_ms() : 0;
    if (backoff_ms > 0) {
        sleep_ms(backoff_ms);
    }

    if (daemon_process_start(&subprocess[index]) != 0) {
        note_abnormal_exit(time(NULL));
        log_error("Failed to start subprocess name=%s program=%s",
                  subprocess[index].name ? subprocess[index].name : "unknown",
                  subprocess[index].program_path ? subprocess[index].program_path : "unknown");
        return -1;
    }
    return 0;
}

static int find_subprocess_index_by_pid(pid_t pid)
{
    for (int i = 0; i < 2; i++) {
        if (subprocess[i].pid == pid) {
            return i;
        }
    }
    return -1;
}

static void handle_child_exit(pid_t pid, int status)
{
    int index = find_subprocess_index_by_pid(pid);
    if (index < 0) {
        return;
    }

    const char *name = subprocess[index].name ? subprocess[index].name : "unknown";
    int normal_exit = WIFEXITED(status) && WEXITSTATUS(status) == 0;

    subprocess[index].pid = -1;

    /* 正常退出是否重启由 daemon.ini 显式控制。 */
    if (normal_exit) {
        log_info("Subprocess %s (pid=%d) exited normally", name, pid);
        if (!g_daemon_cfg.restart_on_normal_exit) {
            restart_enabled[index] = 0;
            return;
        }
    } else {
        note_abnormal_exit(time(NULL));
        log_warn("Subprocess %s (pid=%d) exited abnormally, status=%d",
                 name, pid, status);
    }

    if (is_running && restart_enabled[index]) {
        if (start_subprocess_at(index, 1) != 0) {
            log_warn("Restart deferred for subprocess %s", name);
        }
    }
}

int daemon_runner_run()
{
    char daemon_cfg_path[CONFIG_MAX_PATH_LEN] = {0};
    snprintf(daemon_cfg_path, sizeof(daemon_cfg_path), "%s", APP_DAEMON_CONFIG_FILE);
    ConfigManager gateway_cfg = {0};
    if (config_init(&gateway_cfg, APP_GATEWAY_CONFIG_FILE) == 0 &&
        config_load(&gateway_cfg) == 0) {
        config_get_string(&gateway_cfg, "config_files", "daemon",
                          APP_DAEMON_CONFIG_FILE,
                          daemon_cfg_path, sizeof(daemon_cfg_path));
    }
    config_destroy(&gateway_cfg);

    if (daemon_config_load(&g_daemon_cfg, daemon_cfg_path) != 0) {
        return -1;
    }

    if (daemon(0, 1) < 0) {
        return -1;
    }

    if (redirect_stdio(g_daemon_cfg.log_file) != 0) {
        return -1;
    }

    if (daemon_process_init(&subprocess[0], g_daemon_cfg.program_path, g_daemon_cfg.app_arg) != 0) {
        log_error("Failed to init app subprocess");
        return -1;
    }
    if (daemon_process_init(&subprocess[1], g_daemon_cfg.program_path, g_daemon_cfg.ota_arg) != 0) {
        log_error("Failed to init ota subprocess");
        daemon_process_free(&subprocess[0]);
        return -1;
    }

    signal(SIGTERM, daemon_runner_close);
    signal(SIGINT, daemon_runner_close);

    if (start_subprocess_at(0, 0) != 0) {
        log_warn("Initial start failed for app subprocess");
    }
    if (start_subprocess_at(1, 0) != 0) {
        log_warn("Initial start failed for ota subprocess");
    }

    /*
     * 主循环职责：
     * 1) 回收已退出子进程；
     * 2) 按策略拉起未运行子进程；
     * 3) 空闲时按 monitor_interval 休眠。
     */
    while (is_running) {
        int status = 0;
        pid_t pid = 0;
        int handled_exit = 0;

        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            handled_exit = 1;
            handle_child_exit(pid, status);
        }

        if (pid < 0 && errno != ECHILD && errno != EINTR) {
            log_warn("waitpid error: %s", strerror(errno));
        }

        /* 若子进程因启动失败而处于未运行状态，周期尝试拉起。 */
        for (int i = 0; i < 2; i++) {
            if (!is_running) {
                break;
            }
            if (restart_enabled[i] && subprocess[i].pid <= 0) {
                (void)start_subprocess_at(i, 1);
            }
        }

        if (!handled_exit) {
            sleep_ms(g_daemon_cfg.monitor_interval_ms);
        }
    }

    for (int i = 0; i < 2; i++) {
        daemon_process_stop(&subprocess[i]);
        daemon_process_free(&subprocess[i]);
    }

    return 0;
}
