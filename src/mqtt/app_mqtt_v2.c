/**
 * @file app_mqtt_v2.c
 * @brief MQTT客户端改进版实现 - 基于Paho MQTT库
 * 
 * 功能说明：
 * - MQTT连接管理与自动重连
 * - 消息发布与订阅
 * - 指数退避重连策略
 * - 状态回调与事件通知
 * - 后台线程消息循环
 */

#include "app_mqtt_v2.h"
#include "../thirdparty/log.c/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

/* 使用paho-mqtt库 */
#include <MQTTClient.h>

/**
 * @brief MQTT内部结构体
 * 
 * 存储MQTT客户端的内部状态和资源。
 */
typedef struct {
    MQTTClient mqtt_client;         // Paho MQTT客户端句柄
    pthread_t thread;               // 后台线程
    int running;                    // 运行标志
    pthread_mutex_t lock;           // 状态锁
    
    /* 重连状态 */
    int reconnect_delay;            // 当前重连延迟（秒）
    time_t last_reconnect_time;     // 上次重连尝试时间
} MqttInternal;

/**
 * @brief 连接丢失回调
 * 
 * 当MQTT连接断开时由Paho库调用。
 * 更新状态并触发相关回调。
 * 
 * @param context 用户上下文（MqttClient指针）
 * @param cause 断开原因字符串
 */
static void connection_lost(void *context, char *cause)
{
    MqttClient *client = (MqttClient *)context;
    if (!client) return;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    pthread_mutex_lock(&internal->lock);
    
    // 更新状态为断开
    client->state = MQTT_STATE_DISCONNECTED;
    // 重置重连延迟为最小值
    internal->reconnect_delay = client->config.reconnect_min_interval;
    
    pthread_mutex_unlock(&internal->lock);
    
    log_warn("MQTT connection lost: %s", cause ? cause : "unknown");
    
    // 触发断开回调
    if (client->on_disconnected) {
        client->on_disconnected(client);
    }
    // 触发状态变化回调
    if (client->on_state_changed) {
        client->on_state_changed(client, MQTT_STATE_DISCONNECTED);
    }
}

/**
 * @brief 消息到达回调
 * 
 * 当收到订阅主题的消息时由Paho库调用。
 * 
 * @param context 用户上下文（MqttClient指针）
 * @param topicName 消息主题
 * @param topicLen 主题长度
 * @param message 消息结构体
 * @return 1表示消息已处理
 */
static int message_arrived(void *context, char *topicName, int topicLen,
                           MQTTClient_message *message)
{
    MqttClient *client = (MqttClient *)context;
    if (!client) {
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
    
    log_debug("MQTT message received on topic: %s", topicName);
    
    // 调用用户注册的消息回调
    if (client->on_message) {
        client->on_message(client, topicName, message->payload, message->payloadlen);
    }
    
    // 释放Paho库分配的资源
    MQTTClient_freeMessage(&message);
    MQTTClient_free(topicName);
    return 1;
}

/**
 * @brief 消息交付完成回调
 * 
 * 当消息成功发送到Broker时调用。
 * 
 * @param context 用户上下文
 * @param dt 交付令牌
 */
static void delivery_complete(void *context, MQTTClient_deliveryToken dt)
{
    (void)context;  // 避免未使用警告
    (void)dt;
}

/**
 * @brief 计算重连延迟（指数退避）
 * 
 * 使用指数退避算法计算下次重连的等待时间。
 * 每次重连失败后延迟翻倍，直到达到最大值。
 * 
 * @param client MQTT客户端指针
 * @return 重连延迟时间（秒）
 */
static int calculate_reconnect_delay(MqttClient *client)
{
    MqttInternal *internal = (MqttInternal *)client->internal;
    int delay = internal->reconnect_delay;
    
    // 指数退避：延迟翻倍
    internal->reconnect_delay *= 2;
    
    // 限制最大延迟
    if (internal->reconnect_delay > client->config.reconnect_max_interval) {
        internal->reconnect_delay = client->config.reconnect_max_interval;
    }
    
    return delay;
}

/**
 * @brief 执行重连操作
 * 
 * 尝试重新连接到MQTT Broker。
 * 
 * @param client MQTT客户端指针
 * @return 0成功，-1失败
 */
static int do_reconnect(MqttClient *client)
{
    MqttInternal *internal = (MqttInternal *)client->internal;
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    
    // 设置连接选项
    opts.keepAliveInterval = client->config.keepalive_interval;
    opts.cleansession = client->config.clean_session;
    
    // 设置用户名密码（如果有）
    if (client->config.username[0]) {
        opts.username = client->config.username;
    }
    if (client->config.password[0]) {
        opts.password = client->config.password;
    }
    
    opts.reliable = 1;          // 可靠传输
    opts.connectTimeout = 10;   // 连接超时10秒
    
    // 尝试连接
    int rc = MQTTClient_connect(internal->mqtt_client, &opts);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        // 连接成功
        client->state = MQTT_STATE_CONNECTED;
        client->reconnect_attempts = 0;  // 重置重连计数
        internal->reconnect_delay = client->config.reconnect_min_interval;
        
        log_info("MQTT reconnected successfully");
        
        // 触发回调
        if (client->on_connected) {
            client->on_connected(client);
        }
        if (client->on_state_changed) {
            client->on_state_changed(client, MQTT_STATE_CONNECTED);
        }
        
        return 0;
    }
    
    log_warn("MQTT reconnect failed: %d", rc);
    return -1;
}

