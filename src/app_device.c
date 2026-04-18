/**
 * @file app_device.c
 * @brief 设备抽象层实现 - 通用设备管理框架
 * 
 * 功能说明：
 * - 设备初始化与资源管理
 * - 后台读取线程
 * - 接收/发送缓冲区管理
 * - 虚函数表实现多态
 * - 消息帧解析与回调
 */

#include "../include/app_device.h"
#include <stdlib.h>
#include "../thirdparty/log.c/log.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>

/* 缓冲区大小常量 */
#define BUFFER_LEN 16384  // 16KB

/**
 * @brief 设备后台读取任务
 * 
 * 在独立线程中运行，持续从设备文件描述符读取数据。
 * 读取流程：
 * 1. 从fd读取原始数据
 * 2. 调用post_read进行协议层处理
 * 3. 将处理后的数据写入接收缓冲区
 * 4. 注册任务到线程池进行回调处理
 * 
 * @param argv 设备结构体指针
 * @return NULL
 */
static void *app_device_backgroundTask(void *argv)
{
    unsigned char buf[1024];
    Device *device = argv;
    
    while (device->is_running)
    {
        // 从设备文件描述符读取数据
        int buf_len = read(device->fd, buf, 1024);
        if (buf_len < 0)
        {
            log_warn("read device data error");
            usleep(100000);  // 读取错误，等待100ms后重试
            continue;
        }
        
        // 无数据，等待10ms
        if (buf_len == 0) {
            usleep(10000);
            continue;
        }

        // 调用协议层post_read处理（如蓝牙帧解析）
        if (device->vptr->post_read)
        {
            device->vptr->post_read(device, buf, &buf_len);
        }

        // 将处理后的数据写入接收缓冲区
        if (buf_len > 0)
        {
            app_buffer_write(device->recv_buffer, buf, buf_len);
            // 注册接收任务到线程池
            app_task_register(device->vptr->recv_task, device);
        }
    }

    return NULL;
}

/**
 * @brief 默认接收任务处理函数
 * 
 * 从接收缓冲区解析完整消息帧，并调用回调函数。
 * 消息帧格式：[类型(1)][ID长度(1)][数据长度(1)][ID][数据]
 * 
 * @param argv 设备结构体指针
 */
static void app_device_defaultRecvTask(void *argv)
{
    unsigned char buf[1024];
    Device *device = argv;
    
    // 检查是否有足够数据读取头部
    if (device->recv_buffer->len < 3) {
        return;
    }
    
    // 先peek头部，不移除数据
    if (app_buffer_peek(device->recv_buffer, buf, 3) < 3) {
        return;
    }
    
    // 解析消息头
    int id_len = buf[1];      // ID长度
    int data_len = buf[2];    // 数据长度
    int total_len = id_len + data_len;
    
    // 检查完整消息是否到达
    if (device->recv_buffer->len < 3 + total_len) {
        return;  // 数据不完整，等待更多数据
    }
    
    // 读取头部（从缓冲区移除）
    app_buffer_read(device->recv_buffer, buf, 3);
    
    // 读取剩余数据（ID + 数据）
    app_buffer_read(device->recv_buffer, buf + 3, total_len);
    
    int buf_len = 3 + total_len;
    
    // 调用接收回调函数（带重试机制）
    if (device->vptr && device->vptr->recv_callback) {
        int retry = 0;
        while (device->vptr->recv_callback(buf, buf_len) < 0 && retry < 3)
        {
            usleep(100000);  // 回调失败，等待100ms后重试
            retry++;
        }
        if (retry >= 3) {
            log_error("Callback failed after 3 retries");
        }
    }
}

/**
 * @brief 默认发送任务处理函数
 * 
 * 从发送缓冲区读取数据，调用pre_write处理后写入设备。
 * 
 * @param argv 设备结构体指针
 */
