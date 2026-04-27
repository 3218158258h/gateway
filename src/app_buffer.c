/**
 * @file app_buffer.c
 * @brief 环形缓冲区实现，提供线程安全的数据缓存
 *
 * 功能说明：
 * - 环形缓冲区（Circular Buffer）实现
 * - 线程安全（互斥锁保护）
 * - 支持读写、窥探操作
 * - 自动处理缓冲区回绕
 */

#include "../include/app_buffer.h"
#include <string.h>
#include <stdlib.h>
#include "../thirdparty/log.c/log.h"

/* 从环形缓冲区指定起点复制数据（支持回绕）。 */
static void app_buffer_copy_from_ring(const Buffer *buffer, int start, void *buf, int len)
{
    unsigned char *out = (unsigned char *)buf;
    if (start + len <= buffer->size)
    {
        memcpy(out, buffer->ptr + start, len);
        return;
    }

    int first_len = buffer->size - start;
    memcpy(out, buffer->ptr + start, first_len);
    memcpy(out + first_len, buffer->ptr, len - first_len);
}

/**
 * @brief 初始化环形缓冲区
 * 
 * 分配指定大小的内存并初始化互斥锁。
 * 
 * @param buffer 缓冲区结构体指针
 * @param size 缓冲区大小（字节）
 * @return 0成功，-1失败
 */
int app_buffer_init(Buffer *buffer, int size)
{
    if (!buffer) {
        log_warn("Buffer pointer is NULL");
        return -1;
    }
    
    /* 分配缓冲区内存。 */
    buffer->ptr = malloc(size);
    if (!buffer->ptr)
    {
        log_warn("Not enough memory for buffer %p", buffer);
        return -1;
    }

    /* 初始化互斥锁。 */
    if (pthread_mutex_init(&buffer->lock, NULL) != 0) {
        log_warn("Failed to initialize mutex for buffer");
        free(buffer->ptr);
        buffer->ptr = NULL;
        return -1;
    }
    
    /* 初始化缓冲区状态。 */
    buffer->size = size;    /* 缓冲区总大小。 */
    buffer->start = 0;      /* 数据起始位置。 */
    buffer->len = 0;        /* 当前数据长度。 */
    
    return 0;
}

/**
 * @brief 从缓冲区读取数据
 * 
 * 从缓冲区读取指定长度的数据，并移动读指针。
 * 如果请求长度大于可用数据，则只读取可用数据。
 * 
 * @param buffer 缓冲区结构体指针
 * @param buf 输出数据缓冲区
 * @param len 请求读取的长度
 * @return 实际读取的长度，0表示无数据
 */
int app_buffer_read(Buffer *buffer, void *buf, int len)
{
    if (!buffer || !buf)
    {
        log_warn("Buffer or buf not valid");
        return -1;
    }

    pthread_mutex_lock(&buffer->lock);
    
    /* 限制读取长度不超过可用数据。 */
    if (len > buffer->len)
    {
        len = buffer->len;
    }
    
    /* 无数据可读。 */
    if (len == 0)
    {
        pthread_mutex_unlock(&buffer->lock);
        return 0;
    }

    app_buffer_copy_from_ring(buffer, buffer->start, buf, len);
    buffer->start = (buffer->start + len) % buffer->size;
    
    /* 更新数据长度。 */
    buffer->len -= len;

    pthread_mutex_unlock(&buffer->lock);
    
    return len;
}

/**
 * @brief 窥探缓冲区数据（不移动读指针）
 * 
 * 从缓冲区读取数据但不移动读指针，用于预览数据。
 * 
 * @param buffer 缓冲区结构体指针
 * @param buf 输出数据缓冲区
 * @param len 请求窥探的长度
 * @return 实际窥探的长度，0表示无数据
 */
int app_buffer_peek(Buffer *buffer, void *buf, int len)
{
    if (!buffer || !buf)
    {
        log_warn("Buffer or buf not valid");
        return -1;
    }

    pthread_mutex_lock(&buffer->lock);
    
    // 限制窥探长度不超过可用数据
    if (len > buffer->len)
    {
        len = buffer->len;
    }
    
    // 无数据可窥探
    if (len == 0)
    {
        pthread_mutex_unlock(&buffer->lock);
        return 0;
    }

    app_buffer_copy_from_ring(buffer, buffer->start, buf, len);
    
    // 注意：不更新start和len，保持原状态

    pthread_mutex_unlock(&buffer->lock);
    return len;
}

/**
 * @brief 向缓冲区写入数据
 * 
 * 将数据写入缓冲区的尾部，自动处理回绕。
 * 
 * @param buffer 缓冲区结构体指针
 * @param buf 输入数据缓冲区
 * @param len 写入数据长度
 * @return 0成功，-1缓冲区空间不足
 */
int app_buffer_write(Buffer *buffer, void *buf, int len)
{
    if (!buffer || !buf)
    {
        log_warn("Buffer or buf not valid");
        return -1;
    }
    
    pthread_mutex_lock(&buffer->lock);
    
    // 检查剩余空间是否足够
    if (len > buffer->size - buffer->len)
    {
        pthread_mutex_unlock(&buffer->lock);
        log_warn("Buffer %p is not enough", buffer);
        return -1;
    }

    // 计算写入位置（start + len）
    int write_offset = buffer->start + buffer->len;
    
    // 处理回绕
    if (write_offset >= buffer->size)
    {
        write_offset -= buffer->size;
    }

    // 处理两种情况：连续写入和回绕写入
    if (write_offset + len <= buffer->size)
    {
        // 情况1：写入位置到末尾有足够空间，无需回绕
        memcpy(buffer->ptr + write_offset, buf, len);
    }
    else
    {
        // 情况2：需要分两段写入（跨越缓冲区末尾）
        int first_len = buffer->size - write_offset;  // 第一段可写入长度
        memcpy(buffer->ptr + write_offset, buf, first_len);         // 写入第一段
        memcpy(buffer->ptr, buf + first_len, len - first_len);      // 写入第二段（从头开始）
    }
    
    // 更新数据长度
    buffer->len += len;
    
    pthread_mutex_unlock(&buffer->lock);
    
    return 0;
}

/**
 * @brief 关闭并释放缓冲区
 * 
 * 释放缓冲区内存和互斥锁资源。
 * 
 * @param buffer 缓冲区结构体指针
 */
void app_buffer_close(Buffer *buffer)
{
    if (!buffer) return;
    
    // 释放缓冲区内存
    if (buffer->ptr)
    {
        free(buffer->ptr);
        buffer->ptr = NULL;
    }

    // 销毁互斥锁
    pthread_mutex_destroy(&buffer->lock);

    // 重置状态
    buffer->size = 0;
    buffer->len = 0;
    buffer->start = 0;
}
