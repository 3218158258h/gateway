/**
 * @file app_persistence.c
 * @brief 消息持久化模块实现 -基于SQLite的消息存储
 * 
 * 功能说明：
 * - 消息持久化存储（支持离线重发）
 * - 消息状态管理（待发送/发送中/已发送/失败）
 * - 过期消息自动清理
 * - 支持WAL模式提升并发性能
 */

#include "../include/app_persistence.h"
#include "../thirdparty/log.c/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

/* 数据库建表SQL语句 */
static const char *SQL_CREATE_TABLE = 
    "CREATE TABLE IF NOT EXISTS messages ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"     // 消息ID（自增主键）
    "topic TEXT NOT NULL,"                       // 消息主题
    "payload BLOB NOT NULL,"                     // 消息内容（二进制）
    "payload_len INTEGER NOT NULL,"              // 消息长度
    "qos INTEGER DEFAULT 0,"                     // QoS级别
    "status INTEGER DEFAULT 0,"                  // 消息状态
    "retry_count INTEGER DEFAULT 0,"             // 重试次数
    "create_time INTEGER NOT NULL,"              // 创建时间
    "update_time INTEGER NOT NULL"               // 更新时间
    ");";

/* 创建索引SQL语句（优化状态查询） */
static const char *SQL_CREATE_INDEX = 
    "CREATE INDEX IF NOT EXISTS idx_status ON messages(status);";

/**
 * @brief 初始化持久化管理器
 * 
 * 打开SQLite数据库，创建消息表和索引，配置WAL模式。
 * 
 * @param manager 持久化管理器指针
 * @param config 配置参数，NULL使用默认配置
 * @return 0成功，-1失败
 */
