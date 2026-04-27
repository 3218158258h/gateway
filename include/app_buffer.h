#if !defined(__APP_BUFFER_H__)
#define __APP_BUFFER_H__

#include <pthread.h>

typedef struct BufferStruct
{
    void *ptr;            /* 缓冲区指针。 */
    int size;             /* 缓冲区总长度。 */
    int start;            /* 数据起始偏移。 */
    int len;              /* 当前数据长度。 */
    pthread_mutex_t lock;  /* 缓冲区锁。 */
} Buffer;

/**
 * @brief 初始化缓冲区
 * 
 * @param buffer 缓冲区对象指针
 * @param size 缓冲区长度
 * @return int 0 成功，-1 失败
 */
int app_buffer_init(Buffer *buffer, int size);

/**
 * @brief 从缓冲区读取数据
 * 
 * @param buffer 缓冲区对象指针
 * @param buf 读出数据的缓存区指针
 * @param len 缓存区长度
 * @return int 成功返回实际读取的长度，-1 失败
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
 * @brief 向缓冲区写入数据
 * 
 * @param buffer 缓冲区对象指针
 * @param buf 要写入的数据指针
 * @param len 要写入的长度
 * @return int 0 成功，-1 失败
 */
int app_buffer_write(Buffer *buffer, void *buf, int len);

/**
 * @brief 销毁缓冲区
 * 
 * @param buffer 缓冲区指针
 */
void app_buffer_close(Buffer *buffer);

#endif // __APP_BUFFER_H__
