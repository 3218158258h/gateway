┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐    ┌─────────┐
│  设备   │───►│ 后台线程 │───►│ 协议解析 │───►│ 缓冲区  │───►│ 接收任务 │───►│  云端   │
│ (硬件)  │    │ (读取)  │    │ (转换)  │    │ (缓存)  │    │ (处理)  │    │ (MQTT)  │
└─────────┘    └─────────┘    └─────────┘    └─────────┘    └─────────┘    └─────────┘
     │              │              │              │              │              │
   原始数据       read()        post_read     buffer_write   recv_task    mqtt_publish
   字节流        阻塞读取       帧解析         环形缓冲       业务处理      网络发送

### 缓冲区模块
初始状态：
+---+---+---+---+---+---+---+---+
|   |   |   |   |   |   |   |   |
+---+---+---+---+---+---+---+---+
  ^
start=0, len=0

写入"ABCD"后：
+---+---+---+---+---+---+---+---+
| A | B | C | D |   |   |   |   |
+---+---+---+---+---+---+---+---+
  ^               ^
start=0         写入位置=4, len=4

读取2字节后：
+---+---+---+---+---+---+---+---+
|   |   | C | D |   |   |   |   |
+---+---+---+---+---+---+---+---+
          ^
start=2, len=2

继续写入"EFGHIJ"（环绕）：
+---+---+---+---+---+---+---+---+
| G | H | C | D | E | F |   |   |
+---+---+---+---+---+---+---+---+
          ^               ^
start=2         写入位置=2, len=6

**写入逻辑**
情况1：不需要环绕
          write_offset
               ↓
+---+---+---+---+---+---+---+---+
|   |   | X | X | X | X |   |   |
+---+---+---+---+---+---+---+---+
  ^                   ^
start               写入4字节

情况2：需要环绕
          write_offset
               ↓
+---+---+---+---+---+---+---+---+
| X | X |   |   |   |   | X | X |
+---+---+---+---+---+---+---+---+
  ^                   ^
写入2字节           start

### 线程池任务模块
                    +--------------+
register() --------►│    Pipe      │
  写入TaskStruct    │  [0]  [1]    │
                    +------+-------+
                           │ read()
          +----------------+----------------+
          ↓                ↓                ↓
    +----------+    +----------+    +----------+
    |Executor 1|    |Executor 2|    |Executor N|
    | pthread  |    | pthread  |    | pthread  |
    +----------+    +----------+    +----------+
          │                │                │
          +----------------+----------------+
                           │
                      执行task()

### 消息模块
- 实现二进制与JSON的转换
+-------------+-------------+-------------+--------------+--------------+
| Connection  |   ID Len    |  Data Len   |   Device ID  |     Data     |
|   (1 byte)  |  (1 byte)   |  (1 byte)   │ (id_len)     │ (data_len)   │
+-------------+-------------+-------------+--------------+--------------+
     byte 0        byte 1       byte 2      byte 3~3+id_len-1   剩余字节

### 路由模块
设备数据流（上行）:
+---------+    +---------+    +---------+    +---------+    +---------+
| Device  |--->| Buffer  |--->| Message |--->| Router  |--->| Cloud   |
| (串口)  |    | (环形)  |    | (解析)  |    | (路由)  |    | (MQTT)  |
+---------+    +---------+    +---------+    +---------+    +---------+

云端数据流（下行）:
+---------+    +---------+    +---------+    +---------+    +---------+
| Cloud   |--->| Router  |--->| Message |--->| Buffer  |--->| Device  |
| (MQTT)  |    | (路由)  |    | (解析)  |    | (环形)  |    | (串口)  |
+---------+    +---------+    +---------+    +---------+    +---------+

