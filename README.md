# gateway

智能网关服务，负责：
- 从串口设备接收二进制数据并转发到云端（MQTT / DDS）
- 接收云端下发指令并转发到设备
- 本地持久化消息到 SQLite

本版本已完成低风险架构优化中的关键项（1/2/4/5/7/9），重点增强：
- 启动配置快照（`[snapshot]`）
- 设备部分失败隔离启动（单设备失败不再拖垮全局）
- 协议严格校验（非默认协议）
- 传输健康状态诊断（`[health]`）
- 设备回调去全局化（移除全局 `g_router` 依赖）
- 结构化日志统一（`event=... key=value`）

术语约定（与 `gateway.md` 保持一致）：
- 应用编排层：进程入口与启动编排（`main/app_runner/daemon/ota`）
- 路由层：设备侧与云侧转发（`app_router`）
- 设备层：设备生命周期与收发抽象（`app_device_layer + app_device + app_serial`）
- 协议层：私有协议组帧/拆帧/ACK（`app_protocol_config + app_private_protocol + app_bluetooth`）
- 传输层：云通信（MQTT/DDS，`app_transport + mqtt/dds`）

---

## 1. 快速开始（3 分钟跑通）

仓库根目录：`<project-root>`

### 1.1 构建
```bash
cd <project-root>
make
```

如 Cyclone DDS 安装路径不是默认值：
```bash
make DDS_HOME=/path/to/cyclonedds/install
```

### 1.2 启动
```bash
./build/bin/gateway app
```

### 1.3 看到这些日志说明主流程正常
- `Runtime config loaded`
- `[snapshot] event=startup_config`
- `[device] event=bootstrap_summary`
- `Router initialized`
- `Transport initialized`

---

## 2. 配置总览（先看这 5 个文件）

- `gateway.ini`：总入口，定义运行时、路由、持久化、外部配置文件路径
- `config/transport.ini`：云侧传输（MQTT / DDS）参数
- `config/transport_physical.ini`：物理接口参数与设备注册表
- `config/protocols.ini`：私有协议定义（仅在需要私有协议时启用）
- `config/daemon.ini`：守护进程参数

推荐顺序：
1. 先改 `config/transport_physical.ini` 的 `[device_registry]`
2. 再改 `config/transport.ini` 的传输类型与主题
3. 最后按需改 `config/protocols.ini` 和 `gateway.ini` 的运行参数

---

## 3. 设备配置（最关键）

核心在 `config/transport_physical.ini` 的 `[device_registry]`：
- `device_paths`：设备节点列表
- `interfaces`：接口类型列表（`serial/spi/i2c/can`）
- `protocols`：私有协议名列表，可留空

三条规则：
1. 三个列表按顺序一一对应
2. 某设备不需要私有协议时，在 `protocols` 对应位置留空
3. `interfaces/protocols` 允许空槽位，系统会按默认值回退并给出日志

示例：
```ini
[device_registry]
device_paths = /tmp/gateway-vdev/uart-gw0,/tmp/gateway-vdev/uart-gw1,/dev/i2c-0
interfaces   = serial,serial,i2c
protocols    = ble_mesh_default,, 
```

---

## 4. 两种运行场景

### 4.1 场景 A：无硬件联调（推荐先用这个）

1. 创建 UART 虚拟节点：
```bash
./scripts/debug_uart_pseudo.sh 3 /tmp/gateway-vdev
```

2. 把生成的 `uart-gw*` 写入 `[device_registry].device_paths`

3. 启动网关：
```bash
./build/bin/gateway app
```

4. 模拟下位机上报：
```bash
python3 scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/uart-sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 10 \
  --interval 1
```

可选：
- I2C stub：`sudo ./scripts/debug_i2c_stub.sh 10 0x50 /tmp/gateway-debug-i2c-map.tsv`
- SPI mock：`sudo ./scripts/debug_spi_mock.sh spi-mockup /dev/spidev0.0 /tmp/gateway-debug-spi-map.tsv`
- CAN vcan：`./scripts/debug_can_vcan.sh 2 vcan /tmp/gateway-debug-can-map.tsv`

### 4.2 场景 B：真实设备

1. 在 `[device_registry].device_paths` 填真实节点（如 `/dev/ttyUSB0`、`/dev/i2c-0`、`can0`）
2. 在同序 `interfaces` 填对应接口类型
3. 串口设备如需私有协议，再在 `protocols` 填协议名；不需要就留空
4. 启动并观察 `[device] event=registered` 是否全部成功

---

## 5. 联调验证清单

### 5.1 上行验证（设备 -> 云）
- 看日志是否出现设备消息处理与 publish 成功
- 数据入库检查：
```bash
python3 scripts/read_gateway_db.py --limit 20
```

### 5.2 下行验证（云 -> 设备）
编译测试工具：
```bash
make -C test
```

DDS 发布测试命令：
```bash
./test/publisher --device-type ble_mesh --count 20 --interval-ms 200
```

说明：当前下行路由按 `connection_type` 匹配设备，不按消息 `id` 匹配。

---

## 6. 数据库排查

数据库路径：`gateway.ini` 的 `[persistence].db_path`

```bash
sqlite3 <db_path>
```

常用 SQL：
```sql
.tables
SELECT COUNT(*) FROM messages;
SELECT id, device_path, interface_name, protocol_family, protocol_name, qos, status, retry_count
FROM messages ORDER BY id DESC LIMIT 20;
```

---

## 7. 常见问题（高频）

