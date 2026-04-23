/**
 * @file app_runner.c
 * @brief 网关主运行模块，负责系统初始化与主循环
 *
 * 功能说明：
 * - 系统启动初始化（线程池、持久化、设备、路由）
 * - 信号处理（优雅退出）
 * - 消息持久化回调处理
 * - 云端消息日志记录
 */

#include "../include/app_runner.h"
#include "../include/app_task.h"
#include "../include/app_device_layer.h"
#include "../include/app_link_adapter.h"
#include "../include/app_protocol_config.h"
#include "../include/app_router.h"
#include "../include/app_persistence.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* 默认配置常量。 */
#define DEFAULT_DB_PATH "gateway.db"                             /* 默认数据库路径。 */
#define MAX_SERIAL_DEVICES ROUTER_MAX_DEVICES
#define MAX_DEVICE_PATH_LEN 256
#define DEFAULT_TASK_EXECUTORS 5
#define DEFAULT_PERSIST_QUEUE_SIZE 10000
#define DEFAULT_DEVICE_BUFFER_SIZE 16384
#define DEFAULT_DEVICE_INTERFACE "serial"
#define DEFAULT_DEVICE_PROTOCOL "ble_mesh_default"

typedef struct RuntimeConfig {
    int thread_pool_executors;
} RuntimeConfig;

/* 全局静态变量。 */
static SerialDevice devices[MAX_SERIAL_DEVICES];      /* 串口设备实例数组。 */
static RouterManager router;                          /* 路由管理器实例。 */
static PersistenceManager persistence;                /* 消息持久化管理器实例。 */
static volatile sig_atomic_t stop_requested = 0;      /* 停止请求标志（原子变量）。 */

/* 去除字符串首尾空白字符。 */
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

/* 解析逗号分隔的串口设备列表。 */
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