/**
 * @brief MQTT主循环
 * 
 * 在后台线程中运行，处理自动重连逻辑。
 * 
 * @param client MQTT客户端指针
 */
void mqtt_loop(MqttClient *client)
{
    if (!client) return;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    while (internal->running) {
        // 获取当前连接状态
        pthread_mutex_lock(&internal->lock);
        MqttState state = client->state;
        pthread_mutex_unlock(&internal->lock);
        
        // 处理断开状态下的自动重连
        if (state == MQTT_STATE_DISCONNECTED && client->config.auto_reconnect) {
            // 检查是否达到最大重连次数
            if (client->config.reconnect_max_attempts > 0 &&
                client->reconnect_attempts >= client->config.reconnect_max_attempts) {
                
                pthread_mutex_lock(&internal->lock);
                client->state = MQTT_STATE_FAILED;
                pthread_mutex_unlock(&internal->lock);
                
                log_error("MQTT max reconnect attempts reached");
                
                if (client->on_state_changed) {
                    client->on_state_changed(client, MQTT_STATE_FAILED);
                }
                break;  // 退出循环
            }
            
            // 检查是否到达重连时间
            time_t now = time(NULL);
            int delay = calculate_reconnect_delay(client);
            
            if (now - internal->last_reconnect_time >= delay) {
                // 更新状态为重连中
                pthread_mutex_lock(&internal->lock);
                client->state = MQTT_STATE_RECONNECTING;
                pthread_mutex_unlock(&internal->lock);
                
                if (client->on_state_changed) {
                    client->on_state_changed(client, MQTT_STATE_RECONNECTING);
                }
                
                client->reconnect_attempts++;
                internal->last_reconnect_time = now;
                
                log_info("MQTT reconnecting (attempt %d)...", client->reconnect_attempts);
                
                // 执行重连
                do_reconnect(client);
            }
        }
        
        // 等待100ms后继续循环
        usleep(100000);
    }
}

/**
 * @brief 后台线程入口函数
 * 
 * @param arg MQTT客户端指针
 * @return NULL
 */
static void *mqtt_thread_func(void *arg)
{
    MqttClient *client = (MqttClient *)arg;
    mqtt_loop(client);
    return NULL;
}

/**
 * @brief 初始化MQTT客户端
 * 
 * @param client MQTT客户端指针
 * @param config 配置参数，NULL使用默认配置
 * @return 0成功，-1失败
 */
