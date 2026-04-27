# 网关问题修复记录

---

## 1. 启动配置快照缺失（可观测性不足）

### 问题说明
系统启动时虽然会打印零散日志，但缺少统一“配置快照”输出。线上排障时很难第一时间确认：
- 本次实际生效的线程池参数
- 设备数量、设备缓冲区、协议映射
- 持久化与传输层关键参数

### 问题代码形态（修复前）
`src/app_runner.c` 仅有零散日志，例如：
```c
log_info("Runtime config loaded: thread_pool_executors=%d", ...);
log_info("Startup summary: configured_device_count=%d, ...", ...);
```
没有结构化、完整、可检索的启动快照。

### 修复思路
在启动路径新增结构化快照日志，统一输出 `event=... key=value` 形式：
- 启动总配置快照
- 每个设备配置快照
- 传输层配置快照（在 `router` 初始化时输出）

### 实际改动
- `src/app_runner.c`
  - 新增 `log_startup_snapshot()` / `log_device_snapshot()`
  - 启动时输出 `[snapshot] event=startup_config ...`
  - 按设备输出 `[snapshot] event=device_config ...`
- `src/app_router.c`
  - 在 `transport_load_config()` 成功后输出 `[snapshot] event=transport_config ...`

### 我的看法
配置快照不是“锦上添花”，而是运行系统的最低可观测性基线。尤其多配置文件（`gateway.ini + transport.ini + protocols.ini`）场景，没有快照几乎无法快速定位配置漂移问题。

---

## 2. 设备初始化“全有或全无”，单设备失败会拖垮全局启动

### 问题说明
此前设备初始化或协议应用只要有一个失败，整体直接退出。这在多设备接入场景非常脆弱：一个坏设备会阻断全部业务。

### 问题代码形态（修复前）
`src/app_runner.c` 中：
```c
if (app_device_layer_init(&devices[i], ...) != 0) {
    ... return -1;
}
if (app_device_layer_configure(&devices[i], ...) != 0) {
    ... return -1;
}
```
`src/app_router.c` 启动设备时也会“遇错全回滚”。

### 修复思路
改为“部分成功可运行”策略：
- 启动阶段：失败设备跳过并记录，不影响其他设备
- 路由启动阶段：启动失败的设备从路由列表移除，不回滚已成功设备
- 兜底：如果成功设备数为 0，仍然失败退出（避免空跑）

### 实际改动
- `src/app_runner.c`
  - 设备初始化改为“压缩成功集合”模式
  - 增加 `failed_device_count` 统计
  - 输出 `event=bootstrap_summary`
  - 若 `initialized_device_count == 0`，才返回失败
- `src/app_router.c`
  - `app_router_start()` 中，设备启动失败改为移除失败设备并继续
  - 仅在最终无可运行设备时返回失败

### 我的看法
网关属于“边缘系统”，需要容忍部分设备异常。全有或全无只适合强一致服务，不适合多端接入网关。

---

## 4. 协议配置校验不严格，异常协议可能静默降级

### 问题说明
协议加载逻辑此前偏“容错回退”，会把配置错误悄悄替换成默认值。这样会掩盖协议配置问题，导致现场表现为“设备连上了但行为不对”。

### 问题代码形态（修复前）
`src/app_protocol_config.c` 中大量逻辑是：
```c
log_warn("Invalid ..., fallback default");
```
函数最终多数仍返回成功，导致上层误以为协议加载正常。

### 修复思路
引入“严格校验 + 报告”：
- 新增协议结构完整性校验 `app_protocol_validate()`
- 对显式指定的“非默认协议”启用严格模式：解析错误直接失败
- 对默认协议仍保留兜底，兼顾兼容性
- 在设备层输出更明确的协议失败日志（加载失败/绑定失败/初始化命令失败）

### 实际改动
- `src/app_protocol_config.c`
  - 新增 `app_protocol_validate(...)`
  - 新增 `strict_mode`（非默认协议严格）
  - 解析错误计数 `parse_errors`，严格模式下直接 `return -1`
  - 缺失关键配置时输出 `log_error`
- `src/app_device_layer.c`
  - 增加结构化日志：`event=load_failed/bind_failed/status_check_failed/init_command_failed/apply_success`

