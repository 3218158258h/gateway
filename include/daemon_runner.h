#if !defined(__DAEMON_RUNNER_H__)
#define __DAEMON_RUNNER_H__

/* 守护进程日志文件路径。 */
#define LOG_FILE "/var/log/gateway.log"

/* 启动守护进程。 */
int daemon_runner_run();

#endif /* __DAEMON_RUNNER_H__ */