static void app_device_defaultSendTask(void *argv)
{
    unsigned char buf[1024];
    int buf_len = 0;
    Device *device = argv;
    
    // 检查是否有足够数据读取头部
    if (device->send_buffer->len < 3) {
        return;
    }
    
    // 读取消息头
    app_buffer_read(device->send_buffer, buf, 3);
    
    int id_len = buf[1];
    int data_len = buf[2];
    
    // 检查数据完整性
    if (device->send_buffer->len < id_len + data_len) {
        log_warn("Incomplete send data");
        return;
    }
    
    // 读取完整消息
    app_buffer_read(device->send_buffer, buf + 3, id_len + data_len);
    buf_len = 3 + id_len + data_len;

    // 调用协议层pre_write处理（如蓝牙帧封装）
    if (device->vptr->pre_write)
    {
        device->vptr->pre_write(device, buf, &buf_len);
    }
    
    // 写入设备文件描述符
    if (buf_len > 0)
    {
        write(device->fd, buf, buf_len);
    }
}

/**
 * @brief 初始化设备
 * 
 * 分配资源、打开设备文件、初始化缓冲区和虚函数表。
 * 
 * @param device 设备结构体指针
 * @param filename 设备文件路径
 * @return 0成功，-1失败
 */
int app_device_init(Device *device, char *filename)
{
    if (!device || !filename) {
        log_warn("Invalid device or filename");
        return -1;
    }
    
    // 分配文件名内存
    device->filename = malloc(strlen(filename) + 1);
    if (!device->filename)
    {
        log_warn("Not enough memory for device %s", filename);
        goto DEVICE_EXIT;
    }
    
    // 分配虚函数表内存
    device->vptr = malloc(sizeof(struct VTable));
    if (!device->vptr)
    {
        log_warn("Not enough memory for device %s", filename);
        goto DEVICE_FILENAME_EXIT;
    }
    
    // 分配接收缓冲区内存
    device->recv_buffer = malloc(sizeof(Buffer));
    if (!device->recv_buffer)
    {
        log_warn("Not enough memory for device %s", filename);
        goto DEVICE_VTABLE_EXIT;
    }
    
    // 分配发送缓冲区内存
    device->send_buffer = malloc(sizeof(Buffer));
    if (!device->send_buffer)
    {
        log_warn("Not enough memory for device %s", filename);
        goto DEVICE_RECV_BUFFER_EXIT;
    }

    // 复制文件名
    strcpy(device->filename, filename);
    
    // 打开设备文件（读写模式，不作为控制终端）
    device->fd = open(device->filename, O_RDWR | O_NOCTTY);
    if (device->fd < 0)
    {
        log_warn("Device open failed: %s", filename);
        goto DEVICE_SEND_BUFFER_EXIT;
    }
    
    // 初始化连接类型
    device->connection_type = CONNECTION_TYPE_NONE;
    
    // 初始化接收缓冲区
    if (app_buffer_init(device->recv_buffer, BUFFER_LEN) < 0)
    {
        goto DEVICE_OPEN_FAIL;
    }
    
    // 初始化发送缓冲区
    if (app_buffer_init(device->send_buffer, BUFFER_LEN) < 0)
    {
        goto DEVICE_RECV_INIT_FAIL;
    }
    
    device->is_running = 0;

    // 设置默认虚函数
    device->vptr->background_task = app_device_backgroundTask;  // 后台读取任务
    device->vptr->recv_task = app_device_defaultRecvTask;       // 接收处理任务
    device->vptr->send_task = app_device_defaultSendTask;       // 发送处理任务
    device->vptr->pre_write = NULL;    // 写前处理（由子类实现）
    device->vptr->post_read = NULL;    // 读后处理（由子类实现）
    device->vptr->recv_callback = NULL; // 接收回调（由上层注册）

    log_info("Device %s initialized", device->filename);

    return 0;

// 错误处理：逐层释放已分配的资源
DEVICE_RECV_INIT_FAIL:
    app_buffer_close(device->recv_buffer);
DEVICE_OPEN_FAIL:
    close(device->fd);
DEVICE_SEND_BUFFER_EXIT:
    free(device->send_buffer);
DEVICE_RECV_BUFFER_EXIT:
    free(device->recv_buffer);
DEVICE_VTABLE_EXIT:
    free(device->vptr);
DEVICE_FILENAME_EXIT:
    free(device->filename);
DEVICE_EXIT:
    return -1;
}