### 我的看法
协议配置是“行为定义”，不能用过度兜底掩盖问题。默认协议可兜底是为了可用性，非默认协议应优先保证确定性。

---

## 5. 传输层缺少健康状态机与诊断计数

### 问题说明
传输层只有连接状态，没有可追踪的健康指标。遇到“偶发发布失败”“重连抖动”时，无法快速回答：
- 连接失败了几次？
- 发布失败占比多少？
- 最后一次错误在哪个阶段发生？

### 修复思路
新增 `TransportHealth` 健康结构，并在关键路径记账：
- 状态变化次数与时间
- 连接尝试/失败、断开次数
- 发布尝试/失败、订阅尝试/失败
- 最后错误阶段与错误码

并提供统一输出接口。

### 实际改动
- `include/app_transport.h`
  - 新增 `TransportHealth`
  - `TransportManager` 增加 `health` 字段
  - 新增 `transport_get_health()` / `transport_log_health()`
- `src/dds/app_transport.c`
  - 新增 `transport_now_ms()`、`transport_record_error()`、`transport_set_state()`
  - 在 connect/publish/subscribe/default publish/default subscribe/switch 流程中更新计数
  - 新增 `[health]` 结构化日志输出
- `src/app_runner.c`
  - 在 `router_init`、`router_start_failed`、`before_stop`、`before_close` 时输出健康日志

### 我的看法
健康状态机是传输层“可维护性开关”。没有这层，现场问题只能靠猜；有这层，至少能在一分钟内先定位到失败阶段。

---

## 7. 路由回调依赖全局 `g_router`，存在耦合与并发风险

### 问题说明
设备消息回调通过全局 `g_router` 找路由实例，生命周期与并发边界不清晰。  
问题典型表现：停机过程、重启过程、并发回调下的悬空引用风险。

### 问题代码形态（修复前）
`src/app_router.c`：
```c
static RouterManager *g_router = NULL;
static int on_device_message(void *ptr, int len) {
    RouterManager *router = g_router;
    ...
}
```

### 修复思路
把回调改成“显式上下文传递”：
- 设备层回调签名升级为 `(void *context, void *ptr, int len)`
- 路由为每个设备分配独立 `RouterDeviceCallbackContext`
- 注销/关闭时释放上下文，彻底移除全局依赖

### 实际改动
- `include/app_device.h`
  - 新增 `DeviceRecvCallback`
  - `VTable` 新增 `recv_callback_ctx`
  - `app_device_registerRecvCallback()` 新签名（含 context）
- `src/app_device.c`
  - 回调调用改为 `recv_callback(recv_callback_ctx, ...)`
- `include/app_router.h`
  - `RouterManager` 新增 `device_callback_ctx[]`
- `src/app_router.c`
  - 删除 `g_router`
  - `on_device_message()` 改为基于 context 获取 router/device
  - 注册/注销/关闭时管理 callback context 生命周期

### 我的看法
去全局依赖是架构稳态的关键一步。回调带 context 是 C 工程里最常见、最稳妥的解耦方式。

---

## 9. 日志结构不统一，检索与自动分析困难

### 问题说明
历史日志格式混杂，缺少统一字段，难以被脚本稳定解析。

### 修复思路
在关键路径统一采用“结构化 KV 日志”：
- 统一前缀：`[snapshot]` / `[device]` / `[protocol]` / `[health]`
- 统一字段：`event=... key=value ...`

### 实际改动
- `src/app_runner.c`：启动快照、设备启动结果、设备注册、传输健康日志
- `src/app_router.c`：传输配置快照、设备启动失败日志
- `src/app_device_layer.c`：协议应用各阶段结果日志
- `src/dds/app_transport.c`：健康状态结构化日志

### 我的看法
日志“可读”不等于“可运维”。结构化字段是自动化排障、压测对比、线上告警聚合的前提。

---

## 修改文件清单

- `include/app_device.h`
- `src/app_device.c`
- `include/app_router.h`
- `src/app_router.c`
- `include/app_transport.h`
- `src/dds/app_transport.c`
- `src/app_protocol_config.c`
- `src/app_device_layer.c`
- `src/app_runner.c`
- `problem.md`