int persistence_init(PersistenceManager *manager, const PersistenceConfig *config)
{
    if (!manager) return -1;
    
    // 清零管理器结构体
    memset(manager, 0, sizeof(PersistenceManager));
    
    // 加载配置参数
    if (config) {
        memcpy(&manager->config, config, sizeof(PersistenceConfig));
    } else {
        // 使用默认配置
        strcpy(manager->config.db_path, "/home/nvidia/gateway/gateway.db");
        manager->config.max_retry_count = 3;
        manager->config.message_expire_hours = 24;
        manager->config.max_queue_size = 10000;
    }
    
    sqlite3 *db;
    // 打开数据库文件
    int rc = sqlite3_open(manager->config.db_path, &db);
    if (rc != SQLITE_OK) {
        log_error("Failed to open database: %s", manager->config.db_path);
        return -1;
    }
    
    manager->db = db;
    
    // 创建消息表
    char *err_msg = NULL;
    rc = sqlite3_exec(db, SQL_CREATE_TABLE, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        log_error("Failed to create table: %s", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return -1;
    }
    
    // 创建状态索引
    rc = sqlite3_exec(db, SQL_CREATE_INDEX, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
    }
    
    // 开启WAL模式（提升并发性能）
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    // 设置正常同步模式（平衡性能与安全）
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
    
    manager->is_initialized = 1;
    
    log_info("Persistence initialized: %s", manager->config.db_path);
    return 0;
}

/**
 * @brief 使用默认配置初始化持久化管理器
 * 
 * @param manager 持久化管理器指针
 * @param db_path 数据库路径，NULL使用默认路径
 * @return 0成功，-1失败
 */
int persistence_init_default(PersistenceManager *manager, const char *db_path)
{
    PersistenceConfig config = {0};
    
    // 设置数据库路径
    if (db_path) {
        strncpy(config.db_path, db_path, sizeof(config.db_path) - 1);
    } else {
        strcpy(config.db_path, "/home/nvidia/gateway/gateway.db");
    }
    config.max_retry_count = 3;
    config.message_expire_hours = 24;
    config.max_queue_size = 10000;
    
    return persistence_init(manager, &config);
}

/**
 * @brief 关闭持久化管理器
 * 
 * 关闭数据库连接，释放资源。
 * 
 * @param manager 持久化管理器指针
 */
void persistence_close(PersistenceManager *manager)
{
    if (!manager) return;
    
    // 关闭数据库连接
    if (manager->db) {
        sqlite3_close((sqlite3 *)manager->db);
        manager->db = NULL;
    }
    
    manager->is_initialized = 0;
}

/**
 * @brief 保存消息到数据库
 * 
 * 将消息持久化存储，用于离线重发场景。
 * 
 * @param manager 持久化管理器指针
 * @param topic 消息主题
 * @param payload 消息内容
 * @param len 消息长度
 * @param qos QoS级别
 * @param out_id 输出消息ID
 * @return 0成功，-1失败
 */
int persistence_save(PersistenceManager *manager, const char *topic,
                     const void *payload, size_t len, int qos, uint64_t *out_id)
{
    if (!manager || !manager->is_initialized || !topic || !payload) {
        return -1;
    }
    
    sqlite3 *db = (sqlite3 *)manager->db;
    time_t now = time(NULL);
    
    // 插入消息SQL语句
    const char *sql = "INSERT INTO messages (topic, payload, payload_len, qos, "
                      "status, retry_count, create_time, update_time) "
                      "VALUES (?, ?, ?, ?, 0, 0, ?, ?);";
    
    // 预编译SQL语句
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        log_error("Failed to prepare statement: %s", sqlite3_errmsg(db));
        return -1;
    }
    
    // 绑定参数
    sqlite3_bind_text(stmt, 1, topic, -1, SQLITE_STATIC);           // 主题
    sqlite3_bind_blob(stmt, 2, payload, len, SQLITE_TRANSIENT);     // 内容
    sqlite3_bind_int(stmt, 3, len);                                  // 长度
    sqlite3_bind_int(stmt, 4, qos);                                  // QoS
    sqlite3_bind_int64(stmt, 5, now);                                // 创建时间
    sqlite3_bind_int64(stmt, 6, now);                                // 更新时间
    
    // 执行插入
    rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_DONE) {
        log_error("Failed to insert message: %s", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 获取自增ID
    uint64_t id = (uint64_t)sqlite3_last_insert_rowid(db);
    
    if (out_id) {
        *out_id = id;
    }
    
    sqlite3_finalize(stmt);
    
    log_debug("Message saved: id=%llu, topic=%s, len=%zu", 
              (unsigned long long)id, topic, len);
    return 0;
}

/**
 * @brief 获取下一条待发送消息
 * 
 * 从数据库中获取状态为待发送且重试次数未超限的消息。
 * 
 * @param manager 持久化管理器指针
 * @param out_message 输出消息结构体
 * @return 0成功，-1无消息或失败
 */
int persistence_get_next(PersistenceManager *manager, PersistMessage *out_message)
{
    if (!manager || !manager->is_initialized || !out_message) {
        return -1;
    }
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    // 查询待发送消息（按创建时间升序）
    const char *sql = "SELECT id, topic, payload, payload_len, qos, status, "
                      "retry_count, create_time, update_time "
                      "FROM messages WHERE status = 0 "
                      "AND retry_count < ? "
                      "ORDER BY create_time ASC LIMIT 1;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }
    
    // 绑定最大重试次数
    sqlite3_bind_int(stmt, 1, manager->config.max_retry_count);
    
    rc = sqlite3_step(stmt);
    
    // 没有待发送消息
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    
    // 清零输出结构体
    memset(out_message, 0, sizeof(PersistMessage));
    
    // 读取消息字段
    out_message->id = sqlite3_column_int64(stmt, 0);
    strncpy(out_message->topic, (const char *)sqlite3_column_text(stmt, 1), 
            sizeof(out_message->topic) - 1);
    
    // 读取消息内容（需要复制）
    const void *payload = sqlite3_column_blob(stmt, 2);
    int payload_len = sqlite3_column_int(stmt, 3);
    
    out_message->payload = malloc(payload_len);
    if (!out_message->payload) {
        log_error("Failed to allocate memory for payload");
        sqlite3_finalize(stmt);
        return -1;
    }
    else {
        memcpy(out_message->payload, payload, payload_len);
        out_message->payload_len = payload_len;
    }
    
    // 读取其他字段
    out_message->qos = sqlite3_column_int(stmt, 4);
    out_message->status = sqlite3_column_int(stmt, 5);
    out_message->retry_count = sqlite3_column_int(stmt, 6);
    out_message->create_time = sqlite3_column_int64(stmt, 7);
    out_message->update_time = sqlite3_column_int64(stmt, 8);
    
    sqlite3_finalize(stmt);
    
    // 更新状态为发送中
    persistence_update_status(manager, out_message->id, MSG_STATUS_SENDING);
    
    return 0;
}

/**
 * @brief 更新消息状态
 * 
 * @param manager 持久化管理器指针
 * @param id 消息ID
 * @param status 新状态
 * @return 0成功，-1失败
 */
int persistence_update_status(PersistenceManager *manager, uint64_t id, 
                              MessageStatus status)
{
    if (!manager || !manager->is_initialized) return -1;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    const char *sql = "UPDATE messages SET status = ?, update_time = ? WHERE id = ?;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }
    
    // 绑定参数
    sqlite3_bind_int(stmt, 1, status);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_int64(stmt, 3, id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/**
 * @brief 标记消息为已发送
 * 
 * @param manager 持久化管理器指针
 * @param id 消息ID
 * @return 0成功，-1失败
 */
int persistence_mark_sent(PersistenceManager *manager, uint64_t id)
{
    return persistence_update_status(manager, id, MSG_STATUS_SENT);
}

/**
 * @brief 标记消息发送失败
 * 
 * 增加重试计数，如果超过最大重试次数则标记为失败。
 * 
 * @param manager 持久化管理器指针
 * @param id 消息ID
 * @return 0可重试，-1已达最大重试次数
 */
int persistence_mark_failed(PersistenceManager *manager, uint64_t id)
{
    if (!manager || !manager->is_initialized) return -1;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    // 查询当前重试次数
    const char *check_sql = "SELECT retry_count FROM messages WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, check_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    
    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    
    int retry_count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    
    // 检查是否超过最大重试次数
    if (retry_count >= manager->config.max_retry_count) {
        persistence_update_status(manager, id, MSG_STATUS_FAILED);
        return -1;
    }
    
    // 重置状态为待发送，增加重试计数
    const char *update_sql = "UPDATE messages SET status = 0, retry_count = ?, "
                             "update_time = ? WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, retry_count + 1);
    sqlite3_bind_int64(stmt, 2, time(NULL));
    sqlite3_bind_int64(stmt, 3, id);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/**
 * @brief 删除消息
 * 
 * @param manager 持久化管理器指针
 * @param id 消息ID
 * @return 0成功，-1失败
 */
int persistence_delete(PersistenceManager *manager, uint64_t id)
{
    if (!manager || !manager->is_initialized) return -1;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    const char *sql = "DELETE FROM messages WHERE id = ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    
    sqlite3_bind_int64(stmt, 1, id);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/**
 * @brief 获取待发送消息数量
 * 
 * @param manager 持久化管理器指针
 * @return 待发送消息数量
 */
int persistence_get_pending_count(PersistenceManager *manager)
{
    if (!manager || !manager->is_initialized) return 0;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    // 统计待发送且可重试的消息数量
    const char *sql = "SELECT COUNT(*) FROM messages WHERE status = 0 "
                      "AND retry_count < ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return 0;
    
    sqlite3_bind_int(stmt, 1, manager->config.max_retry_count);
    
    rc = sqlite3_step(stmt);
    int count = 0;
    
    if (rc == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

/**
 * @brief 批量获取待发送消息
 * 
 * 获取多条待发送消息，用于批量重发场景。
 * 
 * @param manager 持久化管理器指针
 * @param messages 输出消息数组
 * @param max_count 最大获取数量
 * @return 实际获取的消息数量
 */
int persistence_get_all_pending(PersistenceManager *manager, 
                                PersistMessage *messages, int max_count)
{
    if (!manager || !manager->is_initialized || !messages || max_count <= 0) {
        return -1;
    }
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    // 批量查询待发送消息
    const char *sql = "SELECT id, topic, payload, payload_len, qos, status, "
                      "retry_count, create_time, update_time "
                      "FROM messages WHERE status = 0 "
                      "AND retry_count < ? "
                      "ORDER BY create_time ASC LIMIT ?;";
    
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }
    
    // 绑定参数
    sqlite3_bind_int(stmt, 1, manager->config.max_retry_count);
    sqlite3_bind_int(stmt, 2, max_count);
    
    int count = 0;
    // 遍历结果集
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW && count < max_count) {
        PersistMessage *msg = &messages[count];
        memset(msg, 0, sizeof(PersistMessage));
        
        // 读取消息字段
        msg->id = sqlite3_column_int64(stmt, 0);
        strncpy(msg->topic, (const char *)sqlite3_column_text(stmt, 1), 
                sizeof(msg->topic) - 1);
        
        // 复制消息内容
        const void *payload = sqlite3_column_blob(stmt, 2);
        int payload_len = sqlite3_column_int(stmt, 3);
        
        msg->payload = malloc(payload_len);
        if (msg->payload) {
            memcpy(msg->payload, payload, payload_len);
            msg->payload_len = payload_len;
        }
        else {
            log_error("Failed to allocate memory for message payload");
            break;
        }
        
        // 读取其他字段
        msg->qos = sqlite3_column_int(stmt, 4);
        msg->status = sqlite3_column_int(stmt, 5);
        msg->retry_count = sqlite3_column_int(stmt, 6);
        msg->create_time = sqlite3_column_int64(stmt, 7);
        msg->update_time = sqlite3_column_int64(stmt, 8);
        
        count++;
    }
    
    sqlite3_finalize(stmt);
    return count;
}

/**
 * @brief 清理过期消息
 * 
 * 删除创建时间超过配置的过期小时数的消息。
 * 
 * @param manager 持久化管理器指针
 * @return 清理的消息数量
 */
int persistence_cleanup_expired(PersistenceManager *manager)
{
    if (!manager || !manager->is_initialized) return 0;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    // 计算过期时间点
    time_t expire_time = time(NULL) - manager->config.message_expire_hours * 3600;
    
    const char *sql = "DELETE FROM messages WHERE create_time < ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    
    sqlite3_bind_int64(stmt, 1, expire_time);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);  // 获取删除行数
    sqlite3_finalize(stmt);
    
    log_debug("Cleaned up %d expired messages", changes);
    return changes;
}

/**
 * @brief 清理已发送消息
 * 
 * 删除状态为已发送的消息，释放存储空间。
 * 
 * @param manager 持久化管理器指针
 * @return 清理的消息数量
 */
int persistence_cleanup_sent(PersistenceManager *manager)
{
    if (!manager || !manager->is_initialized) return 0;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    const char *sql = "DELETE FROM messages WHERE status = ?;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return -1;
    
    sqlite3_bind_int(stmt, 1, MSG_STATUS_SENT);
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(db);
    sqlite3_finalize(stmt);
    
    log_debug("Cleaned up %d sent messages", changes);
    return changes;
}

/**
 * @brief 清空所有消息
 * 
 * 删除表中所有消息记录。
 * 
 * @param manager 持久化管理器指针
 * @return 0成功，-1失败
 */
int persistence_clear_all(PersistenceManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    sqlite3 *db = (sqlite3 *)manager->db;
    
    const char *sql = "DELETE FROM messages;";
    int rc = sqlite3_exec(db, sql, NULL, NULL, NULL);
    
    return (rc == SQLITE_OK) ? 0 : -1;
}

/**
 * @brief 释放消息结构体内存
 * 
 * 释放消息中动态分配的payload内存。
 * 
 * @param message 消息结构体指针
 */
void persistence_free_message(PersistMessage *message)
{
    if (message && message->payload) {
        free(message->payload);
        message->payload = NULL;
        message->payload_len = 0;
    }
}