int mqtt_init(MqttClient *client, const MqttConfig *config)
{
    if (!client) return -1;
    
    // 清零结构体
    memset(client, 0, sizeof(MqttClient));
    
    // 加载配置
    if (config) {
        memcpy(&client->config, config, sizeof(MqttConfig));
    } else {
        // 使用默认配置
        strcpy(client->config.broker_url, "tcp://localhost:1883");
        client->config.keepalive_interval = 60;
        client->config.clean_session = 1;
        client->config.default_qos = MQTT_QOS_0;
        client->config.auto_reconnect = 1;
        client->config.reconnect_min_interval = 1;    // 最小重连间隔1秒
        client->config.reconnect_max_interval = 60;   // 最大重连间隔60秒
        client->config.reconnect_max_attempts = 0;    // 无限重试
    }
    
    // 分配内部结构
    MqttInternal *internal = calloc(1, sizeof(MqttInternal));
    if (!internal) return -1;
    
    // 初始化互斥锁
    pthread_mutex_init(&internal->lock, NULL);
    internal->reconnect_delay = client->config.reconnect_min_interval;
    
    client->internal = internal;
    client->state = MQTT_STATE_DISCONNECTED;
    
    // 创建Paho MQTT客户端
    int rc = MQTTClient_create(&internal->mqtt_client,
                               client->config.broker_url,
                               client->config.client_id,
                               MQTTCLIENT_PERSISTENCE_NONE, NULL);  // 无持久化
    if (rc != MQTTCLIENT_SUCCESS) {
        log_error("Failed to create MQTT client: %d", rc);
        pthread_mutex_destroy(&internal->lock);
        free(internal);
        return -1;
    }
    
    // 设置回调函数
    MQTTClient_setCallbacks(internal->mqtt_client, client,
                           connection_lost, message_arrived, delivery_complete);
    
    log_debug("MQTT client initialized: %s", client->config.broker_url);
    return 0;
}

/**
 * @brief 使用默认配置初始化MQTT客户端
 * 
 * @param client MQTT客户端指针
 * @param broker_url Broker地址
 * @param client_id 客户端ID
 * @return 0成功，-1失败
 */
int mqtt_init_default(MqttClient *client, const char *broker_url, const char *client_id)
{
    MqttConfig config = {0};
    
    // 设置Broker地址
    if (broker_url) {
        strncpy(config.broker_url, broker_url, sizeof(config.broker_url) - 1);
    } else {
        strcpy(config.broker_url, "tcp://localhost:1883");
    }
    
    // 设置客户端ID
    if (client_id) {
        strncpy(config.client_id, client_id, sizeof(config.client_id) - 1);
    }
    
    // 默认配置
    config.keepalive_interval = 60;
    config.clean_session = 1;
    config.default_qos = MQTT_QOS_1;
    config.auto_reconnect = 1;
    config.reconnect_min_interval = 1;
    config.reconnect_max_interval = 60;
    config.reconnect_max_attempts = 0;
    
    return mqtt_init(client, &config);
}

/**
 * @brief 销毁MQTT客户端
 * 
 * 释放所有资源。
 * 
 * @param client MQTT客户端指针
 */
void mqtt_destroy(MqttClient *client)
{
    if (!client) return;
    
    // 先停止客户端
    mqtt_stop(client);
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    if (internal) {
        MQTTClient_destroy(&internal->mqtt_client);
        pthread_mutex_destroy(&internal->lock);
        free(internal);
        client->internal = NULL;
    }
    
    memset(client, 0, sizeof(MqttClient));
}

/**
 * @brief 连接到MQTT Broker
 * 
 * @param client MQTT客户端指针
 * @return 0成功，-1失败
 */
