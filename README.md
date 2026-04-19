# gateway

智能网关服务，负责：
- 从串口设备接收二进制数据并转发到云端（MQTT / DDS）
- 接收云端下发指令并转发到设备
- 本地持久化消息到 SQLite

---

## 1. 构建

仓库根目录：`<project-root>`

### 1.1 普通构建（不启用DDS）
```bash
cd <project-root>
make
```

### 1.2 启用DDS构建
```bash
cd <project-root>
make USE_DDS=1 DDS_HOME=/path/to/cyclonedds/install
```

> 说明：如果配置里使用 `transport.type=dds`，必须用 `USE_DDS=1` 重新构建，否则启动会失败并提示未编译DDS支持。

---

## 2. 启动

```bash
cd <project-root>
./build/bin/gateway app
```

---

## 3. 配置说明（gateway.ini）

配置文件路径：`<project-root>/gateway.ini`

### 3.1 选择传输类型
在 `[transport]` 中设置：
- `type = mqtt`：走 MQTT
- `type = dds`：走 DDS（要求二进制已开启DDS编译）
- `type = auto`：自动选择可用配置

### 3.2 MQTT 必填项（type=mqtt 时）
`[mqtt]` 里至少配置：
- `server`
- `client_id`
- `publish_topic`
- `subscribe_topic`
- `keepalive`

### 3.3 DDS 必填项（type=dds 时）
`[dds]` 里至少配置：
- `domain_id`
- `participant_name`
- `publish_topic`
- `publish_type`
- `subscribe_topic`
- `subscribe_type`

### 3.4 设备串口配置
在 `[device]` 中配置：
- `serial_devices = /dev/ttyUSB0,/dev/ttyUSB1`
- `buffer_size = 16384`

> 多蓝牙模块场景：每个蓝牙模块对应一个串口设备，把所有串口写到 `serial_devices`（逗号分隔）即可并行接入。

---

## 4. 无硬件联调（推荐）

### 4.1 创建虚拟设备节点
```bash
cd <project-root>
./create_virtual_nodes.sh 3 /tmp/gateway-vdev
```

脚本会输出 `gw` 端口列表，把它写进 `gateway.ini` 的 `[device].serial_devices`。

### 4.2 模拟下位机发消息
```bash
python3 <project-root>/simulate_lower_device.py \
  --port /tmp/gateway-vdev/sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 10 \
  --interval 1
```

可选：模拟 AT 指令 ACK
```bash
python3 <project-root>/simulate_lower_device.py \
  --port /tmp/gateway-vdev/sim0 \
  --ack-at --count 0
```

### 4.3 监控串口数据
```bash
./monitor_virtual_port.sh /tmp/gateway-vdev/gw0
```

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
python3 read_gateway_db.py
# 或指定数据库路径
python3 read_gateway_db.py --db <db_path_from_gateway_ini> --limit 50
```

---

## 6. 常见问题

### Q1: `transport.type=dds` 启动失败
通常是二进制未开启 DDS 编译（未使用 `make USE_DDS=1`）。

### Q2: 启动日志太多
当前已进一步精简高频日志：
- 不再打印缓冲区读写剩余长度跟踪日志
- 仅在缓冲区创建失败时打印“第几个创建失败”
- 高频发布/持久化日志下调到 TRACE，默认 INFO 级别下不会刷屏

---

## 7. 项目目录（核心）

```text
gateway/
├── src/
│   ├── app_runner.c
│   ├── app_router.c
│   ├── app_device.c
│   ├── app_serial.c
│   ├── app_bluetooth.c
│   ├── dds/
│   └── mqtt/
├── include/
├── test/
├── gateway.ini
├── create_virtual_nodes.sh
├── simulate_lower_device.py
└── monitor_virtual_port.sh
```
