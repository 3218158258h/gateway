/**
 * @file app_device.c
 * @brief 设备抽象层实现，提供通用设备管理框架
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
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <errno.h>

/* 设备缓冲区大小（可通过配置覆盖）。 */
#define DEFAULT_BUFFER_LEN 16384
#define RECV_TASK_BUF_SIZE 1024
#define FRAME_HEADER_SIZE 3
#define DEVICE_BUFFER_COUNT 2
#define MAX_FRAME_PAYLOAD_LEN (RECV_TASK_BUF_SIZE - FRAME_HEADER_SIZE)
/* 保留一个帧头长度的余量；当缓冲区接近满且帧仍不完整时，丢弃一个字节推动解析前进。 */
#define RECV_STALLED_MARGIN FRAME_HEADER_SIZE
static int g_device_buffer_len = DEFAULT_BUFFER_LEN;

static int app_device_is_valid_connection_type(int connection_type)
{
    return connection_type == CONNECTION_TYPE_NONE ||
           connection_type == CONNECTION_TYPE_LORA ||
           connection_type == CONNECTION_TYPE_BLE_MESH;
}

static void app_device_set_state(Device *device, DeviceState state)
{
    if (!device) {
        return;
    }
    device->lifecycle_state = state;
}

DeviceState app_device_get_state(const Device *device)
{
    if (!device) {
        return DEVICE_STATE_UNINITIALIZED;
    }
    if (device->lifecycle_state < DEVICE_STATE_UNINITIALIZED ||
        device->lifecycle_state > DEVICE_STATE_ERROR) {
        return DEVICE_STATE_ERROR;
    }
    return (DeviceState)device->lifecycle_state;
}

const char *app_device_state_to_string(DeviceState state)
{
    switch (state) {
    case DEVICE_STATE_INITIALIZED:
        return "initialized";
    case DEVICE_STATE_CONFIGURING:
        return "configuring";
    case DEVICE_STATE_CONFIGURED:
        return "configured";
    case DEVICE_STATE_RUNNING:
        return "running";
    case DEVICE_STATE_STOPPED:
        return "stopped";
    case DEVICE_STATE_ERROR:
        return "error";
    case DEVICE_STATE_UNINITIALIZED:
    default:
        return "uninitialized";
    }
}

/**
 * @brief 关闭设备文件描述符并重置为-1
 */
static void app_device_close_fd(Device *device)
{
    if (device && device->fd >= 0) {
        close(device->fd);
        device->fd = -1;
    }
}

/**
 * @brief 释放设备缓冲区资源
 */
static void app_device_release_buffer(Buffer **buffer)
{
    if (buffer && *buffer) {
        app_buffer_close(*buffer);
        free(*buffer);
        *buffer = NULL;
    }
}

void app_device_set_buffer_size(int size)
{
    if (size > 0) {
        g_device_buffer_len = size;
    }
}

