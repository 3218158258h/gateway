#if !defined(__APP_BUFFER_H__)
#define __APP_BUFFER_H__

#include <pthread.h>

typedef struct BufferStruct
{
    void *ptr;            // 缓存区指针
    int size;             // 缓存区总长度
    int start;            // 数据起始offset
    int len;              // 数据长度
    pthread_mutex_t lock; // Buffer锁
} Buffer;

/**
 * @brief 初始化Buffer
 * 
 * @param buffer buffer对象指针
 * @param size buffer长度
 * @return int 0成功 -1失败
 */
int app_buffer_init(Buffer *buffer, int size);

/**
 * @brief 从Buffer中读取数据
 * 
 * @param buffer buffer对象指针
 * @param buf 读出数据的缓存区指针
 * @param len 缓存区长度
 * @return int 成功返回实际读取的长度，-1失败
 */
int app_buffer_read(Buffer *buffer, void *buf, int len);


/**
 * @brief 查看缓冲区数据（不移除）
 * @param buffer 缓冲区指针
 * @param buf 输出缓冲区
 * @param len 要查看的长度
 * @return 实际查看的长度
 */
int app_buffer_peek(Buffer *buffer, void *buf, int len);


/**
 * @brief 向Buffer中写入数据
 * 
 * @param buffer buffer对象指针
 * @param buf 要写入的数据指针
 * @param len 要写入的长度
 * @return int 0成功 -1失败
 */
int app_buffer_write(Buffer *buffer, void *buf, int len);

/**
 * @brief 销毁Buffer
 * 
 * @param buffer buffer指针
 */
void app_buffer_close(Buffer *buffer);

#endif // __APP_BUFFER_H__
