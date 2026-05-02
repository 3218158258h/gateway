# Gateway 虚拟机测试手册

> 本文档用于在 Linux 虚拟机内完成网关功能测试与回归。  
> 原则：先跑通功能闭环，再做稳定性与故障恢复测试。

---

## 1. 测试目标与边界

### 1.1 可在虚拟机完成的测试
- 构建、启动、配置加载、守护进程流程
- UART 虚拟节点收发链路
- MQTT/DDS 上下行逻辑链路
- 路由封包、持久化入库、重启恢复
- CAN `vcan` 基础链路（如虚拟机内核支持）
- I2C/SPI stub/mock 链路（如虚拟机内核支持相关模块）

### 1.2 不等价于真实硬件的部分
- 真实总线电气特性与时序抖动
- 板级驱动异常、DMA/中断行为
- 长时间高负载下真实硬件稳定性

### 1.3 开发板 + 虚拟机半实物联调可行性
- 方案可行：开发板运行网关，PC 虚拟机通过 USB 转 UART/I2C/SPI 模块发消息模拟下位机。
- 推荐用途：功能联调、协议回归、异常注入。
- 关键前提：USB-I2C/SPI 模块必须在开发板上暴露为标准设备（如 `/dev/i2c-*`、`/dev/spidev*`）。
- 若模块只提供厂商私有协议接口而非标准设备节点，则不能直接复用本项目 I2C/SPI 链路。

---

## 2. 环境准备

## 2.1 依赖检查
```bash
cd <project-root>
gcc --version
make --version
python3 --version
socat -V
```

MQTT 模式需具备 MQTT 开发库，DDS 模式需具备 Cyclone DDS 头文件与库。

## 2.2 构建
```bash
cd <project-root>
make
make -C test
```

如 DDS 不在默认路径：
```bash
make DDS_HOME=/path/to/cyclonedds/install
```

---

## 3. 最小可运行配置（建议）

在 `config/transport_physical.ini` 的 `[device_registry]` 使用 2 个 UART 设备做起步：

```ini
[device_registry]
device_paths = /tmp/gateway-vdev/uart-gw0,/tmp/gateway-vdev/uart-gw1
interfaces   = serial,serial
protocols    = ble_mesh_default,
```

说明：
- 三个字段按顺序一一对应。
- `protocols` 空项表示该设备跳过私有协议初始化。

---

## 4. 功能测试流程（建议执行顺序）

## 4.1 UART 虚拟节点创建
```bash
cd <project-root>
./scripts/debug_uart_pseudo.sh 2 /tmp/gateway-vdev
```

确认节点存在：
```bash
ls /tmp/gateway-vdev
```

## 4.2 启动网关
```bash
./build/bin/gateway app
```

启动通过的关键日志：
- `[snapshot] event=startup_config`
- `[snapshot] event=device_config`
- `[device] event=bootstrap_summary`
- `Router initialized`
- `Transport initialized`

## 4.3 模拟下位机上报（上行）
```bash
python3 scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/uart-sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 200 \
  --interval 0.05
```

可选：模拟 AT ACK（用于协议初始化联调）
```bash
python3 scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/uart-sim0 \
  --ack-at --count 0
```

## 4.4 云端下发（下行）

### DDS 模式
```bash
./test/publisher --device-type ble_mesh --count 100 --interval-ms 200
```

### MQTT 模式
用你的 MQTT 客户端向 `config/transport.ini` 的 `subscribe_topic` 发送命令消息。

## 4.5 开发板 + 虚拟机跨机联调流程

1. 开发板侧运行网关  
```bash
./build/bin/gateway app
```

2. 开发板侧确认设备节点  
```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
ls /dev/i2c-* 2>/dev/null
ls /dev/spidev* 2>/dev/null
```

3. 开发板侧配置 `device_registry`  
- `device_paths` 填实际节点（如 `/dev/ttyUSB0,/dev/i2c-1,/dev/spidev0.0`）
- `interfaces` 按顺序填 `serial,i2c,spi`
- `protocols` 按顺序对齐，未使用私有协议位置留空

4. 虚拟机侧发送模拟流量  
- UART：用 `scripts/simulate_lower_device.py` 发送
- I2C/SPI：用你 USB 转模块的官方工具发送事务请求/响应
- 云侧下行：用 DDS `test/publisher` 或 MQTT 客户端发送命令

