#if !defined(__DAEMON_PROCESS_H__)
#define __DAEMON_PROCESS_H__

#include <sys/types.h>

/* 守护进程实际启动的二进制路径。 */
#define PROGRAM_NAME "/home/root/gateway/gateway"

/* 子进程运行信息。 */
typedef struct SubProcessStruct
{
    pid_t pid;
    char *name;
    char **args;
} SubProcess;

/* 初始化子进程信息。 */
int daemon_process_init(SubProcess *subprocess, const char *name);

/* 启动子进程。 */
int daemon_process_start(SubProcess *subprocess);

/* 停止子进程。 */
int daemon_process_stop(SubProcess *subprocess);

/* 释放子进程资源。 */
void daemon_process_free(SubProcess *subprocess);

#endif /* __DAEMON_PROCESS_H__ */
