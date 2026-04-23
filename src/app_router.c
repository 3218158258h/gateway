/**
 * @file app_router.c
 * @brief 消息路由模块实现 - 支持MQTT/DDS双协议切换
 * 
 * 功能说明：
 * - 设备与云端之间的消息路由和转发
 * - 支持MQTT和DDS传输协议的动态切换
 * - 线程安全的设备管理（注册/注销/查询）
 * - JSON与二进制格式的消息转换
 * - 统计信息和状态管理
 */

#include "../include/app_router.h"
#include "../include/app_message.h"
#include "../include/app_device_layer.h"
#include "../include/app_config.h"
#include "../thirdparty/log.c/log.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/* 默认配置常量 */
#define ROUTER_DEFAULT_MESSAGE_SIZE 4096                          /* 默认消息缓冲区大小 */

/* 当前路由器实例，用于设备回调转发。 */
static RouterManager *g_router = NULL;

/**
 * @brief 路由初始化失败统一清理
 */
static int router_init_fail(RouterManager *router, int transport_initialized)
{
    if (!router) return -1;
    if (transport_initialized) {
        transport_close(&router->transport);
    }
    pthread_mutex_destroy(&router->lock);
    memset(router, 0, sizeof(RouterManager));
    return -1;
}

/**
 * @brief 根据连接类型查找设备
 * 
 * 在路由管理器的设备列表中查找指定连接类型的设备。
 * 该函数不是线程安全的，调用前需要获取锁。
 * 
 * @param router 路由管理器指针
 * @param type 连接类型（BLE_MESH/LORA等）
 * @return 找到的设备指针，未找到返回NULL
 */
static Device *find_device_by_type(RouterManager *router, ConnectionType type)
{
    if (!router) return NULL;
    
    /* 遍历设备列表，匹配连接类型 */
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i] && 
            router->devices[i]->connection_type == type) {
            return router->devices[i];
        }
    }
    return NULL;
}

/**
 * @brief 将JSON字符串安全转换为二进制格式
 * 
 * 解析JSON字符串，提取设备ID和数据，转换为二进制格式。
 * 二进制格式：[连接类型(1字节)][ID长度(1字节)][数据长度(1字节)][ID][数据]
 * 
 * @param json_str JSON字符串指针
 * @param json_len JSON字符串长度
 * @param buf 输出缓冲区，用于存储二进制数据
 * @param buf_size 输出缓冲区大小
 * @return 转换后的二进制数据长度，失败返回-1
 */
static int json_to_binary_safe(const char *json_str, int json_len,
                                unsigned char *buf, int buf_size)
{
    if (!json_str || !buf || json_len <= 0 || buf_size <= 0) {
        return -1;
    }
    
    Message message;
    memset(&message, 0, sizeof(Message));
    
    /* 从JSON初始化消息结构 */
    if (app_message_initByJson(&message, (char *)json_str, json_len) < 0) {
        return -1;
    }
    
    /* 检查缓冲区大小是否足够 */
    int required_size = message.id_len + message.data_len + 3;
    if (required_size > buf_size) {
        app_message_free(&message);
        return -1;
    }
    
    /* 转换为二进制格式 */
    int buf_len = app_message_saveBinary(&message, buf, buf_size);
    app_message_free(&message);
    
    return buf_len;
}

/**
 * @brief 将二进制数据安全转换为JSON字符串
 * 
 * 解析二进制格式的消息数据，转换为JSON字符串格式。
 * JSON格式：{"id":"xxx", "data":"xxx", "type":x}
 * 
 * @param binary 二进制数据指针
 * @param binary_len 二进制数据长度
 * @param buf 输出缓冲区，用于存储JSON字符串
 * @param buf_size 输出缓冲区大小
 * @return 转换后的JSON字符串长度，失败返回-1
 */
static int binary_to_json_safe(const void *binary, int binary_len,
                                char *buf, int buf_size)
{
    if (!binary || !buf || binary_len <= 0 || buf_size <= 0) {
        return -1;
    }
    
    Message message;
    memset(&message, 0, sizeof(Message));
    
    /* 从二进制数据初始化消息结构 */
    if (app_message_initByBinary(&message, (void *)binary, binary_len) < 0) {
        return -1;
    }
    
    /* 转换为JSON格式 */
    int result = app_message_saveJson(&message, buf, buf_size);
    app_message_free(&message);
    
    return (result == 0) ? (int)strlen(buf) : -1;
}

