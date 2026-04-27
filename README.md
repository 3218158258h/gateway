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

## 1. 构建

仓库根目录：`<project-root>`

### 1.1 标准构建（默认启用 DDS）
```bash
cd <project-root>
make
```

如 Cyclone DDS 安装路径不是默认值，可显式指定：
```bash
cd <project-root>
make DDS_HOME=/path/to/cyclonedds/install
```

---

## 2. 启动

```bash
cd <project-root>
./build/bin/gateway app
```

---

## 3. 配置说明

顶层清单：`<project-root>/gateway.ini`

实际配置拆分到：
- `config/transport.ini`
- `config/transport_physical.ini`
- `config/protocols.ini`
- `config/daemon.ini`
- `gateway.ini`

### 3.1 传输配置
在 `config/transport.ini` 中设置 `[transport]` / `[mqtt]` / `[dds]`
在 `config/transport_physical.ini` 中设置串口/CAN/SPI/I2C 等物理接口参数

### 3.2 运行时与路由
- `gateway.ini` 的 `[runtime]`：线程池等运行时参数
- `gateway.ini` 的 `[router]`：路由缓冲区参数

### 3.3 设备与蓝牙
- `gateway.ini` 的 `[device]`：串口设备列表与设备缓冲区
- `gateway.ini` 的 `[bluetooth]`：蓝牙模块运行参数（m_addr / net_id / baud_rate）

### 3.4 持久化
`gateway.ini` 的 `[persistence]` 包含 SQLite 数据库与队列参数

### 3.5 守护进程
`config/daemon.ini` 包含守护进程参数：
- 子进程二进制路径（`program_path`）
- 日志重定向路径（`log_file`）
- 崩溃阈值与重启退避策略（`max_crash_count`/`restart_backoff_*`）
- 正常退出是否重启（`restart_on_normal_exit`）

> 多蓝牙模块场景：每个蓝牙模块对应一个串口设备，把所有串口写到 `serial_devices`（逗号分隔）即可并行接入。

---

## 4. 无硬件联调（推荐）

### 4.1 创建虚拟设备节点
```bash
cd <project-root>
./scripts/create_virtual_uart_nodes.sh 3 /tmp/gateway-vdev
```

按设备类型创建（轮询分配）：
```bash
./scripts/create_virtual_uart_nodes.sh 6 /tmp/gateway-vdev /tmp/gateway-vnodes-uart-map.tsv --types ble_mesh,lora
```

脚本会输出 `uart-gw` 端口列表，把它写进 `gateway.ini` 的 `[device].serial_devices`。
同时会生成兼容软链 `gwN/simN`，兼容旧配置与旧工具。
映射文件列：`index type_name type_code gw_port sim_port socat_pid`。

可选：创建 I2C/SPI 风格的虚拟节点（用于接口路径联调）
```bash
./scripts/create_virtual_i2c_nodes.sh 2 /tmp/gateway-i2c-vdev
./scripts/create_virtual_spi_nodes.sh 2 /tmp/gateway-spi-vdev
```
说明：
- 两个脚本默认 `auto` 模式：优先复用真实 `/dev/i2c-*`、`/dev/spidev*`，在目标目录创建 `i2c-gwN`/`spi-gwN` 软链。
- 若真实设备不足，会自动降级为 PTY 对（`i2c-gwN <-> i2c-simN`、`spi-gwN <-> spi-simN`），仅用于字节流联调。
- 可通过第 4 个参数强制模式：`real` 或 `pseudo`。
示例：
```bash
./scripts/create_virtual_i2c_nodes.sh 2 /tmp/gateway-i2c-vdev /tmp/gateway-vnodes-i2c-map.tsv real
./scripts/create_virtual_spi_nodes.sh 2 /tmp/gateway-spi-vdev /tmp/gateway-vnodes-spi-map.tsv pseudo
```

可选：创建 CAN 虚拟接口（基于 Linux `vcan`）
```bash
./scripts/create_virtual_can_nodes.sh 2 vcan /tmp/gateway-vnodes-can-map.tsv
```
说明：CAN 脚本创建并拉起 `vcan` 设备（如 `vcan0/vcan1`），可配合 `cansend/candump` 做链路联调。