int mqtt_connect(MqttClient *client)
{
    if (!client || !client->internal) return -1;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    // 更新状态为连接中
    pthread_mutex_lock(&internal->lock);
    client->state = MQTT_STATE_CONNECTING;
    pthread_mutex_unlock(&internal->lock);
    
    if (client->on_state_changed) {
        client->on_state_changed(client, MQTT_STATE_CONNECTING);
    }
    
    // 设置连接选项
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = client->config.keepalive_interval;
    opts.cleansession = client->config.clean_session;
    
    // 设置认证信息
    if (client->config.username[0]) {
        opts.username = client->config.username;
    }
    if (client->config.password[0]) {
        opts.password = client->config.password;
    }
    
    opts.reliable = 1;
    opts.connectTimeout = 10;
    
    // 执行连接
    int rc = MQTTClient_connect(internal->mqtt_client, &opts);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        // 连接成功
        pthread_mutex_lock(&internal->lock);
        client->state = MQTT_STATE_CONNECTED;
        internal->reconnect_delay = client->config.reconnect_min_interval;
        pthread_mutex_unlock(&internal->lock);
        
        log_info("MQTT connected to %s", client->config.broker_url);
        
        // 触发回调
        if (client->on_connected) {
            client->on_connected(client);
        }
        if (client->on_state_changed) {
            client->on_state_changed(client, MQTT_STATE_CONNECTED);
        }
        
        return 0;
    }
    
    // 连接失败
    pthread_mutex_lock(&internal->lock);
    client->state = MQTT_STATE_FAILED;
    pthread_mutex_unlock(&internal->lock);
    
    log_error("MQTT connection failed: %d", rc);
    
    if (client->on_state_changed) {
        client->on_state_changed(client, MQTT_STATE_FAILED);
    }
    
    return -1;
}

/**
 * @brief 断开MQTT连接
 * 
 * @param client MQTT客户端指针
 */
void mqtt_disconnect(MqttClient *client)
{
    if (!client || !client->internal) return;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    // 只在已连接或正在连接时才调用disconnect
    if (client->state == MQTT_STATE_CONNECTED || 
        client->state == MQTT_STATE_CONNECTING ||
        client->state == MQTT_STATE_RECONNECTING) {
        MQTTClient_disconnect(internal->mqtt_client, 5000);  // 5秒超时
    }
    
    // 更新状态
    pthread_mutex_lock(&internal->lock);
    client->state = MQTT_STATE_DISCONNECTED;
    pthread_mutex_unlock(&internal->lock);
    
    log_info("MQTT disconnected");
    
    // 触发回调
    if (client->on_disconnected) {
        client->on_disconnected(client);
    }
    if (client->on_state_changed) {
        client->on_state_changed(client, MQTT_STATE_DISCONNECTED);
    }
}

/**
 * @brief 获取当前连接状态
 * 
 * @param client MQTT客户端指针
 * @return 连接状态枚举值
 */
MqttState mqtt_get_state(MqttClient *client)
{
    if (!client) return MQTT_STATE_DISCONNECTED;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    if (!internal) return MQTT_STATE_DISCONNECTED;
    
    MqttState state;
    pthread_mutex_lock(&internal->lock);
    state = client->state;
    pthread_mutex_unlock(&internal->lock);
    
    return state;
}

/**
 * @brief 检查是否已连接
 * 
 * @param client MQTT客户端指针
 * @return 1已连接，0未连接
 */
int mqtt_is_connected(MqttClient *client)
{
    return mqtt_get_state(client) == MQTT_STATE_CONNECTED;
}

/**
 * @brief 发布消息
 * 
 * 向指定主题发布消息。
 * 
 * @param client MQTT客户端指针
 * @param topic 主题名
 * @param payload 消息内容
 * @param len 消息长度
 * @param qos QoS级别
 * @return 0成功，-1失败
 */
int mqtt_publish(MqttClient *client, const char *topic, 
                 const void *payload, size_t len, MqttQos qos)
{
    if (!client || !client->internal || !topic || !payload) return -1;
    
    // 检查连接状态
    if (!mqtt_is_connected(client)) {
        log_warn("MQTT not connected, cannot publish");
        return -1;
    }
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    // 构建消息
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    pubmsg.payload = (void *)payload;
    pubmsg.payloadlen = len;
    pubmsg.qos = qos;
    pubmsg.retained = 0;  // 不保留消息
    
    // 发布消息
    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(internal->mqtt_client, topic, &pubmsg, &token);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        // QoS > 0时等待交付完成
        if (qos > MQTT_QOS_0) {
            MQTTClient_waitForCompletion(internal->mqtt_client, token, 5000);
        }
        log_debug("MQTT message published to %s", topic);
        return 0;
    }
    
    log_error("MQTT publish failed: %d", rc);
    return -1;
}

/**
 * @brief 发布字符串消息
 * 
 * 便捷函数，发布字符串类型的消息。
 * 
 * @param client MQTT客户端指针
 * @param topic 主题名
 * @param message 字符串消息
 * @param qos QoS级别
 * @return 0成功，-1失败
 */