/**
 * @brief 处理传输层消息回调（云端→设备）
 * 
 * 当从云端（MQTT/DDS）接收到消息时被传输层调用。
 * 处理流程：
 * 1. 将JSON格式转换为二进制格式
 * 2. 根据连接类型查找目标设备
 * 3. 将消息发送到对应设备
 * 
 * @param transport 传输管理器指针
 * @param topic 消息主题
 * @param data 消息数据（JSON格式）
 * @param len 消息长度
 */
static void on_transport_message(TransportManager *transport, const char *topic,
                                  const void *data, size_t len)
{
    RouterManager *router = (RouterManager *)transport;
    if (!router || !data || len == 0) return;
    
    /* 分配消息缓冲区 */
    int max_message_size = (router->max_message_size > 0) ? router->max_message_size : ROUTER_DEFAULT_MESSAGE_SIZE;
    unsigned char *buf = malloc((size_t)max_message_size);
    if (!buf) return;
    
    /* 将JSON格式转换为二进制格式 */
    int buf_len = json_to_binary_safe((const char *)data, (int)len,
                                       buf, max_message_size);
    if (buf_len < 0) {
        free(buf);
        return;
    }
    
    /* 从二进制数据中获取连接类型（第一个字节） */
    ConnectionType type = (ConnectionType)buf[0];
    
    /* 查找对应类型的设备 */
    pthread_mutex_lock(&router->lock);
    Device *device = find_device_by_type(router, type);
    pthread_mutex_unlock(&router->lock);
    
    if (!device) {
        free(buf);
        return;
    }
    
    /* 向设备发送数据 */
    app_device_write(device, buf, buf_len);
    free(buf);
    
    /* 调用云端消息回调函数（通知上层应用） */
    if (router->on_cloud_message) {
        router->on_cloud_message(router, topic, data, len);
    }
}

/**
 * @brief 处理传输层状态变化回调
 * 
 * 当传输层连接状态发生变化时被调用。
 * 将传输层状态映射到路由管理器状态，并通知上层应用。
 * 
 * @param transport 传输管理器指针
 * @param state 新的传输层状态
 */
static void on_transport_state_changed(TransportManager *transport,
                                        TransportState state)
{
    RouterManager *router = (RouterManager *)transport;
    if (!router) return;
    
    RouterState new_state;
    
    /* 根据传输层状态转换为路由管理器状态 */
    switch (state) {
        case TRANSPORT_STATE_CONNECTED:
            new_state = ROUTER_STATE_RUNNING;
            break;
        case TRANSPORT_STATE_ERROR:
            new_state = ROUTER_STATE_ERROR;
            break;
        default:
            new_state = ROUTER_STATE_STOPPED;
            break;
    }
    
    /* 更新路由管理器状态 */
    pthread_mutex_lock(&router->lock);
    router->state = new_state;
    pthread_mutex_unlock(&router->lock);
    
    /* 调用状态变化回调（通知上层应用） */
    if (router->on_state_changed) {
        router->on_state_changed(router, new_state);
    }
}

/**
 * @brief 处理设备消息回调（设备→云端）
 * 
 * 当设备发送消息时被设备层调用。
 * 处理流程：
 * 1. 将二进制格式转换为JSON格式
 * 2. 通过传输层发送到云端
 * 3. 更新统计信息
 * 
 * @param ptr 消息数据指针（二进制格式）
 * @param len 消息长度
 * @return 处理结果，成功返回0，失败返回-1
 */
static int on_device_message(void *ptr, int len)
{
    if (!g_router || !ptr || len <= 0) return -1;
    
    RouterManager *router = g_router;
    
    int max_message_size = (router->max_message_size > 0) ? router->max_message_size : ROUTER_DEFAULT_MESSAGE_SIZE;
    /* 分配JSON缓冲区 */
    char *buf = malloc((size_t)max_message_size);
    if (!buf) return -1;
    
    /* 将二进制格式转换为JSON格式 */
    int json_len = binary_to_json_safe(ptr, len, buf, max_message_size);
    if (json_len < 0) {
        free(buf);
        return -1;
    }
    
    /* 使用统一的发布接口，自动适配MQTT/DDS */
    int result = transport_publish_default(&router->transport, buf, json_len);
    
    free(buf);
    
    if (result < 0) {
        return -1;
    }
    
    /* 更新发送统计 */
    pthread_mutex_lock(&router->lock);
    router->stats.messages_sent++;
    pthread_mutex_unlock(&router->lock);
    
    /* 调用设备消息回调（通知上层应用） */
    if (router->on_device_message) {
        ConnectionType type = ((unsigned char *)ptr)[0];
        pthread_mutex_lock(&router->lock);
        Device *device = find_device_by_type(router, type);
        pthread_mutex_unlock(&router->lock);
        if (device) {
            router->on_device_message(router, device, ptr, len);
        }
    }
    
    return 0;
}

