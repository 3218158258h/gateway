#if !defined(__APP_TASK_H__)
#define __APP_TASK_H__

/* 线程任务函数类型。 */
typedef void (*Task)(void *);

/* 初始化任务执行器池；executors 为工作线程数量。 */
int app_task_init(int executors);

/* 提交异步任务到线程池。 */
int app_task_register(Task task, void *args);

/* 发送全局停止信号，通知任务系统进入收敛阶段。 */
void app_task_signal_stop(void);

/* 阻塞等待任务系统结束（通常由 app_runner 主循环调用）。 */
void app_task_wait();

/* 关闭线程池并释放任务系统资源。 */
void app_task_close();

#endif /* __APP_TASK_H__ */