### Q1：DDS 启动失败
通常是 Cyclone DDS 路径问题，检查 `DDS_HOME` 与系统库路径。

### Q2：设备部分失败但进程没退出
这是设计行为：只要至少有一个设备成功初始化，网关继续运行。

### Q3：日志里出现大量 `[health]`
这是传输层健康诊断日志，用于定位连接/发布/订阅失败，不建议直接关闭。

### Q4：如何快速判断配置是否生效
看启动快照日志：
- `[snapshot] event=startup_config`
- `[snapshot] event=device_config`
- `[snapshot] event=transport_config`

---

## 7. 项目目录（核心）

```text
gateway/
├── src/                         # 业务源码
│   ├── app_runner.c             # app 进程入口与启动编排
│   ├── app_router.c             # 设备/云端路由与转发
│   ├── app_device_layer.c       # 设备生命周期编排（初始化/配置/启动）
│   ├── app_device.c             # 设备层收发抽象与缓冲任务
│   ├── app_serial.c             # 串口实现
│   ├── app_bluetooth.c          # 蓝牙私有协议交互（AT/ACK/NACK）
│   ├── app_protocol_config.c    # 协议配置加载与校验
│   ├── app_private_protocol.c   # 组帧/拆帧/匹配规则
│   ├── app_persistence.c        # SQLite 持久化
│   ├── app_task.c               # 线程池任务调度
│   ├── app_config.c             # INI 配置读写
│   ├── app_message.c            # 二进制 <-> JSON 转换
│   ├── app_link_adapter.c       # 设备接口适配层
│   ├── dds/                     # DDS 传输实现
│   ├── mqtt/                    # MQTT 传输实现
│   ├── daemon/                  # 守护进程相关
│   └── ota/                     # OTA 升级相关
├── include/                     # 对外头文件与模块接口
├── config/
│   ├── transport.ini
│   ├── transport_physical.ini
│   ├── protocols.ini
│   └── daemon.ini
├── scripts/                     # 联调与运维脚本
├── test/                        # 协议链路测试程序
├── gateway.ini                  # 顶层总配置清单
├── Makefile                     # 主工程构建脚本
├── problem.md                   # 本轮问题修复记录
├── README2.0.md                 # 生产测试与发布门禁文档
└── gateway.md                   # 架构说明与数据流文档
```

### 7.1 `src/` 关键文件说明（按分层）

- `main.c`：主入口，分发 `app/daemon/ota` 子命令。
- `app_runner.c`：应用编排层入口；加载配置、初始化模块、启动主循环。
- `app_router.c`：路由层核心；设备与云端双向路由、设备注册与回调管理。
- `app_device_layer.c`：设备层编排；按运行参数与协议执行设备初始化/配置/启停。
- `app_device.c`：设备层收发抽象；后台读线程、缓冲区、任务回调。
- `app_protocol_config.c`：协议层配置加载与严格校验。
- `app_private_protocol.c`：协议层字节规则处理（组帧/拆帧/匹配）。
- `dds/app_transport.c`：传输层统一抽象（MQTT/DDS）与健康状态计数。
- `app_persistence.c`：持久化模块；消息落库、重试与清理。

### 7.2 `include/` 关键接口头文件

- `app_runner.h`：运行入口声明。
- `app_router.h`：路由器管理接口。
- `app_device.h`：设备层接口（含回调注册）。
- `app_device_layer.h`：设备生命周期编排接口。
- `app_protocol_config.h`：协议配置加载接口。
- `app_private_protocol.h`：协议层字节处理工具接口。
- `app_transport.h`：传输层接口与健康诊断结构。
- `app_persistence.h`：持久化接口。
- `app_config.h`：配置系统接口。

### 7.3 配置文件职责

- `gateway.ini`：总配置入口，聚合运行时、持久化及分层配置文件路径。
- `config/transport.ini`：MQTT/DDS 逻辑传输参数。
- `config/transport_physical.ini`：物理链路参数（串口/CAN/SPI/I2C 等扩展位）。
- `config/protocols.ini`：私有协议定义（帧头帧尾、ACK/NACK、初始化指令等）。
- `config/daemon.ini`：守护进程配置（子进程路径、重启阈值、退避、日志路径）。

### 7.4 脚本与测试

- `scripts/debug_uart_pseudo.sh`：创建 UART PTY 调试节点（`uart-gw/uart-sim`，并兼容 `gw/sim` 软链）。
- `scripts/debug_i2c_stub.sh`：创建 I2C stub 调试总线与从机地址。
- `scripts/debug_spi_mock.sh`：加载 SPI mock/stub 驱动并映射调试节点。
- `scripts/debug_can_vcan.sh`：创建 CAN `vcan` 调试接口。
- `scripts/debug_iface_cleanup.sh`：清理调试资源（UART/Can/I2C stub）。
- `scripts/simulate_lower_device.py`：模拟下位机上报与 AT 响应。
- `scripts/debug_uart_monitor.sh`：监听虚拟 UART 端口。
- `scripts/read_gateway_db.py`：读取 SQLite 消息库。
- `test/publisher.c`：测试命令发布。
- `test/sub_cmd.c`：测试命令订阅。
- `test/sub_data.c`：测试数据订阅。

### 7.5 文档索引

- `README.md`：快速使用、配置与联调入口。
- `README2.0.md`：生产测试、发布门禁与验收口径。
- `problem.md`：本轮架构优化问题与修复细节。
- `迭代计划.md`：阶段任务与完成状态。
- `count.md`：压测记录模板与观测项。
- `gateway.md`：架构图与数据流细节说明。