### 守护进程模块
+-------------------------------------------------------------+
|                    daemon_runner (守护进程)                  |
|                         pid = 1                              |
|  +-----------------------------------------------------+   |
|  |                    主循环                             |   |
|  |  while(is_running) {                                 |   |
|  |      pid = waitpid(-1, &status, WNOHANG);            |   |
|  |      if (子进程退出) {                                |   |
|  |          重启子进程;                                  |   |
|  |      }                                               |   |
|  |  }                                                   |   |
|  +-----------------------------------------------------+   |
+---------------------------+---------------------------------+
                            │ fork()
          +-----------------+-----------------+
          ▼                                   ▼
+-------------------------+         +-------------------------+
|   app (主应用)           |         |   ota (OTA更新)         |
|   pid = 100             |         |   pid = 101             |
|                         |         |                         |
|   RouterManager         |         |   OtaManager            |
|   TransportManager      |         |   定期检查更新           |
|   Device[]              |         |                         |
+-------------------------+         +-------------------------+

## 整体架构
+-------------------------------------------------------------+
|                      应用层                    |
|  +-------------+  +-------------+  +-------------+         |
|  | app_runner  |  |daemon_runner|  | ota_runner  |         |
|  +-------------+  +-------------+  +-------------+         |
+-------------------------------------------------------------+
                            │
+-------------------------------------------------------------+
|                      路由层                         |
|  +-----------------------------------------------------+   |
|  |                  RouterManager                       |   |
|  |  - 设备管理                                          |   |
|  |  - 消息路由                                          |   |
|  |  - 统计信息                                          |   |
|  +-----------------------------------------------------+   |
+-------------------------------------------------------------+
                            │
          +-----------------+-----------------+
          ▼                                   ▼
+-------------------------+         +-------------------------+
|    传输抽象层        |         |    设备抽象层        |
| (Transport Layer)   |         |  (Device Layer)     |
| +--------++--------+|         | +--------++--------+|       |
| │ MQTT   ││  DDS   │|         | │ Serial ││Bluetooth||       |
| │ Client ││Manager │|         | │ Device ││ Device ||       |
| +--------++--------+|         | +--------++--------+|       |
+-------------------------+         +-------------------------+
          │                                   │
          ▼                                   ▼
+-------------------------+         +-------------------------+
|    网络层  |         |   硬件层 │
| - TCP/IP            |         | - 串口              │
| - MQTT Broker       |         | - USB设备           │
| - DDS Domain        |         | - 蓝牙模块          │
+-------------------------+         +-------------------------+


## 模块关系
main.c
  │
  ├── app_runner_run()
  │     │
  │     ├── app_task_init(4)              // 初始化任务调度
  │     │
  │     ├── app_router_init()
  │     │     │
  │     │     ├── config_init()           // 加载配置
  │     │     │     └── config_load()
  │     │     │
  │     │     ├── transport_init()        // 初始化传输层
  │     │     │     ├── mqtt_init()
  │     │     │     └── dds_init()
  │     │     │
  │     │     └── persistence_init()      // 初始化持久化
  │     │
  │     ├── app_router_register_device()  // 注册设备
  │     │     └── app_device_start()
  │     │
  │     └── app_router_start()            // 启动路由
  │           └── transport_connect()
  │
  ├── daemon_runner_run()
  │     │
  │     ├── daemon()                      // 创建守护进程
  │     │
  │     ├── daemon_process_init()         // 初始化子进程
  │     │
  │     ├── daemon_process_start()        // 启动子进程
  │     │     └── fork() + execve()
  │     │
  │     └── waitpid()                     // 监控子进程
  │
  └── ota_runner_run()
        │
        ├── ota_init()                    // 初始化OTA
        │
        └── ota_upgrade()                 // 执行升级
              ├── ota_check_update()
              ├── ota_download()
              ├── ota_verify()
              └── ota_install()


## 启动流程
1. 执行 ./gateway app
   │
   ▼
2. app_task_init(4)
   - 创建管道
   - 创建4个工作线程
   │
   ▼