static int parse_string_list(char *list, char out_values[][APP_PROTOCOL_NAME_MAX_LEN], int max_count)
{
    if (!list || !out_values || max_count <= 0) {
        return 0;
    }

    int count = 0;
    char *saveptr = NULL;
    char *token = strtok_r(list, ",", &saveptr);
    while (token && count < max_count) {
        char *value = trim_whitespace(token);
        if (value[0] != '\0') {
            snprintf(out_values[count], APP_PROTOCOL_NAME_MAX_LEN, "%s", value);
            count++;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return count;
}

/**
 * @brief 从配置文件读取串口设备列表
 */
static int load_device_config(const ConfigManager *gateway_cfg,
                              char out_paths[][MAX_DEVICE_PATH_LEN],
                              char out_interfaces[][APP_INTERFACE_NAME_MAX_LEN],
                              char out_protocols[][APP_PROTOCOL_NAME_MAX_LEN],
                              int *out_count,
                              int *out_buffer_size)
{
    if (!gateway_cfg || !out_paths || !out_interfaces || !out_protocols || !out_count || !out_buffer_size) {
        return -1;
    }

    *out_buffer_size = config_get_int((ConfigManager *)gateway_cfg, "device", "buffer_size", DEFAULT_DEVICE_BUFFER_SIZE);
    if (*out_buffer_size <= 0) {
        log_error("Invalid config [device].buffer_size: %d", *out_buffer_size);
        return -1;
    }

    int max_devices = config_get_int((ConfigManager *)gateway_cfg, "device", "max_devices", ROUTER_MAX_DEVICES);
    if (max_devices <= 0) {
        log_error("Invalid config [device].max_devices: %d", max_devices);
        return -1;
    }
    if (max_devices > ROUTER_MAX_DEVICES) {
        max_devices = ROUTER_MAX_DEVICES;
    }

    char serial_devices[CONFIG_MAX_VALUE_LEN];
    if (config_get_string((ConfigManager *)gateway_cfg, "device", "serial_devices", "",
                          serial_devices, sizeof(serial_devices)) == 0 &&
        serial_devices[0] != '\0') {
        int parsed = parse_serial_device_list(serial_devices, out_paths, max_devices);
        if (parsed > 0) {
            *out_count = parsed;
        } else {
            log_error("Invalid [device].serial_devices: %s", serial_devices);
            return -1;
        }
    }

    if (*out_count == 0) {
        char single_device[MAX_DEVICE_PATH_LEN] = {0};
        if (!(config_get_string((ConfigManager *)gateway_cfg, "device", "single_device",
                                "", single_device, sizeof(single_device)) == 0 &&
              single_device[0] != '\0')) {
            single_device[0] = '\0';
        }

        if (single_device[0] != '\0') {
            char *trimmed = trim_whitespace(single_device);
            if (!trimmed || trimmed[0] == '\0') {
                log_error("Empty config for single device path");
                return -1;
            }
            snprintf(out_paths[0], MAX_DEVICE_PATH_LEN, "%s", trimmed);
            *out_count = 1;
        }
    }

    if (*out_count == 0) {
        log_error("Missing required config: [device].serial_devices or [device].single_device");
        return -1;
    }

    for (int i = 0; i < *out_count; i++) {
        snprintf(out_interfaces[i], APP_INTERFACE_NAME_MAX_LEN, "%s", DEFAULT_DEVICE_INTERFACE);
        snprintf(out_protocols[i], APP_PROTOCOL_NAME_MAX_LEN, "%s", DEFAULT_DEVICE_PROTOCOL);
    }

    char serial_interfaces[CONFIG_MAX_VALUE_LEN] = {0};
    if (config_get_string((ConfigManager *)gateway_cfg, "device", "serial_interfaces", "",
                          serial_interfaces, sizeof(serial_interfaces)) == 0 &&
        serial_interfaces[0] != '\0') {
        char interfaces_copy[CONFIG_MAX_VALUE_LEN] = {0};
        snprintf(interfaces_copy, sizeof(interfaces_copy), "%s", serial_interfaces);
        char values[ROUTER_MAX_DEVICES][APP_PROTOCOL_NAME_MAX_LEN] = {{0}};
        int parsed = parse_string_list(interfaces_copy, values, *out_count);
        for (int i = 0; i < parsed && i < *out_count; i++) {
            snprintf(out_interfaces[i], APP_INTERFACE_NAME_MAX_LEN, "%s", values[i]);
        }
    }

    char serial_protocols[CONFIG_MAX_VALUE_LEN] = {0};
    if (config_get_string((ConfigManager *)gateway_cfg, "device", "serial_protocols", "",
                          serial_protocols, sizeof(serial_protocols)) == 0 &&
        serial_protocols[0] != '\0') {
        char protocols_copy[CONFIG_MAX_VALUE_LEN] = {0};
        snprintf(protocols_copy, sizeof(protocols_copy), "%s", serial_protocols);
        char values[ROUTER_MAX_DEVICES][APP_PROTOCOL_NAME_MAX_LEN] = {{0}};
        int parsed = parse_string_list(protocols_copy, values, *out_count);
        for (int i = 0; i < parsed && i < *out_count; i++) {
            snprintf(out_protocols[i], APP_PROTOCOL_NAME_MAX_LEN, "%s", values[i]);
        }
    }

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
    config->max_queue_size = DEFAULT_PERSIST_QUEUE_SIZE;
}

static int load_runtime_config(const ConfigManager *gateway_cfg, RuntimeConfig *runtime)
{
    if (!gateway_cfg || !runtime) {
        return -1;
    }

    runtime->thread_pool_executors = DEFAULT_TASK_EXECUTORS;
    runtime->thread_pool_executors = config_get_int(
        (ConfigManager *)gateway_cfg, "runtime", "thread_pool_executors", DEFAULT_TASK_EXECUTORS);

    if (runtime->thread_pool_executors <= 0) {
        log_error("Invalid config [runtime].thread_pool_executors: %d",
                  runtime->thread_pool_executors);
        return -1;
    }
    log_info("Runtime config loaded: thread_pool_executors=%d",
             runtime->thread_pool_executors);
    return 0;
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
 * @brief 从总配置加载持久化配置
 * 
 * 读取 gateway.ini 中的 [persistence] 节，获取数据库路径、
 * 最大重试次数、消息过期时间等配置项。
 * 如果配置文件加载失败，使用默认值。
 * 
 * @param config 输出配置结构体指针
 * @return 0成功，-1失败
 */
static int load_persistence_config(const ConfigManager *gateway_cfg, PersistenceConfig *config)
{
    if (!gateway_cfg || !config) {
        return -1;
    }

    load_default_persistence_config(config);

    // 读取数据库路径配置
    config_get_string((ConfigManager *)gateway_cfg, "persistence", "db_path",
                      DEFAULT_DB_PATH, config->db_path, sizeof(config->db_path));
    
    // 读取最大重试次数
    config->max_retry_count = config_get_int((ConfigManager *)gateway_cfg, "persistence", "max_retry", 3);
    
    // 读取消息过期时间（小时）
    config->message_expire_hours = config_get_int((ConfigManager *)gateway_cfg, "persistence", "expire_hours", 24);
    
    // 设置最大队列大小
    config->max_queue_size = config_get_int((ConfigManager *)gateway_cfg, "persistence", "max_queue_size",
                                            DEFAULT_PERSIST_QUEUE_SIZE);
    if (config->max_queue_size <= 0) {
        log_error("Invalid config [persistence].max_queue_size: %d", config->max_queue_size);
        return -1;
    }
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
    (void)device;
    int qos = 1;

    if (router) {
        qos = router->transport.config.default_qos;
        if (qos < 0) {
            qos = 0;
        }
    }

    /* 保存消息到数据库，QoS 与传输层默认配置保持一致。 */
    uint64_t msg_id;
    if (persistence_save(&persistence, "GatewayData", data, len, qos, &msg_id) == 0) {
        log_trace("Message persisted: id=%llu", (unsigned long long)msg_id);
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

    ConfigManager gateway_cfg = {0};
    if (config_init(&gateway_cfg, APP_GATEWAY_CONFIG_FILE) != 0) {
        log_error("Failed to load gateway config file: %s", APP_GATEWAY_CONFIG_FILE);
        return -1;
    }
    if (config_load(&gateway_cfg) != 0) {
        log_error("Failed to load gateway config file: %s", APP_GATEWAY_CONFIG_FILE);
        config_destroy(&gateway_cfg);
        return -1;
    }

    RuntimeConfig runtime_config;
    if (load_runtime_config(&gateway_cfg, &runtime_config) != 0) {
        config_destroy(&gateway_cfg);
        return -1;
    }

    int device_buffer_size = DEFAULT_DEVICE_BUFFER_SIZE;
    int configured_device_count = 0;
    char device_paths[MAX_SERIAL_DEVICES][MAX_DEVICE_PATH_LEN];
    char device_interfaces[MAX_SERIAL_DEVICES][APP_INTERFACE_NAME_MAX_LEN];
    char device_protocols[MAX_SERIAL_DEVICES][APP_PROTOCOL_NAME_MAX_LEN];

    if (load_device_config(&gateway_cfg, device_paths, device_interfaces, device_protocols,
                           &configured_device_count, &device_buffer_size) != 0) {
        config_destroy(&gateway_cfg);
        return -1;
    }
    app_device_set_buffer_size(device_buffer_size);

    // 初始化线程池
    if (app_task_init(runtime_config.thread_pool_executors) != 0) {
        config_destroy(&gateway_cfg);
        return -1;
    }

    // 初始化持久化模块
    PersistenceConfig persist_config;
    if (load_persistence_config(&gateway_cfg, &persist_config) != 0) {
        config_destroy(&gateway_cfg);
        app_task_close();
        return -1;
    }
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

    log_info("Startup summary: configured_device_count=%d, device_buffer_size=%d, persistence_queue_size=%d",
             configured_device_count,
             device_buffer_size,
             persist_config.max_queue_size);

    int initialized_device_count = 0;
    for (int i = 0; i < configured_device_count; i++) {
        if (app_device_layer_init(&devices[i], device_paths[i], device_interfaces[i]) != 0) {
            log_error("Failed to init serial device: %s", device_paths[i]);
            for (int j = 0; j < initialized_device_count; j++) {
                app_device_layer_close((Device *)&devices[j]);
            }
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            app_task_close();
            config_destroy(&gateway_cfg);
            return -1;
        }

        if (app_device_layer_configure(&devices[i], device_protocols[i]) != 0) {
            log_error("Failed to apply protocol '%s' on device: %s",
                      device_protocols[i], device_paths[i]);
            app_device_layer_close((Device *)&devices[i]);
            for (int j = 0; j < initialized_device_count; j++) {
                app_device_layer_close((Device *)&devices[j]);
            }
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            app_task_close();
            config_destroy(&gateway_cfg);
            return -1;
        }

        initialized_device_count++;
    }

    // 初始化路由管理器
    if (app_router_init_with_config(&router, &gateway_cfg) != 0) {
        config_destroy(&gateway_cfg);
        for (int i = 0; i < initialized_device_count; i++) {
            app_device_layer_close((Device *)&devices[i]);
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
            for (int j = 0; j < initialized_device_count; j++) {
                app_device_layer_close((Device *)&devices[j]);
            }
            app_task_close();
            app_router_close(&router);
            if (persistence.is_initialized) {
                persistence_close(&persistence);
            }
            config_destroy(&gateway_cfg);
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
        app_task_close();
        app_router_close(&router);
        if (persistence.is_initialized) {
            persistence_close(&persistence);
        }
        config_destroy(&gateway_cfg);
        return -1;
    }

    // 进入主循环，等待停止信号
    app_task_wait();

    // 停止路由管理器
    app_router_stop(&router);

    // 清理已发送的消息
    if (persistence.is_initialized) {
        persistence_cleanup_sent(&persistence);
        persistence_close(&persistence);
    }

    // 先关闭线程池，确保不会再有设备回调访问路由状态
    app_task_close();
    app_router_close(&router);
    config_destroy(&gateway_cfg);

    return 0;
}
