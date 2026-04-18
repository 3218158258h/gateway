#if !defined(__DAEMON_PROCESS_H__)
#define __DAEMON_PROCESS_H__

#include <sys/types.h>

#define PROGRAM_NAME "/home/root/gateway/gateway"

typedef struct SubProcessStruct
{
    pid_t pid;
    char *name;
    char **args;
} SubProcess;

int daemon_process_init(SubProcess *subprocess, const char *name);

int daemon_process_start(SubProcess *subprocess);

int daemon_process_stop(SubProcess *subprocess);

void daemon_process_free(SubProcess *subprocess);

#endif // __DAEMON_PROCESS_H__