### 4.2 模拟下位机发消息
```bash
python3 <project-root>/scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/uart-sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 10 \
  --interval 1
```

可选：模拟 AT 指令 ACK
```bash
python3 <project-root>/scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/uart-sim0 \
  --ack-at --count 0
```

### 4.3 监控串口数据
```bash
./scripts/monitor_virtual_uart_port.sh /tmp/gateway-vdev/uart-gw0
```

### 4.4 DDS 下行命令发布（按设备类型）
先编译测试发布者：
```bash
cd <project-root>
make -C test
```

按单一设备类型发送（支持别名：`ble_mesh|lora|none`）：
```bash
<project-root>/test/publisher --device-type ble_mesh --count 100 --interval-ms 200
```

按多类型循环发送：
```bash
<project-root>/test/publisher --type-seq ble_mesh,lora --count 100 --interval-ms 200
```

> 注意：网关当前下行路由按 `connection_type` 匹配设备，不按消息 `id` 匹配。

---

## 5. 数据库查看（SQLite）

数据库路径在 `gateway.ini` 的 `[persistence].db_path`。

```bash
sqlite3 <db_path_from_gateway_ini>
```

常用命令：
```sql
.tables
.schema
SELECT COUNT(*) FROM messages;
SELECT id, topic, status, retry_count, created_at FROM messages ORDER BY id DESC LIMIT 20;
```

也可以使用项目内置 Python 脚本（使用 Python 内置 sqlite3）：
```bash
cd <project-root>
python3 scripts/read_gateway_db.py
# 或指定数据库路径
python3 scripts/read_gateway_db.py --db <db_path_from_gateway_ini> --limit 50
```

---

## 6. 常见问题

### Q1: `transport.type=dds` 启动失败
通常是 Cyclone DDS 库路径配置不正确（检查 `DDS_HOME`、`LD_LIBRARY_PATH` 或系统库路径）。

### Q2: 启动日志太多
当前已进一步精简高频日志：
- 不再打印缓冲区读写剩余长度跟踪日志
- 仅在缓冲区创建失败时打印“第几个创建失败”
- 高频发布/持久化日志下调到 TRACE，默认 INFO 级别下不会刷屏

### Q3: 为什么某个设备启动失败后网关还能继续运行？
这是预期行为。当前启动策略为“部分成功可运行”：  
只要至少有一个设备成功初始化并配置，网关会继续提供服务；失败设备会记录在 `[device]` 日志里。

### Q4: 如何快速定位传输层异常？
优先看 `[health]` 日志，关注以下字段：
- `connect_attempts / connect_failures`
- `publish_attempts / publish_failures`
- `subscribe_attempts / subscribe_failures`
- `last_error_stage / last_error_code`

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

- `gateway.ini`：总配置入口，聚合运行时、设备、持久化及分层配置文件路径。
- `config/transport.ini`：MQTT/DDS 逻辑传输参数。
- `config/transport_physical.ini`：物理链路参数（串口/CAN/SPI/I2C 等扩展位）。
- `config/protocols.ini`：私有协议定义（帧头帧尾、ACK/NACK、初始化指令等）。
- `config/daemon.ini`：守护进程配置（子进程路径、重启阈值、退避、日志路径）。

### 7.4 脚本与测试

- `scripts/create_virtual_nodes.sh`：创建虚拟串口节点（`gw/sim` 对）。
- `scripts/create_virtual_uart_nodes.sh`：创建虚拟 UART 节点（`uart-gw/uart-sim`，并兼容 `gw/sim` 软链）。
- `scripts/create_virtual_i2c_nodes.sh`：创建 I2C 节点（优先 real `/dev/i2c-*`，不足时回退 PTY）。
- `scripts/create_virtual_spi_nodes.sh`：创建 SPI 节点（优先 real `/dev/spidev*`，不足时回退 PTY）。
- `scripts/create_virtual_can_nodes.sh`：创建虚拟 CAN 接口（Linux vcan）。
- `scripts/simulate_lower_device.py`：模拟下位机上报与 AT 响应。
- `scripts/monitor_virtual_uart_port.sh`：监听虚拟 UART 端口。
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