/**
 * @brief 启动设备
 * 
 * 创建后台读取线程，开始接收数据。
 * 
 * @param device 设备结构体指针
 * @return 0成功，-1失败
 */
int app_device_start(Device *device)
{
    if (!device) return -1;
    
    // 检查是否已启动
    if (device->is_running)
    {
        return 0;
    }

    // 创建后台读取线程
    if (pthread_create(&device->background_thread, NULL, device->vptr->background_task, device) < 0)
    {
        log_error("Failed to create background thread");
        return -1;
    }
    
    device->is_running = 1;
    log_info("Device %s started", device->filename);
    return 0;
}

/**
 * @brief 向设备写入数据
 * 
 * 将数据写入发送缓冲区，并注册发送任务到线程池。
 * 
 * @param device 设备结构体指针
 * @param ptr 数据指针
 * @param len 数据长度
 * @return 0成功，-1失败
 */
int app_device_write(Device *device, void *ptr, int len)
{
    if (!device || !ptr || len <= 0) return -1;
    
    // 写入发送缓冲区
    if (app_buffer_write(device->send_buffer, ptr, len) < 0)
    {
        return -1;
    }
    
    // 注册发送任务到线程池
    if (app_task_register(device->vptr->send_task, device) < 0)
    {
        return -1;
    }
    return 0;
}

/**
 * @brief 注册接收回调函数
 * 
 * 设置数据接收完成后的回调函数。
 * 
 * @param device 设备结构体指针
 * @param recv_callback 回调函数指针
 */
void app_device_registerRecvCallback(Device *device, int (*recv_callback)(void *, int))
{
    if (device && device->vptr) {
        device->vptr->recv_callback = recv_callback;
    }
}

/**
 * @brief 停止设备
 * 
 * 停止后台线程，关闭文件描述符。
 * 使用超时等待线程退出，超时后强制取消。
 * 
 * @param device 设备结构体指针
 */
void app_device_stop(Device *device)
{
    if (!device) return;
    
    if (device->is_running)
    {
        device->is_running = 0;
        
        // 关闭文件描述符（会触发read返回）
        if (device->fd >= 0) {
            close(device->fd);
            device->fd = -1;
        }
        
        // 设置2秒超时等待线程退出
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        
        // 超时等待线程退出
        if (pthread_timedjoin_np(device->background_thread, NULL, &ts) != 0) {
            // 超时后强制取消线程
            pthread_cancel(device->background_thread);
            pthread_join(device->background_thread, NULL);
        }
    }
}

/**
 * @brief 关闭设备
 * 
 * 停止设备并释放所有资源。
 * 
 * @param device 设备结构体指针
 */
void app_device_close(Device *device)
{
    if (!device) return;
    
    // 停止设备
    app_device_stop(device);
    
    // 释放发送缓冲区
    if (device->send_buffer) {
        app_buffer_close(device->send_buffer);
        free(device->send_buffer);
        device->send_buffer = NULL;
    }
    
    // 释放接收缓冲区
    if (device->recv_buffer) {
        app_buffer_close(device->recv_buffer);
        free(device->recv_buffer);
        device->recv_buffer = NULL;
    }
    
    // 关闭文件描述符
    if (device->fd >= 0) {
        close(device->fd);
        device->fd = -1;
    }
    
    // 释放虚函数表
    if (device->vptr) {
        free(device->vptr);
        device->vptr = NULL;
    }
    
    // 释放文件名
    if (device->filename) {
        free(device->filename);
        device->filename = NULL;
    }
}
