/**
 * @file app_runner.c
 * @brief 网关主运行模块 - 系统初始化与主循环
 * 
 * 功能说明：
 * - 系统启动初始化（线程池、持久化、设备、路由）
 * - 信号处理（优雅退出）
 * - 消息持久化回调处理
 * - 云端消息日志记录
 */

#include "../include/app_runner.h"
#include "../include/app_task.h"
#include "../include/app_serial.h"
#include "../include/app_bluetooth.h"
#include "../include/app_router.h"
#include "../include/app_persistence.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* 默认配置常量 */
#define DEFAULT_SERIAL_DEVICE "/dev/ttyUSB0"          // 默认串口设备路径
#define DEFAULT_CONFIG_FILE "/home/nvidia/gateway/gateway.ini"  // 默认配置文件路径
#define DEFAULT_DB_PATH "/home/nvidia/gateway/gateway.db"       // 默认数据库路径
#define MAX_SERIAL_DEVICES ROUTER_MAX_DEVICES
#define MAX_DEVICE_PATH_LEN 256

/* 全局静态变量 */
static SerialDevice devices[MAX_SERIAL_DEVICES];      // 串口设备实例数组
static RouterManager router;                          // 路由管理器实例
static PersistenceManager persistence;                // 消息持久化管理器实例
static volatile sig_atomic_t stop_requested = 0;      // 停止请求标志（原子变量）

/**
 * @brief 去除字符串首尾空白字符
 */
static char *trim_whitespace(char *str)
{
    if (!str) {
        return str;
    }
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    if (*str == '\0') {
        return str;
    }
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    return str;
}

/**
 * @brief 解析逗号分隔的串口设备列表
 */
