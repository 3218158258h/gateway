#if !defined(__APP_RUNNER_H__)
#define __APP_RUNNER_H__

/*
 * 网关应用入口：
 * - 加载总配置与运行时参数
 * - 初始化设备、路由、持久化与任务系统
 * - 进入主循环并处理退出信号
 */
int app_runner_run();

#endif /* __APP_RUNNER_H__ */