5. 联调证据采集  
- 开发板：保存网关日志（重点看 `[snapshot]`、`[device]`、`[protocol]`、`[health]`）
- 开发板：`python3 scripts/read_gateway_db.py --limit 50`
- 虚拟机：记录发送时间戳与报文内容用于对账

6. 常见风险  
- 虚拟机 USB 透传抖动会带来偶发超时，关键结论建议在开发板本机复测
- USB 转 I2C/SPI 若非标准内核设备节点，I2C/SPI 测试会失效
- 供电或线缆问题会伪造成协议故障，先排除物理层再排软件问题

---

## 5. 数据库验证

```bash
python3 scripts/read_gateway_db.py --limit 50
```

建议重点看：
- `device_path`
- `interface_name`
- `protocol_family`
- `protocol_name`
- `qos`
- `status/retry_count`

或使用 sqlite3：
```bash
sqlite3 <db_path_from_gateway_ini>
```
```sql
SELECT id, device_path, interface_name, protocol_family, protocol_name, qos, status, retry_count
FROM messages ORDER BY id DESC LIMIT 20;
```

---

## 6. 虚拟机扩展测试

## 6.1 CAN（vcan）
```bash
./scripts/debug_can_vcan.sh 2 vcan /tmp/gateway-debug-can-map.tsv
```

## 6.2 I2C（stub）
```bash
sudo ./scripts/debug_i2c_stub.sh 10 0x50 /tmp/gateway-debug-i2c-map.tsv
```

## 6.3 SPI（mock）
```bash
sudo ./scripts/debug_spi_mock.sh spi-mockup /dev/spidev0.0 /tmp/gateway-debug-spi-map.tsv
```

说明：I2C/SPI 测试依赖虚拟机内核模块是否可用，不可用时先完成 UART + MQTT/DDS 主链路验证。

---

## 7. 故障注入与恢复

## 7.1 网络中断恢复
1. 运行过程中断开网络 30~120 秒。  
2. 恢复网络后观察是否自动恢复发布/订阅。  
3. 检查持久化是否按重试策略回放。

## 7.2 进程重启恢复
1. 运行中强制停止网关。  
2. 重新启动后检查：
- 设备是否重新注册
- 传输是否重连
- 数据库状态是否一致

## 7.3 OTA 专项校验（虚拟机）

目标：验证 OTA 升级流程的关键判定与失败路径是否正确。

1. 版本比较校验  
- 准备版本文件：当前版本 `1.9`，服务端版本 `1.10`。  
- 预期：应识别为“有更新”，不能按字符串比较误判。

2. 签名下载失败校验  
- 人为将签名 URL 指向不存在路径。  
- 预期：`ota_verify` 直接失败并返回下载/网络错误，不进入“签名通过”路径。

3. boot 配置完整性校验  
- 人为篡改 `boot.conf` 的 `checksum` 或 `version`。  
- 预期：读取失败并回退到安全默认分区，不信任损坏配置。

4. 升级检查返回码语义  
- 模拟“无更新”和“检查失败”两种场景。  
- 预期：无更新返回 `1`，失败返回 `-1`，日志语义清晰区分。

5. 空固件保护  
- 准备 0 字节固件文件。  
- 预期：安装阶段直接失败，不允许进入进度计算和分区写入。

---

## 8. 通过标准（虚拟机场景）

- 构建成功：`make` 与 `make -C test` 均通过
- 启动成功：关键快照日志齐全，无初始化致命失败
- 上行成功：模拟设备数据可进入路由并落库
- 下行成功：测试命令可从云侧到达设备侧
- 恢复成功：网络中断/进程重启后可自动恢复
- 稳定性：连续运行 2 小时无崩溃、无卡死、无明显内存持续上涨

### 8.1 半实物联调补充标准
- 开发板上目标设备均完成初始化与注册
- 虚拟机发送报文可在开发板日志与数据库中闭环对账
- 至少完成一次链路中断恢复验证（USB重插或网络短断）
- 关键用例在开发板本机复测通过

---

## 9. 测试记录模板

- 测试日期：
- 虚拟机环境（OS/内核/CPU/内存）：
- 传输模式（MQTT/DDS）：
- 设备配置（device_paths/interfaces/protocols）：
- 执行命令：
- 日志证据：
- 数据库证据：
- 结果：PASS / FAIL / BLOCKED
- 问题与修复建议：
