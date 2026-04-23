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

## 3. 配置说明

顶层清单：`<project-root>/gateway.ini`

实际配置拆分到：
- `config/transport.ini`
- `config/transport_physical.ini`
- `config/runtime.ini`
- `config/router.ini`
- `config/persistence.ini`
- `config/device.ini`
- `config/network.ini`

### 3.1 传输配置
在 `config/transport.ini` 中设置 `[transport]` / `[mqtt]` / `[dds]`
在 `config/transport_physical.ini` 中设置串口/CAN/SPI/I2C 等物理接口参数

### 3.2 运行时与路由
- `config/runtime.ini`：线程池等运行时参数
- `config/router.ini`：路由缓冲区参数

### 3.3 设备与网络
- `config/device.ini`：串口设备列表与设备缓冲区
- `config/network.ini`：蓝牙模块运行参数（m_addr / net_id / baud_rate）

### 3.4 持久化
`config/persistence.ini` 包含 SQLite 数据库与队列参数

> 多蓝牙模块场景：每个蓝牙模块对应一个串口设备，把所有串口写到 `serial_devices`（逗号分隔）即可并行接入。

---

## 4. 无硬件联调（推荐）

### 4.1 创建虚拟设备节点
```bash
cd <project-root>
./scripts/create_virtual_nodes.sh 3 /tmp/gateway-vdev
```

按设备类型创建（轮询分配）：
```bash
./scripts/create_virtual_nodes.sh 6 /tmp/gateway-vdev /tmp/gateway-vnodes-map.tsv --types ble_mesh,lora
```

脚本会输出 `gw` 端口列表，把它写进 `config/device.ini` 的 `[device].serial_devices`。
并生成映射文件（列：`index type_name type_code gw_port sim_port socat_pid`），便于你按类型统计。

### 4.2 模拟下位机发消息
```bash
python3 <project-root>/scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 10 \
  --interval 1
```

可选：模拟 AT 指令 ACK
```bash
python3 <project-root>/scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/sim0 \
  --ack-at --count 0
```

### 4.3 监控串口数据
```bash
./scripts/monitor_virtual_port.sh /tmp/gateway-vdev/gw0
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

数据库路径在 `config/persistence.ini` 的 `[persistence].db_path`。

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
├── config/
│   ├── transport.ini
│   ├── transport_physical.ini
│   ├── runtime.ini
│   ├── router.ini
│   ├── persistence.ini
│   ├── device.ini
│   └── network.ini
├── test/
├── gateway.ini
├── scripts/
│   ├── create_virtual_nodes.sh
│   ├── simulate_lower_device.py
│   ├── monitor_virtual_port.sh
│   └── read_gateway_db.py
```
