/**
 * @file app_task.c
 * @brief 线程池任务调度模块 - 基于队列的任务分发
 * 
 * 功能说明：
 * - 基于链表队列的任务调度（替代管道实现）
 * - 支持多个工作线程并发执行任务
 * - 使用条件变量实现高效的任务等待/唤醒
 * - 线程安全的任务注册和执行
 * 
 * 改进说明：
 * - 原实现使用管道(pipe)，每次读写都有系统调用开销
 * - 改用队列+条件变量，减少系统调用，提高性能
 */

#include "../include/app_task.h"
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "../thirdparty/log.c/log.h"
#include <errno.h>
#include <signal.h>

/* 全局停止标志 */
static volatile sig_atomic_t task_should_stop = 0;

/* 任务结构体 */
struct TaskStruct
{
    Task task;                    // 任务函数指针
    void *argv;                   // 任务参数
    struct TaskStruct *next;      // 链表下一个节点指针
};

/* 任务队列结构体 */
typedef struct TaskQueue
{
    struct TaskStruct *head;      // 队列头指针
    struct TaskStruct *tail;      // 队列尾指针
} TaskQueue;

/* 全局静态变量 */
static pthread_t *executor_ptr = NULL;      // 工作线程数组
static int executors_count = 0;             // 工作线程数量
static TaskQueue task_queue = {NULL, NULL};     // 任务队列
static pthread_mutex_t queue_lock;          // 队列互斥锁
static pthread_cond_t queue_cond;           // 队列条件变量
static int is_initialized = 0;              // 初始化标志

/**
 * @brief 广播停止并唤醒所有等待线程
 */
static void app_task_broadcast_stop(void)
{
    pthread_mutex_lock(&queue_lock);
    pthread_cond_broadcast(&queue_cond);
    pthread_mutex_unlock(&queue_lock);
}

/**
 * @brief 清理已创建的执行线程
 */
static void app_task_cleanup_executors(int created_count)
{
    for (int i = 0; i < created_count; i++) {
        if (executor_ptr[i]) {
            pthread_cancel(executor_ptr[i]);
            pthread_join(executor_ptr[i], NULL);
        }
    }
}

/**
 * @brief 销毁任务管理器同步原语
 */
static void app_task_destroy_sync_primitives(void)
{
    pthread_mutex_destroy(&queue_lock);
    pthread_cond_destroy(&queue_cond);
}

/**
 * @brief 初始化任务队列
 * 
 * @param queue 队列指针
 */
static void task_queue_init(TaskQueue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
}

/**
 * @brief 向队列尾部添加任务
 * 
 * @param queue 队列指针
 * @param task 任务结构体指针
 * @return 0成功，-1失败
 */
static int task_queue_push(TaskQueue *queue, struct TaskStruct *task)
{
    if (!queue || !task) return -1;
    
    task->next = NULL;
    
    if (queue->tail == NULL) {
        // 队列为空，新任务作为头节点
        queue->head = task;
        queue->tail = task;
    } else {
        // 队列非空，添加到尾部
        queue->tail->next = task;
        queue->tail = task;
    }
    
    return 0;
}

/**
 * @brief 从队列头部取出任务
 * 
 * @param queue 队列指针
 * @return 任务结构体指针，队列为空时返回NULL
 */
static struct TaskStruct* task_queue_pop(TaskQueue *queue)
{
    if (!queue || queue->head == NULL) return NULL;
    
    struct TaskStruct *task = queue->head;
    queue->head = task->next;
    
    if (queue->head == NULL) {
        // 队列已空，更新尾指针
        queue->tail = NULL;
    }
    
    return task;
}

/**
 * @brief 检查队列是否为空
 * 
 * @param queue 队列指针
 * @return 1为空，0为非空
 */
static int task_queue_is_empty(TaskQueue *queue)
{
    return (queue == NULL || queue->head == NULL);
}

/**
 * @brief 清空队列并释放内存
 * 
 * @param queue 队列指针
 */
static void task_queue_clear(TaskQueue *queue)
{
    if (!queue) return;
    
    struct TaskStruct *current = queue->head;
    while (current != NULL) {
        struct TaskStruct *next = current->next;
        free(current);
        current = next;
    }
    
    queue->head = NULL;
    queue->tail = NULL;
}

/**
 * @brief 线程清理处理函数
 * 
 * 当线程被取消或退出时调用，用于资源清理。
 * 
 * @param arg 线程ID指针
 */
static void cleanup_handler(void *arg)
{
    log_debug("Executor %d cleaned up", *(int*)arg);
}

/**
 * @brief 工作线程执行函数
 * 
 * 从任务队列中循环获取任务并执行。
 * 使用条件变量等待新任务，避免忙等待。
 * 
 * @param argv 线程ID指针
 * @return NULL
 */
static void *app_task_executor(void *argv)
{
    int executor_id = *(int*)argv;
    free(argv);

    int id = executor_id;
    pthread_cleanup_push(cleanup_handler, &id);

    // 设置线程取消状态和类型
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1)
    {
        struct TaskStruct *task_struct = NULL;

        // 加锁并等待任务
        pthread_mutex_lock(&queue_lock);
        
        // 等待队列非空或停止信号
        while (task_queue_is_empty(&task_queue) && !task_should_stop) {
            pthread_cond_wait(&queue_cond, &queue_lock);
        }
        
        // 检查停止信号
        if (task_should_stop) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }
        
        // 从队列取出任务
        task_struct = task_queue_pop(&task_queue);
        pthread_mutex_unlock(&queue_lock);

        if (task_struct == NULL) {
            continue;
        }

        // 执行任务（禁用取消以保证任务完整性）
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        task_struct->task(task_struct->argv);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

        // 释放任务结构体内存
        free(task_struct);

        // 检查取消请求
        pthread_testcancel();
    }

    pthread_cleanup_pop(1);
    return NULL;
}

