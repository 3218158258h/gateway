#if !defined(__APP_DEVICE_H__)
#define __APP_DEVICE_H__
#include "app_buffer.h"
#include "app_task.h"
#include "app_message.h"
#include <sys/types.h>

typedef enum
{
    /* 设备生命周期状态：按初始化、配置、运行、停止分阶段推进。 */
    DEVICE_STATE_UNINITIALIZED = 0,
    DEVICE_STATE_INITIALIZED,
    DEVICE_STATE_CONFIGURING,
    DEVICE_STATE_CONFIGURED,
    DEVICE_STATE_RUNNING,
    DEVICE_STATE_STOPPED,
    DEVICE_STATE_ERROR
} DeviceState;

struct VTable;

typedef struct DeviceStruct
{
    struct VTable *vptr;            /* 设备行为表：由具体实现填充读写和回调逻辑。 */
    char *filename;                 /* 设备文件名或节点路径。 */
    int fd;                         /* 打开的设备文件描述符。 */
    pthread_t background_thread;    /* 设备后台同步线程。 */
    ConnectionType connection_type; /* 当前设备使用的连接类型。 */
    Buffer *recv_buffer;            /* 接收缓冲区。 */
    Buffer *send_buffer;            /* 发送缓冲区。 */
    int is_running;                 /* 当前设备是否处于运行态。 */
    DeviceState lifecycle_state;    /* 设备生命周期状态。 */
} Device;

struct VTable
{
    void *(*background_task)(void *);                      /* 后台线程主函数。 */
    Task recv_task;                                        /* 接收任务。 */
    Task send_task;                                        /* 发送任务。 */
    int (*post_read)(Device *device, void *ptr, int *len); /* 读后处理：把原始字节转成协议数据。 */
    int (*pre_write)(Device *device, void *ptr, int *len); /* 写前处理：把业务数据转成设备命令。 */
    int (*recv_callback)(void *ptr, int len);              /* 接收回调。 */
};

int app_device_init(Device *device, char *filename);
void app_device_set_buffer_size(int size);
int app_device_get_buffer_size(void);

DeviceState app_device_get_state(const Device *device);
const char *app_device_state_to_string(DeviceState state);

/**
 * @brief 启动设备后台线程
 *
 * @param device 设备对象
 * @return int 0 成功，-1 失败
 */
int app_device_start(Device *device);

/**
 * @brief 将二进制数据写入设备发送缓冲区
 *
 * @param device 设备对象
 * @param ptr 待写入数据
 * @param len 数据长度
 * @return int 0 成功，-1 失败
 */
int app_device_write(Device *device, void *ptr, int len);

/* 注册设备接收回调，给上层转发拆帧后的消息。 */
void app_device_registerRecvCallback(Device *device, int (*recv_callback)(void *, int));

/**
 * @brief 停止设备后台线程
 *
 * @param device 设备对象
 */
void app_device_stop(Device *device);

/* 关闭设备并释放底层资源。 */
void app_device_close(Device *device);

#endif // __APP_DEVICE_H__