static int parse_serial_device_list(char *list, char out_paths[][MAX_DEVICE_PATH_LEN], int max_count)
{
    if (!list || !out_paths || max_count <= 0) {
        return 0;
    }

    int count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(list, ",", &saveptr);
    while (token && count < max_count) {
        char *path = trim_whitespace(token);
        if (path[0] != '\0') {
            snprintf(out_paths[count], MAX_DEVICE_PATH_LEN, "%s", path);
            count++;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

/**
 * @brief 从配置文件读取串口设备列表
 */
static int load_device_config(char out_paths[][MAX_DEVICE_PATH_LEN], int *out_count)
{
    if (!out_paths || !out_count) {
        return -1;
    }

    snprintf(out_paths[0], MAX_DEVICE_PATH_LEN, "%s", DEFAULT_SERIAL_DEVICE);
    *out_count = 1;

    ConfigManager cfg_mgr;
    if (config_init(&cfg_mgr, DEFAULT_CONFIG_FILE) != 0 || config_load(&cfg_mgr) != 0) {
        log_warn("Failed to load device config, using default device: %s", DEFAULT_SERIAL_DEVICE);
        return 0;
    }

    int max_devices = config_get_int(&cfg_mgr, "device", "max_devices", ROUTER_MAX_DEVICES);
    if (max_devices <= 0) {
        max_devices = 1;
    }
    if (max_devices > ROUTER_MAX_DEVICES) {
        max_devices = ROUTER_MAX_DEVICES;
    }

    char serial_devices[CONFIG_MAX_VALUE_LEN];
    if (config_get_string(&cfg_mgr, "device", "serial_devices", "",
                          serial_devices, sizeof(serial_devices)) == 0 &&
        serial_devices[0] != '\0') {
        int parsed = parse_serial_device_list(serial_devices, out_paths, max_devices);
        if (parsed > 0) {
            *out_count = parsed;
            config_destroy(&cfg_mgr);
            return 0;
        }
    }

    char single_device[MAX_DEVICE_PATH_LEN];
    config_get_string(&cfg_mgr, "bluetooth", "device",
                      DEFAULT_SERIAL_DEVICE, single_device, sizeof(single_device));
    snprintf(out_paths[0], MAX_DEVICE_PATH_LEN, "%s",
             single_device[0] ? single_device : DEFAULT_SERIAL_DEVICE);
    *out_count = 1;

    config_destroy(&cfg_mgr);
    return 0;
}

/**
 * @brief 填充持久化默认配置
 */
static void load_default_persistence_config(PersistenceConfig *config)
{
    if (!config) {
        return;
    }
    snprintf(config->db_path, sizeof(config->db_path), "%s", DEFAULT_DB_PATH);
    config->max_retry_count = 3;
    config->message_expire_hours = 24;
    config->max_queue_size = 10000;
}

/**
 * @brief 信号处理函数
 * 
 * 当收到SIGINT（Ctrl+C）或SIGTERM信号时被调用，
 * 设置停止标志并通知线程池停止。
 * 
 * @param sig 信号值（未使用）
 */
static void app_runner_signal_handler(int sig)
{
    (void)sig;                                        // 避免未使用参数警告
    stop_requested = 1;                               // 设置停止标志
    app_task_signal_stop();                           // 通知线程池停止
}

/**
 * @brief 从配置文件加载持久化配置
 * 
 * 读取配置文件中的[persistence]节，获取数据库路径、
 * 最大重试次数、消息过期时间等配置项。
 * 如果配置文件加载失败，使用默认值。
 * 
 * @param config 输出配置结构体指针
 * @param config_file 配置文件路径
 * @return 0成功，-1失败
 */
static int load_persistence_config(PersistenceConfig *config, const char *config_file)
{
    ConfigManager cfg_mgr;
    
    // 初始化配置管理器
    if (config_init(&cfg_mgr, config_file) != 0) {
        log_warn("Failed to load config file, using defaults");
        load_default_persistence_config(config);
        return 0;
    }
    
    // 加载配置文件
    if (config_load(&cfg_mgr) != 0) {
        log_warn("Failed to load config file, using defaults");
        load_default_persistence_config(config);
        return 0;
    }
    
    // 读取数据库路径配置
    char db_path[256];
    if (config_get_string(&cfg_mgr, "persistence", "db_path", 
                  DEFAULT_DB_PATH, db_path, sizeof(db_path)) == 0) {
        snprintf(config->db_path, sizeof(config->db_path), "%s", db_path);
    } else {
        snprintf(config->db_path, sizeof(config->db_path), "%s", DEFAULT_DB_PATH);
    }
    
    // 读取最大重试次数
    config->max_retry_count = config_get_int(&cfg_mgr, "persistence", "max_retry", 3);
    
    // 读取消息过期时间（小时）
    config->message_expire_hours = config_get_int(&cfg_mgr, "persistence", "expire_hours", 24);
    
    // 设置最大队列大小
    config->max_queue_size = 10000;
    
    // 销毁配置管理器
    config_destroy(&cfg_mgr);
    return 0;
}

/**
 * @brief 设备消息回调 - 保存消息到持久化存储
 * 
 * 当设备发送消息时被路由管理器调用，
 * 将消息保存到SQLite数据库，支持离线重发。
 * 
 * @param router 路由管理器指针
 * @param device 设备指针
 * @param data 消息数据
 * @param len 消息长度
 */
static void on_device_message_persist(RouterManager *router, Device *device,
                                       const void *data, size_t len)
{
    (void)router;
    (void)device;
    // 保存消息到数据库
    uint64_t msg_id;
    if (persistence_save(&persistence, "GatewayData", data, len, 1, &msg_id) == 0) {
        log_debug("Message persisted: id=%llu", (unsigned long long)msg_id);
    }
}

/**
 * @brief 云端消息回调 - 记录收到的命令
 * 
 * 当从云端（MQTT/DDS）接收到命令消息时被路由管理器调用，
 * 记录命令日志，可用于调试和审计。
 * 
 * @param router 路由管理器指针
 * @param topic 消息主题
 * @param data 消息数据
 * @param len 消息长度
 */
static void on_cloud_message_log(RouterManager *router, const char *topic,
                                  const void *data, size_t len)
{
    (void)router;
    (void)data;
    log_info("Received command from cloud: topic=%s, len=%zu", topic, len);
}

/**
 * @brief 网关主运行函数
 * 
 * 系统启动入口，完成以下初始化：
 * 1. 注册信号处理函数
 * 2. 初始化线程池
 * 3. 初始化持久化模块
 * 4. 初始化串口设备
 * 5. 配置蓝牙连接类型
 * 6. 初始化路由管理器
 * 7. 注册设备到路由管理器
 * 8. 启动路由管理器
 * 9. 进入主循环等待
 * 
 * @return 0正常退出，-1初始化失败
 */
int app_runner_run()
{
    // 注册信号处理函数（SIGINT: Ctrl+C, SIGTERM: kill命令）
    signal(SIGINT, app_runner_signal_handler);
    signal(SIGTERM, app_runner_signal_handler);

    // 初始化线程池（5个工作线程）
    if (app_task_init(5) != 0) {
        return -1;
    }

    // 初始化持久化模块
    PersistenceConfig persist_config;
    load_persistence_config(&persist_config, DEFAULT_CONFIG_FILE);
    
    if (persistence_init(&persistence, &persist_config) != 0) {
        log_warn("Failed to init persistence, continuing without persistence");
    } else {
        log_info("Persistence initialized: %s", persist_config.db_path);
        // 清理过期消息
        int cleaned = persistence_cleanup_expired(&persistence);
        if (cleaned > 0) {
            log_info("Cleaned %d expired messages", cleaned);
        }
    }

    // 从配置加载设备列表并初始化串口设备
    char device_paths[MAX_SERIAL_DEVICES][MAX_DEVICE_PATH_LEN];
    int configured_device_count = 0;
    load_device_config(device_paths, &configured_device_count);

    int initialized_device_count = 0;
    for (int i = 0; i < configured_device_count; i++) {
        if (app_serial_init(&devices[i], device_paths[i]) != 0) {
            log_error("Failed to init serial device: %s", device_paths[i]);
            for (int j = 0; j < initialized_device_count; j++) {
                app_device_close((Device *)&devices[j]);
            }
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            app_task_close();
            return -1;
        }

        if (app_bluetooth_setConnectionType(&devices[i]) != 0) {
            log_error("Failed to config bluetooth on device: %s", device_paths[i]);
            app_device_close((Device *)&devices[i]);
            for (int j = 0; j < initialized_device_count; j++) {
                app_device_close((Device *)&devices[j]);
            }
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            app_task_close();
            return -1;
        }

        initialized_device_count++;
    }

    // 初始化路由管理器
    if (app_router_init(&router, NULL) != 0) {
        for (int i = 0; i < initialized_device_count; i++) {
            app_device_close((Device *)&devices[i]);
        }
        if (persistence.is_initialized) {
            persistence_close(&persistence);
        }
        app_task_close();
        return -1;
    }

    // 注册串口设备到路由管理器
    for (int i = 0; i < initialized_device_count; i++) {
        if (app_router_register_device(&router, (Device *)&devices[i]) != 0) {
            for (int j = i; j < initialized_device_count; j++) {
                app_device_close((Device *)&devices[j]);
            }
            app_router_close(&router);
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            app_task_close();
            return -1;
        }
    }

    // 注册消息回调函数
    if (persistence.is_initialized) {
        app_router_on_device_message(&router, on_device_message_persist);  // 设备消息持久化回调
        app_router_on_cloud_message(&router, on_cloud_message_log);        // 云端消息日志回调
    }

    // 启动路由管理器（连接云端、启动设备）
    if (app_router_start(&router) != 0) {
        app_router_close(&router);
        if (persistence.is_initialized) {
            persistence_close(&persistence);
        }
        app_task_close();
        return -1;
    }

    // 进入主循环，等待停止信号
    app_task_wait();

    // 停止路由管理器
    app_router_stop(&router);
    app_router_close(&router);

    // 清理已发送的消息
    if (persistence.is_initialized) {
        persistence_cleanup_sent(&persistence);
        persistence_close(&persistence);
    }
    
    // 关闭线程池
    app_task_close();

    return 0;
}
