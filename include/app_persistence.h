/**
 * @file app_persistence.h
 * @brief 消息持久化系统，基于 SQLite 保存待发送消息
 *
 * 功能：
 * - 消息队列持久化存储
 * - 断点续传支持
 * - 消息状态管理
 * - 自动清理过期消息
 *
 * @author Gateway Team
 * @version 2.0
 */

#ifndef __APP_PERSISTENCE_H__
#define __APP_PERSISTENCE_H__

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* 消息状态 */
typedef enum {
    MSG_STATUS_PENDING = 0,     /* 待发送 */
    MSG_STATUS_SENDING,         /* 发送中 */
    MSG_STATUS_SENT,            /* 已发送 */
    MSG_STATUS_FAILED           /* 发送失败 */
} MessageStatus;

/* 持久化消息 */
typedef struct PersistMessage {
    uint64_t id;                /* 消息ID */
    char topic[256];            /* 主题 */
    uint8_t *payload;           /* 消息内容 */
    size_t payload_len;         /* 消息长度 */
    int qos;                    /* QoS级别 */
    MessageStatus status;       /* 状态 */
    int retry_count;            /* 重试次数 */
    time_t create_time;         /* 创建时间 */
    time_t update_time;         /* 更新时间 */
} PersistMessage;

/* 持久化配置 */
typedef struct PersistenceConfig {
    char db_path[256];          /* 数据库文件路径 */
    int max_retry_count;        /* 最大重试次数 */
    int message_expire_hours;   /* 消息过期时间(小时) */
    int max_queue_size;         /* 最大队列大小 */
} PersistenceConfig;

/* 持久化管理器 */
typedef struct PersistenceManager {
    PersistenceConfig config;
    void *db;                   /* SQLite数据库指针 */
    int is_initialized;
    uint64_t next_message_id;
} PersistenceManager;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化持久化管理器
 * @param manager 管理器指针
 * @param config 配置
 * @return 0成功, -1失败
 */
int persistence_init(PersistenceManager *manager, const PersistenceConfig *config);

/**
 * @brief 关闭持久化管理器
 * @param manager 管理器指针
 */
void persistence_close(PersistenceManager *manager);

/* ========== 消息操作 ========== */

/**
 * @brief 保存消息到队列
 * @param manager 管理器指针
 * @param topic 主题
 * @param payload 消息内容
 * @param len 消息长度
 * @param qos QoS级别
 * @param out_id 输出消息ID(可为NULL)
 * @return 0成功, -1失败
 */
int persistence_save(PersistenceManager *manager, const char *topic,
                     const void *payload, size_t len, int qos, uint64_t *out_id);

/**
 * @brief 获取下一条待发送消息
 * @param manager 管理器指针
 * @param out_message 输出消息
 * @return 0成功, -1无消息
 */
int persistence_get_next(PersistenceManager *manager, PersistMessage *out_message);

/**
 * @brief 更新消息状态
 * @param manager 管理器指针
 * @param id 消息ID
 * @param status 新状态
 * @return 0成功, -1失败
 */
int persistence_update_status(PersistenceManager *manager, uint64_t id, 
                              MessageStatus status);

/**
 * @brief 标记消息为已发送
 * @param manager 管理器指针
 * @param id 消息ID
 * @return 0成功, -1失败
 */
int persistence_mark_sent(PersistenceManager *manager, uint64_t id);

/**
 * @brief 标记消息发送失败(增加重试计数)
 * @param manager 管理器指针
 * @param id 消息ID
 * @return 0成功, -1失败(超过最大重试次数)
 */
int persistence_mark_failed(PersistenceManager *manager, uint64_t id);

/**
 * @brief 删除消息
 * @param manager 管理器指针
 * @param id 消息ID
 * @return 0成功, -1失败
 */
int persistence_delete(PersistenceManager *manager, uint64_t id);

/* ========== 查询操作 ========== */

/**
 * @brief 获取待发送消息数量
 * @param manager 管理器指针
 * @return 消息数量
 */
int persistence_get_pending_count(PersistenceManager *manager);

/**
 * @brief 获取所有待发送消息
 * @param manager 管理器指针
 * @param messages 消息数组
 * @param max_count 最大数量
 * @return 实际数量
 */
int persistence_get_all_pending(PersistenceManager *manager, 
                                PersistMessage *messages, int max_count);

/* ========== 维护操作 ========== */

/**
 * @brief 清理过期消息
 * @param manager 管理器指针
 * @return 清理的消息数量
 */
int persistence_cleanup_expired(PersistenceManager *manager);

/**
 * @brief 清理已发送消息
 * @param manager 管理器指针
 * @return 清理的消息数量
 */
int persistence_cleanup_sent(PersistenceManager *manager);

/**
 * @brief 清空所有消息
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int persistence_clear_all(PersistenceManager *manager);

/* ========== 资源释放 ========== */

/**
 * @brief 释放消息资源
 * @param message 消息指针
 */
void persistence_free_message(PersistMessage *message);

#endif /* __APP_PERSISTENCE_H__ */
