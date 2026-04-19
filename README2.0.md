# Gateway README 2.0（测试方法与流程草案）

## 1. 目标与原则

本轮目标：
- 优先验证功能完整性与准确性（基础功能 + 基础通信）
- 建立可重复的测试流程与验收标准
- 明确当前环境阻塞项，避免在功能未闭环前进行过早优化

原则：
- 先功能正确，再性能优化（“过早优化是万恶之源”）
- 每个测试项都要有输入、步骤、预期结果、失败排查
- 记录可复现命令与测试结论

---

## 2. 测试范围分期

### Phase A（本轮）：基础功能与基础通信
- 构建与依赖可用性检查
- 配置加载与启动路径验证
- 串口模拟链路验证（虚拟串口 + 下位机模拟）
- MQTT/DDS 基本收发链路验证
- 本地持久化（SQLite）可观察性验证

### Phase B（后续）：高并发与稳定性
- 多设备并发、突发流量、长稳压测
- 重试/退避/队列堆积行为验证

### Phase C（后续）：资源与能耗
- CPU、内存、I/O、网络占用
- 功耗测量与场景对比
- 根据瓶颈进行针对性优化与回归验证

---

## 3. 环境准备与依赖

## 3.1 基础依赖（构建/运行）
- gcc / make
- Python 3
- sqlite3
- MQTT C SDK（头文件 `MQTTClient.h` 与链接库）
- Cyclone DDS（DDS 模式下需要 `dds/dds.h` 及相关库）
- socat（虚拟串口联调）

## 3.2 当前仓库内标准入口
- 主工程构建：`make`
- DDS测试构建：`make -C test`
- 虚拟串口创建：`./create_virtual_nodes.sh`
- 下位机模拟：`python3 simulate_lower_device.py`
- 串口监控：`./monitor_virtual_port.sh`
- 数据库查看：`python3 read_gateway_db.py`

---

## 4. 本轮基础测试流程（推荐执行顺序）

## Step 1：构建与测试构建基线
1. 执行主工程构建：`make`
2. 执行测试目录构建：`make -C test`

验收标准：
- 若依赖齐全，应完成编译并生成目标文件
- 若失败，必须记录缺失依赖并归类为“环境阻塞”

---

## Step 2：配置检查（gateway.ini）
重点检查：
- `[transport].type` 与编译选项是否一致  
  - `type=dds` 时，二进制必须由 `make USE_DDS=1 ...` 构建
- `[device].serial_devices` 是否指向可用串口（真实或虚拟）
- `[persistence].db_path` 是否可写

验收标准：
- 配置与运行模式一致，不存在明显冲突项

---

## Step 3：无硬件联调（虚拟串口）
1. 创建虚拟串口对（示例3对）  
   `./create_virtual_nodes.sh 3 /tmp/gateway-vdev`
2. 将输出的 `gw*` 端口写入 `[device].serial_devices`
3. 启动网关进程
4. 启动模拟下位机发送数据（示例）：
   ```bash
   python3 simulate_lower_device.py \
     --port /tmp/gateway-vdev/sim0 \
     --device-id 0001 \
     --payload 01020304 \
     --count 10 \
     --interval 1
   ```
5. 用 `monitor_virtual_port.sh` 观察串口收发

验收标准：
- 网关可从模拟设备接收帧，不崩溃、不异常退出
- 收发链路日志与预期一致

---

## Step 4：基础通信测试（MQTT / DDS）

### 4.1 MQTT 基础收发
前置：
- `transport.type=mqtt`
- MQTT 参数配置完整（server/client_id/publish_topic/subscribe_topic）

验证点：
- 设备上行数据可发布到 `publish_topic`
- 云端下行指令可由网关订阅并转发到串口

### 4.2 DDS 基础收发
前置：
- `transport.type=dds`
- 使用 `make USE_DDS=1 DDS_HOME=...` 构建
- `test/` 下发布订阅程序可构建运行

验证点：
- GatewayData 发布路径可观测
- GatewayCommand 订阅路径可观测

验收标准：
- 完成至少一次端到端上行与下行闭环

---

## Step 5：持久化与可观察性
1. 执行：`python3 read_gateway_db.py --limit 20`
2. 核对 `messages` 表是否按预期落库/状态更新

验收标准：
- 数据库可访问
- 消息记录字段（topic/status/retry/create_time 等）可读

---

## 5. 测试用例模板（执行记录）

每条用例建议记录：
- 用例ID：
- 测试目的：
- 前置条件：
- 输入数据：
- 执行步骤：
- 预期结果：
- 实际结果：
- 是否通过（PASS/FAIL/BLOCKED）：
- 证据（日志/命令输出/截图）：

---

## 6. 本轮执行结果（当前环境）

已验证：
- `make` 已执行，编译在 MQTT 头文件缺失处失败（`MQTTClient.h`）
- `make -C test` 已执行，DDS 头文件缺失（`dds/dds.h` 等）
- `simulate_lower_device.py --help` 可用
- `read_gateway_db.py --help` 可用，读取默认数据库可用（当前 messages 表为空）
- `monitor_virtual_port.sh` 用法检查正常

阻塞项：
- 缺少 MQTT/Cyclone DDS 头文件与库
- 缺少 `socat`，导致虚拟串口对无法创建

结论：
- 当前环境可完成“脚本级”与“可观察性”基础验证  
- 端到端通信与完整功能闭环受外部依赖缺失阻塞，需先补齐依赖再继续

---

## 7. 后续计划（不做过早优化）

在 Phase A 功能闭环通过后，再按以下顺序推进：
1. 高并发压测（设备数、消息速率、持续时间分层）
2. 资源画像（CPU/内存/线程/队列/磁盘I/O）
3. 功耗测量（空闲、稳态、峰值场景）
4. 仅针对实测瓶颈做优化
5. 每次优化后执行功能回归 + 性能对比，确保“优化不破坏正确性”