3. app_router_init()
   │
   ├── config_init() + config_load()
   │   - 加载 /etc/gateway/gateway.ini
   │   - 解析配置项到 ConfigManager.items[]
   │
   ├── transport_init()
   │   ├── mqtt_init()
   │   │   - 创建 Paho MQTT 客户端
   │   │   - 设置回调函数
   │   │
   │   └── dds_init()
   │       - 创建 DDS Participant
   │       - 注册话题
   │
   └── persistence_init()
       - 打开 SQLite 数据库
       - 创建消息表
   │
   ▼
4. 初始化设备
   │
   ├── app_serial_init(&serial_device, "/dev/ttyUSB0")
   │   - 打开串口设备
   │   - 配置波特率、数据位等
   │   - 初始化缓冲区
   │
   └── app_bluetooth_setConnectionType(&serial_device)
       - 设置蓝牙协议处理函数
       - 配置蓝牙模块
   │
   ▼
5. 注册设备到路由器
   app_router_register_device(&router, &serial_device.super)
   - 设置设备接收回调
   │
   ▼
6. 启动设备
   app_device_start(&serial_device.super)
   - 创建后台读取线程
   │
   ▼
7. 启动传输层
   app_router_start(&router)
   │
   ├── mqtt_start()
   │   - mqtt_connect()
   │   - 创建后台线程 (mqtt_loop)
   │
   └── transport_subscribe_default()
       - 订阅命令话题
   │
   ▼
8. 主循环
   while (running) {
       usleep(100000);
       // 消息处理由回调完成
   }

## 数据流转
### cmd
1. 云端发送消息
   │
   │   MQTT: Broker → Paho回调
   │   DDS:  DDS Reader回调
   │
   ▼
2. 传输层回调 (on_message)
   │
   │   mqtt_message_callback() 或 dds_data_callback()
   │
   ▼
3. 转发到路由层
   │
   │   manager->on_message(manager, topic, payload, len)
   │
   ▼
4. 路由层处理 (router_on_cloud_message)
   │
   │   - 解析JSON消息
   │   - 提取设备ID
   │   - 查找目标设备
   │
   ▼
5. 转换为二进制格式
   │
   │   app_message_saveBinary(&msg, binary, sizeof(binary))
   │
   ▼
6. 写入设备发送缓冲区
   │
   │   app_device_write(device, binary, binary_len)
   │   │
   │   ├── app_buffer_write(device->send_buffer, ptr, len)
   │   │
   │   └── app_task_register(device->vptr->send_task, device)
   │
   ▼
7. 工作线程执行发送任务 (send_task)
   │
   │   - 从缓冲区读取消息
   │   - 调用协议封装函数
   │
   ▼
8. 协议封装 (pre_write)
   │
   │   例如蓝牙AT指令封装：
   │   - 构造 AT+MESH 指令
   │   - 添加设备ID和数据
   │
   ▼
9. 写入设备
   │
   │   write(device->fd, buf, buf_len)
   │
   ▼
10. 设备接收数据


### data
1. 设备发送数据
   │
   ▼
2. 后台线程读取 (background_task)
   │
   │   read(device->fd, buf, 1024)
   │
   ▼
3. 协议解析 (post_read)
   │
   │   例如蓝牙帧解析：
   │   - 查找帧头 F1 DD
   │   - 解析长度字段
   │   - 提取设备ID和数据
   │   - 转换为标准消息格式
   │
   ▼
4. 写入接收缓冲区
   │
   │   app_buffer_write(device->recv_buffer, buf, buf_len)
   │
   ▼
5. 注册接收任务
   │
   │   app_task_register(device->vptr->recv_task, device)
   │
   ▼
6. 工作线程执行接收任务 (recv_task)
   │
   │   - peek消息头
   │   - 检查完整消息
   │   - 读取完整消息
   │
   ▼
7. 调用用户回调 (recv_callback)
   │
   │   - 解析消息
   │   - 转换为JSON格式
   │
   ▼
8. 路由到传输层
   │
   │   app_router_send_to_cloud(router, topic, data, len)
   │
   ▼
9. 发布到云端
   │
   │   MQTT: mqtt_publish() → Paho库 → Broker
   │   DDS:  dds_publish()  → Cyclone DDS → Subscriber
