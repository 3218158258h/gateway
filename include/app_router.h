/**
 * @file app_router.h
 * @brief 消息路由模块，负责设备侧与云侧之间的转发和编排
 *
 * 主要职责：
 * - 设备与云端之间的消息路由
 * - MQTT / DDS 传输切换
 * - 线程安全的设备管理
 * - 消息格式转换
 */

#ifndef __APP_ROUTER_H__
#define __APP_ROUTER_H__

#include "app_device.h"
#include "app_transport.h"
#include "app_config.h"

/* 最大设备数量 */
#define ROUTER_MAX_DEVICES 32

/* 路由器状态 */
typedef enum {
    ROUTER_STATE_STOPPED = 0,
    ROUTER_STATE_RUNNING,
    ROUTER_STATE_ERROR
} RouterState;

/* 路由器统计信息 */
typedef struct RouterStats {
    uint64_t messages_sent;     /* 发送消息数 */
    uint64_t messages_received; /* 接收消息数 */
    uint64_t send_errors;       /* 发送错误数 */
    uint64_t recv_errors;       /* 接收错误数 */
} RouterStats;

/* 路由器管理器 */
typedef struct RouterManager {
    TransportManager transport;     /* 传输管理器 */
    Device *devices[ROUTER_MAX_DEVICES];
    int max_message_size;
    int device_count;
    RouterState state;
    pthread_mutex_t lock;           /* 线程安全锁 */
    RouterStats stats;
    int is_initialized;
    
    /* 回调 */
    void (*on_device_message)(struct RouterManager *router, Device *device,
                              const void *data, size_t len);
    void (*on_cloud_message)(struct RouterManager *router, const char *topic,
                             const void *data, size_t len);
    void (*on_state_changed)(struct RouterManager *router, RouterState state);
} RouterManager;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化路由器（从配置文件）
 * @param router 路由器指针
 * @param config_file 配置文件路径（NULL使用默认）
 * @return 0成功, -1失败
 */
int app_router_init(RouterManager *router, const char *config_file);

/**
 * @brief 使用已加载的总配置初始化路由器
 * @param router 路由器指针
 * @param gateway_config 已加载的 gateway.ini 配置
 * @return 0成功, -1失败
 */
int app_router_init_with_config(RouterManager *router, const ConfigManager *gateway_config);

/**
 * @brief 关闭路由器
 * @param router 路由器指针
 */
void app_router_close(RouterManager *router);

/* ========== 设备管理 ========== */

/**
 * @brief 注册设备
 * @param router 路由器指针
 * @param device 设备指针
 * @return 0成功, -1失败（设备数量已满）
 */
int app_router_register_device(RouterManager *router, Device *device);

/**
 * @brief 注销设备
 * @param router 路由器指针
 * @param device 设备指针
 * @return 0成功, -1失败
 */
int app_router_unregister_device(RouterManager *router, Device *device);

/**
 * @brief 获取设备数量
 * @param router 路由器指针
 * @return 设备数量
 */
int app_router_get_device_count(RouterManager *router);

/* ========== 传输控制 ========== */

/**
 * @brief 启动传输层
 * @param router 路由器指针
 * @return 0成功, -1失败
 */
int app_router_start(RouterManager *router);

/**
 * @brief 停止传输层
 * @param router 路由器指针
 */
void app_router_stop(RouterManager *router);

/**
 * @brief 切换传输类型
 * @param router 路由器指针
 * @param type 传输类型（MQTT/DDS）
 * @return 0成功, -1失败
 */
int app_router_switch_transport(RouterManager *router, TransportType type);

/**
 * @brief 获取当前传输类型
 * @param router 路由器指针
 * @return 传输类型
 */
TransportType app_router_get_transport_type(RouterManager *router);

/* ========== 消息发送 ========== */

/**
 * @brief 向云端发送消息
 * @param router 路由器指针
 * @param topic 主题（NULL使用默认）
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int app_router_send_to_cloud(RouterManager *router, const char *topic,
                              const void *data, size_t len);

/**
 * @brief 向设备发送消息
 * @param router 路由器指针
 * @param device 设备指针
 * @param data 数据
 * @param len 数据长度
 * @return 0成功, -1失败
 */
int app_router_send_to_device(RouterManager *router, Device *device,
                               const void *data, size_t len);

/* ========== 状态查询 ========== */

/**
 * @brief 获取路由器状态
 * @param router 路由器指针
 * @return 状态
 */
RouterState app_router_get_state(RouterManager *router);

/**
 * @brief 获取统计信息
 * @param router 路由器指针
 * @param stats 输出统计信息
 */
void app_router_get_stats(RouterManager *router, RouterStats *stats);

/**
 * @brief 重置统计信息
 * @param router 路由器指针
 */
void app_router_reset_stats(RouterManager *router);

/* ========== 回调设置 ========== */

/**
 * @brief 设置设备消息回调
 */
void app_router_on_device_message(RouterManager *router,
    void (*callback)(RouterManager *, Device *, const void *, size_t));

/**
 * @brief 设置云端消息回调
 */
void app_router_on_cloud_message(RouterManager *router,
    void (*callback)(RouterManager *, const char *, const void *, size_t));

/**
 * @brief 设置状态变化回调
 */
void app_router_on_state_changed(RouterManager *router,
    void (*callback)(RouterManager *, RouterState));

#endif /* __APP_ROUTER_H__ */
