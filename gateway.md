# Gateway 架构说明（对齐当前代码）

本文档描述当前 `gateway` 的真实分层、启动流程、数据链路与关键设计点。  
与历史版本相比，已对齐以下改动：
- 启动配置快照日志（`[snapshot]`）
- 设备“部分失败可运行”策略
- 非默认协议严格校验
- 传输层健康诊断（`[health]`）
- 设备回调去全局化（移除 `g_router`）

---

## 1. 总体分层

1. 应用编排层  
- `main.c`、`app_runner.c`、`daemon/*`、`ota/*`  
- 负责进程入口、初始化编排、生命周期控制。

2. 路由层  
- `app_router.c`  
- 负责设备侧与云侧消息转发、设备注册管理、传输层回调承接。

3. 设备层（生命周期）  
- `app_device_layer.c`、`app_link_adapter.c`  
- 负责设备初始化、协议配置、启动/停止、运行态切换。

4. 协议层（私有协议）  
- `app_protocol_config.c`、`app_private_protocol.c`、`app_bluetooth.c`  
- 负责组帧/拆帧、ACK/NACK 识别、协议模板展开、初始化命令执行。

5. 传输层（云通信）  
- `dds/app_transport.c`、`mqtt/app_mqtt_v2.c`、`dds/app_dds.c`  
- 统一 MQTT / DDS 的连接、发布、订阅、状态回调与健康计数。

6. 公共基础模块  
- `app_task.c` 线程池  
- `app_buffer.c` 环形缓冲  
- `app_message.c` 二进制与 JSON 转换  
- `app_persistence.c` SQLite 持久化  
- `app_config.c` 配置加载

---

## 2. 核心对象关系

- `RouterManager`
  - 持有 `TransportManager`
  - 持有 `Device* devices[]`
  - 管理设备回调上下文 `device_callback_ctx[]`
  - 提供路由统计与状态回调

- `TransportManager`
  - 统一 MQTT/DDS 接口
  - 维护 `TransportHealth`（连接/发布/订阅计数、错误阶段、状态变化）

- `Device / SerialDevice`
  - 后台读线程 + 收发缓冲区 + 任务回调
  - 通过 `DeviceRecvCallback(context, ptr, len)` 回调到路由层

---

## 3. 启动流程（app 模式）

1. `app_runner_run()` 启动，加载 `gateway.ini`。  
2. 读取运行时参数、设备列表、持久化配置。  
3. 输出启动快照：`[snapshot] event=startup_config / device_config`。  
4. 初始化线程池与持久化。  
5. 逐设备执行：
- `app_device_layer_init()`
- `app_device_layer_configure()`
- 失败设备跳过并记录，不影响其他设备。  
6. 初始化路由器：`app_router_init_with_config()`。  
7. 注册可用设备到路由器。  
8. 启动路由器：连接传输层、订阅下行主题、启动设备。  
9. 进入主循环等待停止信号。  
10. 停止阶段输出 `[health]`，再依次停止路由、清理持久化、关闭线程池。

---

## 4. 数据链路

### 4.1 上行（设备 -> 云端）

1. 设备字节流由 `app_device` 后台线程读取。  
2. `post_read` 做协议解帧（如蓝牙私有帧）。  
3. 写入接收缓冲区并投递 `recv_task`。  
4. `recv_task` 组装完整消息后回调路由层。  
5. 路由层将二进制转 JSON。  
6. 通过 `transport_publish_default()` 发到 MQTT/DDS。  
7. 可选落库：`on_device_message_persist()` 写入 SQLite（含 QoS）。

### 4.2 下行（云端 -> 设备）

1. MQTT/DDS 收到下行命令。  
2. 传输层回调 `on_transport_message()` 到路由层。  
3. 路由层将 JSON 转二进制。  
4. 根据 `connection_type` 选设备。  
5. 写入设备发送缓冲区并投递 `send_task`。  
6. `pre_write` 组帧后写入串口设备。

---

## 5. 配置体系

- `gateway.ini`：总配置入口（运行时、设备、持久化、配置文件路径）。  
- `config/transport.ini`：MQTT/DDS 逻辑传输参数。  
- `config/transport_physical.ini`：物理接口参数扩展位。  
- `config/protocols.ini`：私有协议定义（帧头/尾、ACK/NACK、init_cmds）。
- `config/daemon.ini`：守护进程参数（子进程路径、日志路径、崩溃阈值、重启退避）。

设计原则：
- 总配置负责“启哪些模块、用哪份分层配置”。
- 分层配置负责“各层细节参数”。

---

## 6. 关键设计决策

1. 设备容错启动  
- 多设备场景中，单设备失败不会阻断全局。  
- 仅当“无可用设备”才失败退出。

2. 协议严格模式  
- 默认协议允许兼容兜底。  
- 非默认协议启用严格校验，解析/校验失败直接拒绝。

3. 回调上下文化  
- 回调签名升级为 `(context, ptr, len)`。  
- 每设备独立上下文，避免全局指针耦合与生命周期风险。

4. 传输健康可观测  
- 计数连接/发布/订阅尝试与失败。  
- 记录最后错误阶段/错误码。  
- 通过 `[health]` 日志快速定位失败段。

5. 结构化日志统一  
- 关键链路统一 `event=... key=value`，便于检索与压测对比。

---

## 7. 常用排障入口

1. 看启动快照  
- 关注 `[snapshot]`：配置是否按预期加载。

2. 看设备启动汇总  
- 关注 `[device] event=bootstrap_summary`：成功/失败设备数量。

3. 看协议应用失败点  
- 关注 `[protocol] event=*`：加载失败、绑定失败、命令失败的具体阶段。

4. 看传输层健康  
- 关注 `[health]`：
  - `connect_failures`
  - `publish_failures`
  - `subscribe_failures`
  - `last_error_stage`

---

## 8. 相关文档

- `README.md`：快速使用与目录说明  
- `README2.0.md`：生产测试门禁与验收指标  
- `problem.md`：本轮问题与修复细节  
- `迭代计划.md`：优化清单状态  
- `count.md`：压测记录模板

---

## 9. 联调脚本命名约定（2026-04 更新）

- UART 虚拟节点：`scripts/create_virtual_uart_nodes.sh`
  - 节点命名：`uart-gwN / uart-simN`
  - 兼容软链：`gwN / simN`
- I2C 虚拟节点：`scripts/create_virtual_i2c_nodes.sh`
  - 节点命名：`i2c-gwN / i2c-simN`
- SPI 虚拟节点：`scripts/create_virtual_spi_nodes.sh`
  - 节点命名：`spi-gwN / spi-simN`
- CAN 虚拟接口：`scripts/create_virtual_can_nodes.sh`
  - 接口命名：`<prefix><index>`（默认 `vcan0/vcan1/...`）

说明：
- I2C/SPI 脚本生成的是“字节流模拟节点（PTY）”，用于网关应用层联调。
- 若要验证真实内核 `i2c-dev/spidev` ioctl 行为，需使用目标板内核驱动和真实总线设备。
- CAN 脚本使用 Linux `vcan`，更接近真实 CAN socket 使用方式（需内核支持 vcan）。
