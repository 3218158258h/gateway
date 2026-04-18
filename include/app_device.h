#if !defined(__APP_DEVICE_H__)
#define __APP_DEVICE_H__
#include "app_buffer.h"
#include "app_task.h"
#include "app_message.h"
#include <sys/types.h>

struct VTable;

typedef struct DeviceStruct
{
    struct VTable *vptr;            // 虚函数表
    char *filename;                 // 设备文件名
    int fd;                         // 设备文件描述符
    pthread_t background_thread;    // 设备同步线程
    ConnectionType connection_type; // 连接类型
    Buffer *recv_buffer;            // 接收缓冲区
    Buffer *send_buffer;            // 发送缓冲区
    int is_running;                 // 设备是否正在运行
} Device;

struct VTable
{
    void *(*background_task)(void *);                      // 后台线程函数
    Task recv_task;                                        // 接收线程函数
    Task send_task;                                        // 发送线程函数
    int (*post_read)(Device *device, void *ptr, int *len); // 读后处理函数
    int (*pre_write)(Device *device, void *ptr, int *len); // 写前处理函数
    int (*recv_callback)(void *ptr, int len);// 接收回调函数
};

int app_device_init(Device *device, char *filename);

/**
 * @brief 启动设备后台线程
 *
 * @param device
 * @return int
 */
int app_device_start(Device *device);

/**
 * @brief 将二进制格式的数据写到设备发送缓冲区
 *
 * @param device
 * @param ptr
 * @param len
 * @return int
 */
int app_device_write(Device *device, void *ptr, int len);

// 这里需要一个注册回调函数的方法

void app_device_registerRecvCallback(Device *device, int (*recv_callback)(void *, int));

/**
 * @brief 停止设备后台线程
 *
 * @param device
 */
void app_device_stop(Device *device);

void app_device_close(Device *device);

#endif // __APP_DEVICE_H__