int app_device_get_buffer_size(void)
{
    return g_device_buffer_len;
}

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
    unsigned char buf[RECV_TASK_BUF_SIZE];
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
    unsigned char buf[RECV_TASK_BUF_SIZE];
    unsigned char header[FRAME_HEADER_SIZE];
    Device *device = argv;

    if (!device || !device->recv_buffer) {
        return;
    }

    // Method 1: drain all complete frames in this task invocation.
    while (device->recv_buffer->len >= FRAME_HEADER_SIZE) {
        // 先peek头部，不移除数据
        if (app_buffer_peek(device->recv_buffer, header, FRAME_HEADER_SIZE) < FRAME_HEADER_SIZE) {
            return;
        }

        int connection_type = header[0];
        int id_len = header[1];      // 设备标识长度
        int data_len = header[2];    // 数据长度
        int total_len = id_len + data_len;

        // Invalid type: discard one byte to avoid bad header blocking subsequent frames.
        if (!app_device_is_valid_connection_type(connection_type)) {
            app_buffer_read(device->recv_buffer, header, 1);
            log_warn("Discard invalid recv frame type: %d", connection_type);
            continue;
        }

        // Invalid length: discard header to avoid dead loop / overflow risk.
        if (total_len < 0 || total_len > MAX_FRAME_PAYLOAD_LEN) {
            app_buffer_read(device->recv_buffer, header, FRAME_HEADER_SIZE);
            log_warn("Discard invalid recv frame length: id_len=%d, data_len=%d", id_len, data_len);
            continue;
        }

        int required_len = FRAME_HEADER_SIZE + total_len;
        // Incomplete frame: wait for more bytes; near-capacity stall triggers one-byte discard.
        if (device->recv_buffer->len < required_len) {
            if (device->recv_buffer->size <= RECV_STALLED_MARGIN) {
                app_buffer_read(device->recv_buffer, header, 1);
                log_warn("Discard incomplete recv frame due to too-small buffer config: size=%d, need=%d",
                         device->recv_buffer->size, required_len);
            } else {
                int stall_threshold = device->recv_buffer->size - RECV_STALLED_MARGIN;
                if (device->recv_buffer->len > stall_threshold) {
                    app_buffer_read(device->recv_buffer, header, 1);
                    log_warn("Discard stalled incomplete recv frame to prevent blocking: buffered=%d, need=%d",
                             device->recv_buffer->len, required_len);
                }
            }
            return;
        }

        // Read full frame (header + payload).
        if (app_buffer_read(device->recv_buffer, buf, FRAME_HEADER_SIZE) != FRAME_HEADER_SIZE) {
            log_warn("Failed to read recv frame header from buffer");
            return;
        }
        if (total_len > 0 &&
            app_buffer_read(device->recv_buffer, buf + FRAME_HEADER_SIZE, total_len) != total_len) {
            log_warn("Failed to read recv frame payload from buffer: expected=%d", total_len);
            return;
        }

        int buf_len = FRAME_HEADER_SIZE + total_len;

        // Invoke receive callback (with retry).
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
    if (device->send_buffer->len < FRAME_HEADER_SIZE) {
        return;
    }
    
    // 先窥探头部，避免在数据不完整时破坏帧边界
    int peek_len = app_buffer_peek(device->send_buffer, buf, FRAME_HEADER_SIZE);
    if (peek_len != FRAME_HEADER_SIZE) {
        if (peek_len < 0) {
            log_warn("Failed to peek send buffer header (device send path)");
        } else {
            log_debug("Insufficient header bytes for send frame validation: got=%d, need=%d (will retry)",
                      peek_len, FRAME_HEADER_SIZE);
        }
        return;
    }
    
    int id_len = buf[1];
    int data_len = buf[2];
    int total_len = id_len + data_len;
    if (total_len < 0 || total_len > ((int)sizeof(buf) - FRAME_HEADER_SIZE)) {
        log_error("Invalid send frame length: id_len=%d, data_len=%d", id_len, data_len);
        app_buffer_read(device->send_buffer, buf, FRAME_HEADER_SIZE);
        return;
    }
    
    // 检查数据完整性
    if (device->send_buffer->len < FRAME_HEADER_SIZE + total_len) {
        log_warn("Incomplete send data");
        return;
    }

    // 读取完整消息（头部 + 负载）
    app_buffer_read(device->send_buffer, buf, FRAME_HEADER_SIZE);
    
    // 读取剩余负载（ID + 数据）
    app_buffer_read(device->send_buffer, buf + FRAME_HEADER_SIZE, total_len);
    buf_len = FRAME_HEADER_SIZE + total_len;

    // 调用协议层pre_write处理（如蓝牙帧封装）
    if (device->vptr->pre_write)
    {
        device->vptr->pre_write(device, buf, &buf_len);
    }
    
    // 写入设备文件描述符
    if (buf_len > 0)
    {
        ssize_t written = write(device->fd, buf, buf_len);
        if (written < 0) {
            log_error("Device write failed for %s: %s",
                      device->filename ? device->filename : "(unknown)",
                      strerror(errno));
        } else if (written != buf_len) {
            log_warn("Partial device write: %zd/%d", written, buf_len);
        }
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

    device->fd = -1;
    device->is_running = 0;
    device->vptr = NULL;
    device->recv_buffer = NULL;
    device->send_buffer = NULL;
    app_device_set_state(device, DEVICE_STATE_UNINITIALIZED);
    
    // 分配文件名内存
    size_t filename_len = strlen(filename) + 1;
    device->filename = malloc(filename_len);
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
    memcpy(device->filename, filename, filename_len);
    
    // 打开设备文件（读写模式，不作为控制终端）
    device->fd = open(device->filename, O_RDWR | O_NOCTTY);
    if (device->fd < 0)
    {
        log_warn("Device open failed: %s", filename);
        goto DEVICE_SEND_BUFFER_EXIT;
    }
    
    // 初始化连接类型
    device->connection_type = CONNECTION_TYPE_NONE;
    app_device_set_state(device, DEVICE_STATE_INITIALIZED);
    
    // 初始化接收缓冲区
    if (app_buffer_init(device->recv_buffer, g_device_buffer_len) < 0)
    {
        log_error("Device %s buffer creation failed: index=1/%d", device->filename, DEVICE_BUFFER_COUNT);
        goto DEVICE_OPEN_FAIL;
    }
    
    // 初始化发送缓冲区
    if (app_buffer_init(device->send_buffer, g_device_buffer_len) < 0)
    {
        log_error("Device %s buffer creation failed: index=2/%d", device->filename, DEVICE_BUFFER_COUNT);
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

    log_info("Device %s initialized (buffer_size=%d)", device->filename, g_device_buffer_len);

    return 0;

// 错误处理：逐层释放已分配的资源
DEVICE_RECV_INIT_FAIL:
    app_buffer_close(device->recv_buffer);
DEVICE_OPEN_FAIL:
    app_device_close_fd(device);
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

    device->is_running = 1;
    app_device_set_state(device, DEVICE_STATE_RUNNING);

    // 创建后台读取线程
    if (pthread_create(&device->background_thread, NULL, device->vptr->background_task, device) != 0)
    {
        device->is_running = 0;
        app_device_set_state(device, DEVICE_STATE_ERROR);
        log_error("Failed to create background thread");
        return -1;
    }

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
        app_device_set_state(device, DEVICE_STATE_ERROR);
        return -1;
    }
    
    // 注册发送任务到线程池
    if (app_task_register(device->vptr->send_task, device) < 0)
    {
        app_device_set_state(device, DEVICE_STATE_ERROR);
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
        app_device_set_state(device, DEVICE_STATE_STOPPED);
        
        // 关闭文件描述符（会触发read返回）
        app_device_close_fd(device);
        
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
    app_device_release_buffer(&device->send_buffer);
    
    // 释放接收缓冲区
    app_device_release_buffer(&device->recv_buffer);
    
    // 关闭文件描述符
    app_device_close_fd(device);
    
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
    app_device_set_state(device, DEVICE_STATE_UNINITIALIZED);
}
