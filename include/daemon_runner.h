#if !defined(__DAEMON_RUNNER_H__)
#define __DAEMON_RUNNER_H__

/* 启动守护进程主循环，负责拉起/监控/重启 app 与 ota 子进程。 */
int daemon_runner_run();

#endif /* __DAEMON_RUNNER_H__ */