/**
 * @brief 初始化任务管理器
 * 
 * 创建指定数量的工作线程，初始化任务队列和同步原语。
 * 
 * @param executors 工作线程数量
 * @return 0成功，-1失败
 */
int app_task_init(int executors)
{
    // 初始化互斥锁
    if (pthread_mutex_init(&queue_lock, NULL) != 0) {
        log_error("pthread_mutex_init failed");
        return -1;
    }

    // 初始化条件变量
    if (pthread_cond_init(&queue_cond, NULL) != 0) {
        log_error("pthread_cond_init failed");
        pthread_mutex_destroy(&queue_lock);
        return -1;
    }

    // 检查是否已初始化
    if (is_initialized || executor_ptr != NULL || executors_count > 0) {
        log_error("Task manager already initialized");
        app_task_destroy_sync_primitives();
        return -1;
    }

    // 检查线程数量参数
    if (executors <= 0) {
        log_error("Invalid executors count: %d", executors);
        app_task_destroy_sync_primitives();
        return -1;
    }

    // 分配线程数组内存
    executors_count = executors;
    executor_ptr = malloc(executors_count * sizeof(pthread_t));
    if (!executor_ptr) {
        app_task_destroy_sync_primitives();
        executors_count = 0;
        return -1;
    }
    memset(executor_ptr, 0, executors_count * sizeof(pthread_t));

    // 初始化任务队列
    task_queue_init(&task_queue);

    // 创建工作线程
    int i;
    for (i = 0; i < executors_count; i++) {
        int *t = malloc(sizeof(int));
        if (!t) {
            goto THREAD_EXIT;
        }
        *t = i;
        
        if (pthread_create(executor_ptr + i, NULL, app_task_executor, (void *)t) != 0) {
            log_error("Failed to create executor %d", i);
            free(t);
            goto THREAD_EXIT;
        }
    }

    is_initialized = 1;
    task_should_stop = 0;
    log_info("Task manager init result: requested=%d, created=%d, missing=0",
             executors, executors_count);
    return 0;

THREAD_EXIT:
    int requested = executors_count;
    int created = i;
    app_task_cleanup_executors(created);

    // 清理资源
    free(executor_ptr);
    executor_ptr = NULL;
    app_task_destroy_sync_primitives();
    executors_count = 0;

    log_error("Task manager init failed: requested=%d, created=%d, missing=%d",
              requested, created, requested - created);
    return -1;
}

/**
 * @brief 注册任务到队列
 * 
 * 将任务添加到任务队列尾部，并通知工作线程。
 * 
 * @param task 任务函数指针
 * @param args 任务参数
 * @return 0成功，-1失败
 */
int app_task_register(Task task, void *args)
{
    if (!is_initialized) {
        log_error("Task manager not initialized");
        return -1;
    }

    if (task == NULL) {
        log_error("Task function is NULL");
        return -1;
    }

    // 分配任务结构体内存
    struct TaskStruct *task_struct = malloc(sizeof(struct TaskStruct));
    if (!task_struct) {
        log_error("Failed to allocate task struct");
        return -1;
    }

    task_struct->task = task;
    task_struct->argv = args;
    task_struct->next = NULL;

    // 加锁并将任务加入队列
    pthread_mutex_lock(&queue_lock);
    
    task_queue_push(&task_queue, task_struct);
    
    // 通知一个等待的工作线程
    pthread_cond_signal(&queue_cond);
    
    pthread_mutex_unlock(&queue_lock);

    return 0;
}

/**
 * @brief 发送停止信号
 * 
 * 设置停止标志，通知所有工作线程退出。
 */
void app_task_signal_stop(void)
{
    task_should_stop = 1;
    app_task_broadcast_stop();
}

/**
 * @brief 等待任务管理器停止
 * 
 * 阻塞等待停止信号，每100ms检查一次状态。
 */
void app_task_wait(void)
{
    while (!task_should_stop && is_initialized) {
        usleep(100000);  // 100ms
    }
}

/**
 * @brief 关闭任务管理器
 * 
 * 停止所有工作线程，释放资源。
 */
void app_task_close(void)
{
    log_info("Closing task manager...");

    if (!is_initialized || executor_ptr == NULL || executors_count == 0) {
        return;
    }

    // 发送停止信号
    app_task_signal_stop();

    // 等待所有工作线程退出
    for (int i = 0; i < executors_count; i++) {
        if (executor_ptr[i]) {
            pthread_cancel(executor_ptr[i]);
            pthread_join(executor_ptr[i], NULL);
            executor_ptr[i] = 0;
        }
    }

    // 释放线程数组
    free(executor_ptr);
    executor_ptr = NULL;

    // 清空任务队列
    pthread_mutex_lock(&queue_lock);
    task_queue_clear(&task_queue);
    pthread_mutex_unlock(&queue_lock);

    // 销毁同步原语
    app_task_destroy_sync_primitives();

    executors_count = 0;
    is_initialized = 0;
    task_should_stop = 0;

    log_info("Task manager closed");
}