/**
 * @brief 初始化路由管理器
 * 
 * 初始化路由管理器的各个组件：
 * - 初始化互斥锁
 * - 从配置文件初始化传输层
 * - 注册传输层回调函数
 * 
 * @param router 路由管理器指针
 * @param config_file 配置文件路径，NULL则使用默认配置
 * @return 初始化结果，成功返回0，失败返回-1
 */
static int app_router_init_from_loaded_config(RouterManager *router, const ConfigManager *cfg)
{
    if (!router || !cfg) return -1;

    /* 初始化路由管理器结构体 */
    memset(router, 0, sizeof(RouterManager));

    /* 初始化互斥锁 */
    if (pthread_mutex_init(&router->lock, NULL) != 0) {
        return -1;
    }

    /* 初始化设备计数和状态 */
    router->device_count = 0;
    router->state = ROUTER_STATE_STOPPED;
    router->is_initialized = 0;

    int transport_inited = 0;
    int router_message_size = config_get_int((ConfigManager *)cfg, "router", "max_message_size", ROUTER_DEFAULT_MESSAGE_SIZE);

    char transport_cfg_file[CONFIG_MAX_PATH_LEN] = {0};
    config_get_string((ConfigManager *)cfg, "config_files", "transport", APP_TRANSPORT_CONFIG_FILE,
                      transport_cfg_file, sizeof(transport_cfg_file));
    if (transport_cfg_file[0] == '\0') {
        snprintf(transport_cfg_file, sizeof(transport_cfg_file), "%s", APP_TRANSPORT_CONFIG_FILE);
    }

    if (router_message_size <= 0) {
        log_error("Invalid config [router].max_message_size: %d", router_message_size);
        return router_init_fail(router, transport_inited);
    }
    router->max_message_size = router_message_size;

    TransportConfig transport_config = {0};

    /* 先加载网络侧协议配置，再初始化传输层 */
    if (transport_load_config(&transport_config, transport_cfg_file) != 0) {
        log_error("Failed to load transport config file: %s", transport_cfg_file);
        return router_init_fail(router, transport_inited);
    }
    if (transport_init(&router->transport, &transport_config) != 0) {
        log_error("Failed to initialize transport from config file: %s", transport_cfg_file);
        return router_init_fail(router, transport_inited);
    }
    transport_inited = 1;

    /* 注册传输层回调函数 */
    transport_on_message(&router->transport, on_transport_message);
    transport_on_state_changed(&router->transport, on_transport_state_changed);

    /* 设置初始化完成状态 */
    router->is_initialized = 1;
    g_router = router;
    log_info("Router initialized: max_message_size=%d, max_devices=%d",
             router->max_message_size, ROUTER_MAX_DEVICES);

    return 0;
}

/**
 * @brief 初始化路由器（从配置文件）
 * @param router 路由器指针
 * @param config_file 配置文件路径（NULL使用默认）
 * @return 0成功, -1失败
 */
int app_router_init(RouterManager *router, const char *config_file)
{
    const char *gateway_cfg_file = config_file ? config_file : APP_DEFAULT_CONFIG_FILE;
    ConfigManager cfg = {0};
    if (config_init(&cfg, gateway_cfg_file) != 0) {
        log_error("Failed to load gateway config file: %s", gateway_cfg_file);
        return -1;
    }
    if (config_load(&cfg) != 0) {
        log_error("Failed to load gateway config file: %s", gateway_cfg_file);
        config_destroy(&cfg);
        return -1;
    }

    int result = app_router_init_from_loaded_config(router, &cfg);
    config_destroy(&cfg);
    return result;
}

int app_router_init_with_config(RouterManager *router, const ConfigManager *gateway_config)
{
    return app_router_init_from_loaded_config(router, gateway_config);
}

/**
 * @brief 关闭路由管理器
 * 
 * 释放路由管理器的所有资源：
 * - 断开传输层连接
 * - 停止并关闭所有设备
 * - 销毁互斥锁
 * 
 * @param router 路由管理器指针
 */
void app_router_close(RouterManager *router)
{
    if (!router || !router->is_initialized) return;
    
    /* 断开传输层连接 */
    transport_disconnect(&router->transport);
    
    /* 停止并关闭所有设备 */
    pthread_mutex_lock(&router->lock);
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i]) {
            app_device_layer_close(router->devices[i]);
            router->devices[i] = NULL;
        }
    }
    router->device_count = 0;
    pthread_mutex_unlock(&router->lock);
    
    /* 关闭传输层 */
    transport_close(&router->transport);
    
    /* 销毁互斥锁 */
    pthread_mutex_destroy(&router->lock);
    
    /* 标记为未初始化 */
    router->is_initialized = 0;
    if (g_router == router) {
        g_router = NULL;
    }

}

