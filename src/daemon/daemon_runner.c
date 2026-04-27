#include "../include/daemon_runner.h"
#include "../include/daemon_process.h"
#include "../thirdparty/log.c/log.h"
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/reboot.h>
#include <string.h>
#include <time.h>
#include <errno.h>

/* 定义两个子进程结构体，分别管理 app 和 ota 进程。 */
static SubProcess subprocess[2];
/* 守护进程运行状态标记：1 表示运行中，0 表示停止。 */
static int is_running = 1;
/* 子进程崩溃计数，用于监控异常退出频率。 */
static int crash_count = 0;
/* 上次崩溃的时间戳，用于重置崩溃计数。 */
static time_t last_crash_time = 0;

/* 崩溃计数重置时间间隔（秒）：5 分钟内无崩溃则重置计数。 */
#define CRASH_COUNT_RESET_INTERVAL 300

/* 信号处理函数：收到终止信号时触发守护进程优雅退出。 */
void daemon_runner_close(int sig)
{
    (void)sig;
    is_running = 0;
}

/* 守护进程核心运行函数。 */
int daemon_runner_run()
{
    /* 将当前进程创建为守护进程。 */
    if (daemon(0, 1) < 0)
    {
        log_error("daemon error");  /* 日志记录守护进程创建失败。 */
        exit(EXIT_FAILURE);         /* 退出进程，返回失败状态。 */
    }

    /* 关闭标准输入、标准输出、标准错误。 */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    /* 重定向标准输入到 /dev/null。 */
    open("/dev/null", O_RDWR);
    /* 重定向标准输出到日志文件。 */
    open(LOG_FILE, O_RDWR | O_CREAT, 0644);
    /* 重定向标准错误到日志文件。 */
    open(LOG_FILE, O_RDWR | O_CREAT, 0644);

    /* 初始化第一个子进程（app 进程）。 */
    if (daemon_process_init(&subprocess[0], "app") != 0) {
        log_error("Failed to init app subprocess");  /* 日志记录初始化失败。 */
        return -1;                                   /* 返回初始化失败。 */
    }
    /* 初始化第二个子进程（ota 进程）。 */
    if (daemon_process_init(&subprocess[1], "ota") != 0) {
        log_error("Failed to init ota subprocess");  /* 日志记录初始化失败。 */
        daemon_process_free(&subprocess[0]);         /* 释放已初始化的 app 进程资源。 */
        return -1;                                   /* 返回初始化失败。 */
    }
    
    /* 注册 SIGTERM 信号处理函数。 */
    signal(SIGTERM, daemon_runner_close);
    // 启动app子进程
    daemon_process_start(&subprocess[0]);
    // 启动ota子进程
    daemon_process_start(&subprocess[1]);

    // 守护进程主循环：持续监控子进程状态
    while (is_running)
    {
        int status = 0;  // 存储子进程退出状态
        // 非阻塞等待任意子进程退出（WNOHANG：无退出进程则立即返回0）
        pid_t pid = waitpid(-1, &status, WNOHANG);
        
        // 子进程回收调用失败
        if (pid < 0)
        {
            // 被信号中断则跳过本次循环，继续监控
            if (errno == EINTR) continue;
            // 记录waitpid错误日志（非中断类错误）
            log_warn("waitpid error: %s", strerror(errno));
            usleep(100000);  // 休眠100ms，避免高频循环占用CPU
            continue;
        }

        // 无子进程退出，休眠100ms后继续监控
        if (pid == 0)
        {
            usleep(100000);
            continue;
        }

        // 获取当前时间戳
        time_t now = time(NULL);
        // 检查是否超过重置时间间隔：上次崩溃超过5分钟则重置崩溃计数
        if (last_crash_time > 0 && 
            (now - last_crash_time) > CRASH_COUNT_RESET_INTERVAL) {
            crash_count = 0;
        }
        
        // 检查子进程退出状态
        // 判断是否正常退出并获取退出返回码
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            log_info("Process %d exited normally", pid);  // 记录正常退出日志
            // 正常退出不计入崩溃计数
        } else {
            // 异常退出（崩溃、信号终止等），增加崩溃计数
            crash_count++;
            last_crash_time = now;  // 更新上次崩溃时间
        }
        
        // 崩溃计数超过10次（异常退出频率过高）
        if (crash_count > 10)
        {
            log_error("Crash count exceeded: %d", crash_count);  // 记录严重错误日志
            // 创建/打开错误记录文件，写入崩溃计数
            int fd = open("/tmp/gateway.error", O_RDWR | O_CREAT, 0644);
            if (fd >= 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "crash_count=%d\n", crash_count);
                ssize_t written = write(fd, buf, strlen(buf));  // 写入崩溃计数
                if (written < 0) {
                    log_warn("Failed to write crash marker: %s", strerror(errno));
                }
                close(fd);                    // 关闭文件描述符
            }
        }

        // 遍历子进程列表，找到退出的进程并重启
        for (int i = 0; i < 2; i++)
        {
            // 匹配退出进程的PID
            if (pid == subprocess[i].pid)
            {
                // 记录子进程退出日志，准备重启
                log_warn("Subprocess %s (pid=%d) exited, restarting...", 
                         subprocess[i].name, pid);
                subprocess[i].pid = -1;  // 重置PID标记为未运行
                daemon_process_start(&subprocess[i]);  // 重启子进程
                break;  // 找到目标进程，退出循环
            }
        }
    }

    // 守护进程退出时，停止并释放所有子进程资源
    for (int i = 0; i < 2; i++)
    {
        daemon_process_stop(&subprocess[i]);   // 停止子进程
        daemon_process_free(&subprocess[i]);   // 释放子进程资源
    }

    return 0;  // 守护进程正常退出
}