int mqtt_publish_string(MqttClient *client, const char *topic, 
                        const char *message, MqttQos qos)
{
    if (!message) return -1;
    return mqtt_publish(client, topic, message, strlen(message), qos);
}

/**
 * @brief 订阅主题
 * 
 * @param client MQTT客户端指针
 * @param topic 主题名
 * @param qos QoS级别
 * @return 0成功，-1失败
 */
int mqtt_subscribe(MqttClient *client, const char *topic, MqttQos qos)
{
    if (!client || !client->internal || !topic) return -1;
    
    // 检查连接状态
    if (!mqtt_is_connected(client)) {
        log_warn("MQTT not connected, cannot subscribe");
        return -1;
    }
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    // 执行订阅
    int rc = MQTTClient_subscribe(internal->mqtt_client, topic, qos);
    
    if (rc == MQTTCLIENT_SUCCESS) {
        log_info("MQTT subscribed to %s", topic);
        return 0;
    }
    
    log_error("MQTT subscribe failed: %d", rc);
    return -1;
}

/**
 * @brief 取消订阅主题
 * 
 * @param client MQTT客户端指针
 * @param topic 主题名
 * @return 0成功，-1失败
 */
int mqtt_unsubscribe(MqttClient *client, const char *topic)
{
    if (!client || !client->internal || !topic) return -1;
    
    if (!mqtt_is_connected(client)) {
        return -1;
    }
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    int rc = MQTTClient_unsubscribe(internal->mqtt_client, topic);
    return (rc == MQTTCLIENT_SUCCESS) ? 0 : -1;
}

/**
 * @brief 设置连接成功回调
 */
void mqtt_on_connected(MqttClient *client, void (*callback)(MqttClient *))
{
    if (client) client->on_connected = callback;
}

/**
 * @brief 设置断开连接回调
 */
void mqtt_on_disconnected(MqttClient *client, void (*callback)(MqttClient *))
{
    if (client) client->on_disconnected = callback;
}

/**
 * @brief 设置消息接收回调
 */
void mqtt_on_message(MqttClient *client, 
                     void (*callback)(MqttClient *, const char *, const void *, size_t))
{
    if (client) client->on_message = callback;
}

/**
 * @brief 设置状态变化回调
 */
void mqtt_on_state_changed(MqttClient *client, void (*callback)(MqttClient *, MqttState))
{
    if (client) client->on_state_changed = callback;
}

/**
 * @brief 启动MQTT客户端
 * 
 * 连接Broker并启动后台线程。
 * 
 * @param client MQTT客户端指针
 * @return 0成功，-1失败
 */
int mqtt_start(MqttClient *client)
{
    if (!client || !client->internal) return -1;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    // 检查是否已启动
    if (internal->running) return 0;
    
    // 尝试连接
    if (mqtt_connect(client) != 0) {
        // 连接失败，但启动后台线程进行重连
        log_warn("MQTT initial connection failed, will retry in background");
    }
    
    internal->running = 1;
    internal->last_reconnect_time = time(NULL);
    
    // 创建后台线程
    if (pthread_create(&internal->thread, NULL, mqtt_thread_func, client) != 0) {
        log_error("Failed to create MQTT thread");
        internal->running = 0;
        return -1;
    }
    
    log_info("MQTT client started");
    return 0;
}

/**
 * @brief 停止MQTT客户端
 * 
 * 停止后台线程并断开连接。
 * 
 * @param client MQTT客户端指针
 */
void mqtt_stop(MqttClient *client)
{
    if (!client || !client->internal) return;
    
    MqttInternal *internal = (MqttInternal *)client->internal;
    
    if (!internal->running) {
        // 即使没有运行，也需要断开连接
        mqtt_disconnect(client);
        return;
    }
    
    log_info("Stopping MQTT client...");
    
    // 停止后台线程
    internal->running = 0;
    pthread_join(internal->thread, NULL);
    
    // 断开连接
    mqtt_disconnect(client);
    
    log_info("MQTT client stopped");
}