/**
 * @brief 注册设备到路由管理器
 * 
 * 将设备添加到路由管理器的设备列表中，并注册消息回调。
 * 如果设备已存在则直接返回成功。
 * 
 * @param router 路由管理器指针
 * @param device 设备指针
 * @return 注册结果，成功返回0，失败返回-1（设备数量已满）
 */
int app_router_register_device(RouterManager *router, Device *device)
{
    if (!router || !device || !router->is_initialized) return -1;
    
    pthread_mutex_lock(&router->lock);
    
    /* 检查设备数量是否已达上限 */
    if (router->device_count >= ROUTER_MAX_DEVICES) {
        pthread_mutex_unlock(&router->lock);
        return -1;
    }
    
    /* 检查设备是否已注册 */
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i] == device) {
            pthread_mutex_unlock(&router->lock);
            return 0;
        }
    }
    
    /* 添加设备到列表 */
    router->devices[router->device_count++] = device;
    
    /* 注册设备消息回调 */
    app_device_registerRecvCallback(device, on_device_message);
    
    pthread_mutex_unlock(&router->lock);
    
    return 0;
}

/**
 * @brief 启动路由管理器
 * 
 * 启动路由管理器的各个组件：
 * - 连接传输层（MQTT/DDS）
 * - 订阅云端下发话题
 * - 启动所有已注册的设备
 * 
 * @param router 路由管理器指针
 * @return 启动结果，成功返回0，失败返回-1
 */
int app_router_start(RouterManager *router)
{
    if (!router || !router->is_initialized) return -1;
    
    /* 检查是否已运行 */
    if (router->state == ROUTER_STATE_RUNNING) return 0;
    
    /* 连接传输层 */
    if (transport_connect(&router->transport) != 0) {
        router->state = ROUTER_STATE_ERROR;
        return -1;
    }
    
    /* 订阅下发话题（接收云端指令）- 自动适配MQTT/DDS */
    if (transport_subscribe_default(&router->transport) != 0) {
        log_warn("Failed to subscribe to command topic");
        /* 不返回错误，继续运行 */
    } else {
        log_info("Subscribed to command topic: %s", 
                 transport_get_subscribe_topic(&router->transport));
    }
    
    /* 启动所有设备 */
    pthread_mutex_lock(&router->lock);
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i]) {
            if (app_device_layer_start(router->devices[i]) != 0) {
                for (int j = 0; j <= i; j++) {
                    if (router->devices[j]) {
                        app_device_layer_stop(router->devices[j]);
                    }
                }
                router->state = ROUTER_STATE_ERROR;
                pthread_mutex_unlock(&router->lock);
                transport_disconnect(&router->transport);
                return -1;
            }
        }
    }
    router->state = ROUTER_STATE_RUNNING;
    pthread_mutex_unlock(&router->lock);
    
    return 0;
}

/**
 * @brief 停止路由管理器
 * 
 * 停止路由管理器的各个组件：
 * - 停止所有设备
 * - 断开传输层连接
 * 
 * @param router 路由管理器指针
 */
void app_router_stop(RouterManager *router)
{
    if (!router) return;
    
    /* 停止所有设备 */
    pthread_mutex_lock(&router->lock);
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i]) {
            app_device_layer_stop(router->devices[i]);
        }
    }
    router->state = ROUTER_STATE_STOPPED;
    pthread_mutex_unlock(&router->lock);
    
    /* 断开传输层连接 */
    transport_disconnect(&router->transport);
}

/**
 * @brief 注销设备
 * 
 * 从路由管理器的设备列表中移除设备，并停止设备。
 * 
 * @param router 路由管理器指针
 * @param device 设备指针
 * @return 注销结果，成功返回0，失败返回-1（设备不存在）
 */
