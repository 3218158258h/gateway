#include "../include/daemon_process.h"
#include <stdlib.h>
#include "../thirdparty/log.c/log.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

/**
 * @brief 初始化子进程结构体
 * @param subprocess 子进程结构体指针，用于存储子进程信息
 * @param name 子进程名称（如"app"、"ota"）
 * @return 0-初始化成功，-1-初始化失败（参数无效/内存分配失败）
 * @note 主要完成：参数校验、结构体内存清空、分配参数/名称内存、初始化核心字段
 */
int daemon_process_init(SubProcess *subprocess, const char *name)
{
    // 参数合法性校验：子进程结构体指针或名称为空则返回失败
    if (!subprocess || !name) return -1;
    
    // 将子进程结构体内存清零，避免脏数据
    memset(subprocess, 0, sizeof(SubProcess));
    
    // 为子进程启动参数分配内存（3个字符串指针：程序名、子进程名、NULL结束符）
    subprocess->args = malloc(sizeof(char *) * 3);
    if (subprocess->args == NULL)
    {
        log_error("Failed to allocate memory for subprocess args");
        return -1;
    }
    // 将参数内存清零，确保结束符位置为NULL
    memset(subprocess->args, 0, sizeof(char *) * 3);
    
    // 为子进程名称分配内存（长度=名称长度+1，存储字符串结束符'\0'）
    subprocess->name = malloc(strlen(name) + 1);
    if (subprocess->name == NULL)
    {
        log_error("Failed to allocate memory for subprocess name");
        // 内存分配失败，释放已分配的参数内存，避免内存泄漏
        free(subprocess->args);
        subprocess->args = NULL;
        return -1;
    }

    // 将子进程名称拷贝到分配的内存中
    strcpy(subprocess->name, name);
    // 初始化PID为-1，表示子进程未运行
    subprocess->pid = -1;

    // 构造子进程启动参数：
    // args[0] = 可执行程序名（全局宏PROGRAM_NAME）
    // args[1] = 子进程名称（如"app"）
    // args[2] = NULL，符合execve函数参数格式要求
    subprocess->args[0] = PROGRAM_NAME;
    subprocess->args[1] = subprocess->name;
    subprocess->args[2] = NULL;
    return 0;
}

/**
 * @brief 启动子进程
 * @param subprocess 已初始化的子进程结构体指针
 * @return 0-启动成功，-1-启动失败（参数无效/fork失败）
 * @note 核心逻辑：通过fork创建子进程，子进程中调用execve执行目标程序
 */
int daemon_process_start(SubProcess *subprocess)
{
    // 参数合法性校验：子进程结构体指针为空则返回失败
    if (!subprocess) return -1;
    
    // 记录启动子进程的日志
    log_info("Starting subprocess %s", subprocess->name);
    // fork创建子进程：父进程返回子进程PID，子进程返回0，失败返回-1
    subprocess->pid = fork();
    if (subprocess->pid < 0)
    {
        log_error("Failed to fork subprocess %s", subprocess->name);
        return -1;
    }
    // 子进程执行分支
    if (subprocess->pid == 0)
    {
        // 替换子进程镜像，执行目标程序
        // 参数：程序路径、启动参数、环境变量
        if (execve(PROGRAM_NAME, subprocess->args, __environ) < 0)
        {
            log_error("Failed to execve subprocess %s", subprocess->name);
            exit(EXIT_FAILURE);  // 执行失败则退出子进程
        }
    }

    return 0;
}

/**
 * @brief 停止指定的子进程
 * @param subprocess 子进程结构体指针
 * @return 0-停止成功（或进程未运行），-1-停止失败（参数无效/发送信号失败/等待失败）
 * @note 核心逻辑：发送SIGTERM信号终止进程，等待进程退出并释放PID
 */
int daemon_process_stop(SubProcess *subprocess)
{
    // 参数合法性校验：子进程结构体指针为空则返回失败
    if (!subprocess) return -1;
    
    // 检查进程是否运行：PID<=0表示未运行，记录日志并返回成功
    if (subprocess->pid <= 0) {
        log_warn("Subprocess %s not running", 
                 subprocess->name ? subprocess->name : "unknown");
        return 0;
    }
    
    // 记录停止子进程的日志（包含PID）
    log_info("Stopping subprocess %s (pid=%d)", subprocess->name, subprocess->pid);
    
    // 向子进程发送SIGTERM信号，请求优雅退出
    if (kill(subprocess->pid, SIGTERM) < 0)
    {
        log_error("Failed to send SIGTERM to subprocess %s", subprocess->name);
        return -1;
    }
    
    // 阻塞等待子进程退出，获取退出状态
    int status;
    if (waitpid(subprocess->pid, &status, 0) < 0)
    {
        log_error("Failed to wait for subprocess %s", subprocess->name);
        return -1;
    }
    
    // 重置PID为-1，表示进程已停止
    subprocess->pid = -1;
    return 0;
}

/**
 * @brief 释放子进程结构体占用的内存资源
 * @param subprocess 子进程结构体指针
 * @note 主要释放：参数数组内存、名称字符串内存，重置PID为-1，避免内存泄漏
 */
void daemon_process_free(SubProcess *subprocess)
{
    // 参数合法性校验：子进程结构体指针为空则直接返回
    if (!subprocess) return;
    
    // 释放参数数组内存，并置空指针，避免野指针
    if (subprocess->args) {
        free(subprocess->args);
        subprocess->args = NULL;
    }
    // 释放名称字符串内存，并置空指针
    if (subprocess->name) {
        free(subprocess->name);
        subprocess->name = NULL;
    }
    // 重置PID为-1，标记进程未运行
    subprocess->pid = -1;
}