int app_router_unregister_device(RouterManager *router, Device *device)
{
    if (!router || !device || !router->is_initialized) return -1;
    
    pthread_mutex_lock(&router->lock);
    
    /* 查找并移除设备 */
    for (int i = 0; i < router->device_count; i++) {
        if (router->devices[i] == device) {
            app_device_layer_stop(device);
            
            /* 移动后面的元素填补空位 */
            for (int j = i; j < router->device_count - 1; j++) {
                router->devices[j] = router->devices[j + 1];
            }
            router->devices[--router->device_count] = NULL;
            
            pthread_mutex_unlock(&router->lock);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&router->lock);
    return -1;
}

/**
 * @brief 获取设备数量
 * 
 * @param router 路由管理器指针
 * @return 已注册的设备数量
 */
int app_router_get_device_count(RouterManager *router)
{
    return router ? router->device_count : 0;
}

/**
 * @brief 切换传输类型
 * 
 * 在MQTT和DDS之间切换传输协议。
 * 
 * @param router 路由管理器指针
 * @param type 目标传输类型（MQTT/DDS）
 * @return 切换结果，成功返回0，失败返回-1
 */
int app_router_switch_transport(RouterManager *router, TransportType type)
{
    if (!router || !router->is_initialized) return -1;
    return transport_switch_type(&router->transport, type);
}

/**
 * @brief 获取当前传输类型
 * 
 * @param router 路由管理器指针
 * @return 当前传输类型
 */
TransportType app_router_get_transport_type(RouterManager *router)
{
    return router ? transport_get_type(&router->transport) : TRANSPORT_TYPE_MQTT;
}

/**
 * @brief 向云端发送消息
 * 
 * 通过传输层将消息发送到云端。
 * 如果未指定话题，则使用默认发布话题。
 * 
 * @param router 路由管理器指针
 * @param topic 消息主题，NULL使用默认话题
 * @param data 消息数据
 * @param len 消息长度
 * @return 发送结果，成功返回0，失败返回-1
 */
int app_router_send_to_cloud(RouterManager *router, const char *topic,
                              const void *data, size_t len)
{
    if (!router || !data) return -1;
    
    /* 如果没有指定话题，使用默认发布话题 */
    if (!topic) {
        return transport_publish_default(&router->transport, data, len);
    }
    return transport_publish(&router->transport, topic, data, len);
}

/**
 * @brief 向设备发送消息
 * 
 * 将消息发送到指定设备。
 * 
 * @param router 路由管理器指针
 * @param device 目标设备指针
 * @param data 消息数据
 * @param len 消息长度
 * @return 发送结果，成功返回0，失败返回-1
 */
int app_router_send_to_device(RouterManager *router, Device *device,
                               const void *data, size_t len)
{
    if (!router || !device || !data) return -1;
    return app_device_write(device, (void *)data, (int)len);
}

/**
 * @brief 获取路由管理器状态
 * 
 * @param router 路由管理器指针
 * @return 当前状态
 */
RouterState app_router_get_state(RouterManager *router)
{
    return router ? router->state : ROUTER_STATE_STOPPED;
}

/**
 * @brief 获取统计信息
 * 
 * 获取消息发送/接收/错误等统计信息。
 * 
 * @param router 路由管理器指针
 * @param stats 输出统计信息结构体
 */
void app_router_get_stats(RouterManager *router, RouterStats *stats)
{
    if (router && stats) {
        pthread_mutex_lock(&router->lock);
        memcpy(stats, &router->stats, sizeof(RouterStats));
        pthread_mutex_unlock(&router->lock);
    }
}

/**
 * @brief 重置统计信息
 * 
 * 将所有统计计数器清零。
 * 
 * @param router 路由管理器指针
 */
void app_router_reset_stats(RouterManager *router)
{
    if (router) {
        pthread_mutex_lock(&router->lock);
        memset(&router->stats, 0, sizeof(RouterStats));
        pthread_mutex_unlock(&router->lock);
    }
}

/**
 * @brief 设置设备消息回调
 * 
 * 当设备发送消息时调用此回调函数。
 * 
 * @param router 路由管理器指针
 * @param callback 回调函数指针
 */
void app_router_on_device_message(RouterManager *router,
    void (*callback)(RouterManager *, Device *, const void *, size_t))
{
    if (router) router->on_device_message = callback;
}

/**
 * @brief 设置云端消息回调
 * 
 * 当从云端接收到消息时调用此回调函数。
 * 
 * @param router 路由管理器指针
 * @param callback 回调函数指针
 */
void app_router_on_cloud_message(RouterManager *router,
    void (*callback)(RouterManager *, const char *, const void *, size_t))
{
    if (router) router->on_cloud_message = callback;
}

/**
 * @brief 设置状态变化回调
 * 
 * 当路由管理器状态发生变化时调用此回调函数。
 * 
 * @param router 路由管理器指针
 * @param callback 回调函数指针
 */
void app_router_on_state_changed(RouterManager *router,
    void (*callback)(RouterManager *, RouterState))
{
    if (router) router->on_state_changed = callback;
}
