# 网关问题修复记录

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

## 31. 注释风格不统一，存在英文注释与关键逻辑说明缺口

### 问题说明
项目中部分文件仍存在英文注释或注释粒度不一致的情况。
在多人协作或后续自维护场景下，会增加阅读成本，尤其是设备收发链路、
配置解析链路和空协议分支等关键逻辑，缺少统一、直观的中文注释规范。

### 问题代码
- `src/app_device.c`
  - 默认收发任务中存在多处英文注释（如帧合法性与丢弃策略说明）。
- `src/main.c`
  - 缺少文件头注释，分支注释风格不一致。
- `src/app_device_layer.c`
  - 空协议分支新增后，语义需要明确注释以避免误解。

### 修复思路
- 对关键路径优先执行“必要注释补齐”：
  - 复杂控制流（异常丢弃、重试、状态分支）必须给出中文说明。
  - 入口文件补文件级注释，分支注释统一风格。
- 保持“少而准”的原则，不做逐行噪声注释。

### 实际改动
- `src/app_device.c`
  - 将默认接收任务中的英文注释替换为中文，明确：
    - 帧消费策略
    - 非法类型/长度处理
    - 不完整帧与缓冲区拥塞处理
    - 回调重试语义
- `src/main.c`
  - 新增文件头注释。
  - 统一主命令分发分支注释表达。
  - 补充帮助文本保留英文的原因说明。
- `src/app_device_layer.c`
  - 在空协议分支补充中文注释，明确“跳过私有协议初始化”的行为边界。

### 验证结果
- 核心执行链路注释已统一为中文，复杂分支可读性提升。
- 入口与关键逻辑说明更加清晰，便于后续迭代维护。

---

## 30. device_registry 的协议列表空项会错位，且无法表达“该设备无私有协议”

### 问题说明
用户希望在 `device_registry.protocols` 中通过空项表示“该设备暂不启用私有协议”，
例如前几个串口有协议、后几个 SPI/I2C/CAN 先留空。

原实现使用 `strtok_r` 并跳过空 token，会把后续协议左移，导致按索引错位；
同时未填协议会回落默认协议，无法表达“显式无协议”。

### 问题代码
- `src/app_runner.c`
  - `parse_string_list()` 仅统计非空项，空项被丢弃。
  - `load_device_registry_from_physical_config()` 初始化 `out_protocols` 为默认协议，
    导致空项最终仍会套用默认协议。
- `src/app_device_layer.c`
  - `app_device_layer_apply_protocol()` 对空协议名没有明确分支，无法做“跳过协议”。

### 修复思路
- 解析器改为“按索引对齐”并保留空项，空项只占位不左移。
- `device_registry` 路径下协议默认值改为空字符串，避免隐式套默认协议。
- 设备层增加“空协议显式跳过”分支：设备进入 `CONFIGURED`，但不加载私有协议。
- 日志把空协议显示为 `none`，便于排障。

### 实际改动
- `src/app_runner.c`
  - 新增 `parse_aligned_string_list()`，按索引保留空项。
  - `device_registry` 读取时：
    - `interfaces`/`protocols` 使用对齐解析；
    - 协议默认值改为空字符串；
    - 增加空项数量日志。
  - 快照与注册日志中，空协议统一显示为 `none`。
- `src/app_device_layer.c`
  - `app_device_layer_apply_protocol()` 新增空协议分支：
    - `connection_type=CONNECTION_TYPE_NONE`
    - `protocol_name=""`
    - 串口场景清空 `post_read/pre_write`
    - 状态置为 `DEVICE_STATE_CONFIGURED`
- `config/transport_physical.ini`
  - `device_registry` 注释新增“protocols 空项表示跳过私有协议”说明。
- `README.md`
  - 补充 `device_registry.protocols` 空项语义说明。

### 验证结果
- `protocols` 可用空项按索引占位，不再发生协议错位。
- 空协议设备可正常完成启动流程，不触发私有协议加载失败。

---

## 29. 蓝牙运行参数分散在 gateway.ini，协议配置不自包含

### 问题说明
私有协议初始化命令使用占位符 `{m_addr}/{net_id}/{baud_code}`，
但替换值来自 `gateway.ini [bluetooth]`，导致同一协议定义分散在两处配置：
- 协议帧规则在 `protocols.ini`
- 协议关键运行参数在 `gateway.ini`

这种设计会增加配置耦合，迁移协议或复用协议时容易漏配。

### 问题代码
- `src/app_device_layer.c`
  - `app_device_layer_load_runtime_config()` 从 `gateway.ini [bluetooth]` 读取
    `m_addr/net_id/baud_rate`。
  - `app_device_layer_apply_protocol_serial()` 用上述值替换 `init_cmds` 占位符。
- `gateway.ini`
  - 存在 `[bluetooth]` 节，保存协议模板变量。
- `config/protocols.ini`
  - 注释声明占位符值来自 `gateway.ini [bluetooth]`。

### 修复思路
- 将 `m_addr/net_id/work_baud` 下沉到每个协议节，协议配置自包含。
- 删除设备层对 `gateway.ini [bluetooth]` 的读取逻辑。
- 串口协议应用阶段直接使用当前协议对象里的参数进行模板替换。
- 删除 `gateway.ini [bluetooth]` 节，并同步文档说明。

### 实际改动
- `include/app_private_protocol.h`
  - `PrivateProtocolConfig` 新增：
    - `m_addr[5]`
    - `net_id[5]`
    - `work_baud`
- `src/app_private_protocol.c`
  - 默认协议补充：
    - `m_addr=0001`
    - `net_id=1111`
    - `work_baud=115200`
- `src/app_protocol_config.c`
  - 协议默认值与解析逻辑新增 `m_addr/net_id/work_baud`。
  - 校验新增：
    - `m_addr/net_id` 长度必须 4
    - `work_baud` 仅允许 `9600/115200`
  - 注释改为“占位符替换值来自当前协议节”。
- `src/app_device_layer.c`
  - 删除 `app_device_layer_load_runtime_config()` 及其缓存状态。
  - `app_device_layer_apply_protocol_serial()` 改为使用 `protocol->m_addr/protocol->net_id/protocol->work_baud`。
- `config/protocols.ini`
  - 更新注释说明来源。
  - 在协议节新增：
    - `m_addr`
    - `net_id`
    - `work_baud`
- `gateway.ini`
  - 删除 `[bluetooth]` 节。
- `README.md`、`include/app_bluetooth.h`
  - 更新相关说明，去除对 `gateway.ini [bluetooth]` 的依赖描述。

### 验证结果
- 串口私有协议初始化参数由 `protocols.ini` 单点提供。
- `gateway.ini` 不再承载协议模板变量，配置边界更清晰。

---

## 28. 设备节点清单仍在 gateway.ini，物理配置分层未闭环

### 问题说明
设备实例清单（节点路径、接口类型、协议名）原先放在 `gateway.ini` 的 `[device]`：
- `serial_devices`
- `serial_interfaces`
- `serial_protocols`

这与“物理链路配置集中在 `config/transport_physical.ini`”的分层目标不一致，
会导致设备声明与接口参数分散在两份配置，维护和联调容易错位。

### 问题代码
- `src/app_runner.c`
  - `load_device_config()` 仅从 `gateway.ini [device]` 读取设备清单。
- `gateway.ini`
  - `[device]` 内包含设备路径/接口/协议三组列表。
- 文档仍引导修改 `gateway.ini [device].serial_devices`。

### 修复思路
- 把设备实例声明迁移到 `config/transport_physical.ini`，新增统一节：
  - `[device_registry]`
  - `device_paths`
  - `interfaces`
  - `protocols`
- `app_runner` 启动时优先读取 `[device_registry]`。
- 保留旧字段读取作为回退路径，避免已有部署立刻失效。
- 同步修正文档与注释，统一配置入口说明。

### 实际改动
- `config/transport_physical.ini`
  - 新增 `[device_registry]` 设备实例清单。
- `src/app_runner.c`
  - 新增 `load_device_registry_from_physical_config()`。
  - `load_device_config()` 优先从 `physical_transport` 配置读取设备清单。
  - 旧 `serial_*` 字段保留为兼容回退。
  - 更新错误日志文案，明确优先项为 `[device_registry].device_paths`。
- `gateway.ini`
  - 删除 `serial_devices/serial_interfaces/serial_protocols`，改为迁移注释。
- 文档/注释更新：
  - `README.md`
  - `gateway.md`
  - `Test.md`
  - `config/protocols.ini`

### 验证结果
- 设备节点声明可直接在 `config/transport_physical.ini [device_registry]` 管理。
- 运行时设备加载链路优先使用物理配置文件，分层职责更清晰。

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



## 10. 守护进程无限重启，异常时会形成重启风暴

### 问题说明
原守护逻辑在子进程异常退出后立即重启，且崩溃计数超过阈值后仍继续重启。  
这会导致 CPU 抖动、日志爆炸、系统不可用。

### 问题代码形态（修复前）
`src/daemon/daemon_runner.c` 里即使 `crash_count > 10`，也仅写 marker，不阻断重启：
```c
if (crash_count > 10) { ... }
daemon_process_start(&subprocess[i]);  // 继续重启
```

### 修复思路
- 增加“熔断窗口”：超过阈值后进入阻断期，不再立即拉起。
- 增加指数退避：异常越频繁，重启间隔越长，上限可配置。

### 实际改动
- `src/daemon/daemon_runner.c`
  - 新增 `restart_block_until` 熔断逻辑
  - 新增 `compute_backoff_ms()` 指数退避
  - 新增 `check_restart_allowed()` 与 `note_abnormal_exit()`
  - 超阈值时写 marker 并阻断重启

### 我的看法
守护进程的核心不是“重启”，而是“受控重启”。没有熔断和退避，守护进程本身会成为故障放大器。

---

## 11. 子进程启动失败返回值未处理，状态机会失真

### 问题说明
原代码调用 `daemon_process_start()` 后不检查返回值。  
fork/exec 失败时守护进程可能认为“已拉起”，后续状态不可控。

### 问题代码形态（修复前）
```c
daemon_process_start(&subprocess[0]);
daemon_process_start(&subprocess[1]);
```

### 修复思路
- 抽象统一启动入口 `start_subprocess_at()`。
- 统一处理启动失败：计入崩溃计数、应用熔断/退避策略、日志明确化。

### 实际改动
- `src/daemon/daemon_runner.c`
  - 新增 `start_subprocess_at(index, is_retry)`
  - 初次启动和重启路径都改为统一入口，失败不再静默


---

## 12. 标准流重定向不安全，日志文件打开方式不正确

### 问题说明
原实现直接 `close(0/1/2)` 再 `open()`，不校验返回 fd 与重定向结果；  
并且日志文件未使用追加模式，存在覆盖风险。

### 问题代码形态（修复前）
```c
close(STDIN_FILENO); close(STDOUT_FILENO); close(STDERR_FILENO);
open("/dev/null", O_RDWR);
open(LOG_FILE, O_RDWR | O_CREAT, 0644);
open(LOG_FILE, O_RDWR | O_CREAT, 0644);
```

### 修复思路
- 改为 `dup2` 显式绑定 0/1/2。
- 日志文件使用 `O_WRONLY | O_CREAT | O_APPEND`。
- 日志路径不可用时兜底到 `/tmp/gateway.log`。

### 实际改动
- `src/daemon/daemon_runner.c`
  - 新增 `redirect_stdio()`
  - 全量替换原始重定向逻辑

### 我的看法
守护进程标准流重定向如果做错，后续任何故障都“看不见”，这是运维高危点。

---

## 13. 子进程程序路径硬编码，部署路径变化即失效

### 问题说明
原来 `PROGRAM_NAME` 固定为 `/home/root/gateway/gateway`，一旦部署路径变化直接失败。

### 修复思路
- 去宏硬编码，改为配置驱动。
- 子进程结构体保存 `program_path`，启动时使用该路径执行。

### 实际改动
- `include/daemon_process.h`
  - 删除 `PROGRAM_NAME` 宏
  - `SubProcess` 增加 `program_path`
  - `daemon_process_init()` 新增 `program_path` 参数
- `src/daemon/daemon_process.c`
  - 启动改为 `execv(subprocess->program_path, subprocess->args)`
- `config/daemon.ini`
  - 新增 `program_path` 配置项

### 我的看法
路径硬编码属于“环境耦合”问题，短期省事，长期是部署隐患。

---

## 14. `__environ` 可移植性差

### 问题说明
原代码使用 `execve(..., __environ)`，该符号在部分工具链不可用。

### 修复思路
- 使用 `execv()` 继承当前进程环境，去除对 `__environ` 的依赖。

### 实际改动
- `src/daemon/daemon_process.c`
  - `execve` -> `execv`

### 我的看法
守护进程这类基础组件应尽量避免非标准符号依赖，减少跨平台/交叉编译问题。

---

## 15. 子进程回收边界处理不足（`ESRCH/ECHILD`）

### 问题说明
停止子进程时，如果目标已退出或已被回收，原逻辑会报错返回，造成误告警。

### 修复思路
- `kill` 遇到 `ESRCH` 视为“进程已结束”。
- `waitpid` 遇到 `ECHILD` 视为“已被回收”。

### 实际改动
- `src/daemon/daemon_process.c`
  - `daemon_process_stop()` 增加 `ESRCH/ECHILD` 特判

### 我的看法
停止流程应优先保证幂等，不应把“已经结束”当成错误。

---

## 16. 缺少守护进程专用配置文件

### 问题说明
守护参数（日志路径、重启阈值、退避策略）散落在代码里，不能按现场环境调优。

### 修复思路
- 增加独立配置模块与配置文件，统一守护参数入口。

### 实际改动
- 新增 `include/daemon_config.h`
- 新增 `src/daemon/daemon_config.c`
- 新增 `config/daemon.ini`
- `src/daemon/daemon_runner.c` 接入 `daemon_config_load()`
- `Makefile` 增加 `src/daemon/daemon_config.c` 编译项

### 我的看法
守护进程属于部署敏感模块，参数配置化是必须项，不应该靠重编译调参数。

---


---

## 17. 虚拟节点脚本命名存在歧义，且缺少 I2C/SPI 联调脚本

### 问题说明
原有脚本 `create_virtual_nodes.sh` 默认只创建串口（UART）节点，但命名过于泛化，容易让人误解为“可创建任意接口虚拟节点”。  
同时缺少 I2C/SPI 风格节点脚本，联调入口不完整。

### 问题代码形态（修复前）
- 仅有 `scripts/create_virtual_nodes.sh`（实际依赖 `socat` 创建串口 PTY 对）。
- 仅有 `scripts/monitor_virtual_port.sh`，未体现 UART 语义。

### 修复思路
1. 明确脚本语义：以接口名入脚本名，避免歧义。  
2. 保持兼容：旧脚本保留为包装器，给出 deprecate 提示并转发到新脚本。  
3. 补齐 I2C/SPI 接口联调脚本。  
4. 同步文档，明确“模拟节点”和“真实内核设备”的边界。

### 实际改动
- 新增：
  - `scripts/create_virtual_uart_nodes.sh`
  - `scripts/create_virtual_i2c_nodes.sh`
  - `scripts/create_virtual_spi_nodes.sh`
  - `scripts/monitor_virtual_uart_port.sh`
- 兼容包装：
  - `scripts/create_virtual_nodes.sh`（deprecated，转发到 UART 脚本）
  - `scripts/monitor_virtual_port.sh`（deprecated，转发到 UART 监控脚本）
- UART 脚本命名升级：
  - 新节点：`uart-gwN / uart-simN`
  - 兼容软链：`gwN / simN`
- 文档更新：
  - `README.md`：新增多接口脚本用法与说明
  - `gateway.md`：新增联调脚本命名约定章节

### 我的看法
脚本名就是运维接口。命名不清会直接造成误用成本。  
此次拆分后，“脚本名即能力边界”更明确，且保留旧入口，风险可控。

---

## 18. 缺少 CAN 虚拟接口创建脚本，联调链路不完整

### 问题说明
补齐 UART/I2C/SPI 后，仍缺少 CAN 联调入口。  
测试人员只能手工敲 `ip link`，流程不统一、复现成本高。

### 问题代码形态（修复前）
- `scripts/` 下没有 CAN 接口创建脚本。
- 文档没有给出 CAN 虚拟接口标准步骤。

### 修复思路
1. 新增 `create_virtual_can_nodes.sh`。  
2. 统一支持数量、前缀、映射文件输出。  
3. 文档同步，明确 CAN 用 `vcan`，I2C/SPI 用 PTY 模拟。

### 实际改动
- 新增：
  - `scripts/create_virtual_can_nodes.sh`
    - 使用 `ip link add dev <name> type vcan`
    - 自动 `ip link set up <name>`
    - 输出映射文件：`index iface name state`
    - 支持参数：`COUNT PREFIX MAP_FILE`
    - 支持环境变量：`START_INDEX`
- 文档更新：
  - `README.md`：新增 CAN 脚本用法与说明
  - `gateway.md`：联调脚本命名约定新增 CAN

### 我的看法
CAN 场景优先采用 `vcan` 是更工程化的做法：比 PTY 更贴近 socketCAN 使用方式，调试价值更高。

## 19. SPI/I2C/CAN 设备被错误套用串口协议流程

### 问题说明
在设备层协议应用阶段，旧逻辑默认按串口蓝牙流程执行：切波特率、设置阻塞模式、发送 AT/初始化命令。  
这会导致非串口接口（SPI/I2C/CAN）在配置阶段误调用串口函数，出现配置失败或行为异常。

### 问题代码形态（修复前）
`src/app_device_layer.c` 中 `app_device_layer_apply_protocol(...)` 没有按接口分支，直接执行：
```c
app_serial_setBaudRate(...)
app_serial_setBlockMode(...)
app_serial_flush(...)
app_device_layer_send_command(...)
```
这套流程只适用于 UART 类设备。

### 修复思路
- 把协议应用流程拆成“串口专用”和“非串口”两条路径。
- 串口接口保持原有完整初始化流程（AT 状态检查 + init_cmds + 波特率切换）。
- 非串口接口只做协议绑定，不再触发串口配置指令。
- CAN 设备保留链路层专用 `post_read/pre_write` 钩子，不被协议层覆盖。

### 实际改动
- `src/app_device_layer.c`
  - 新增 `app_device_layer_apply_protocol_serial(...)`
  - 新增 `app_device_layer_apply_protocol_non_serial(...)`
  - `app_device_layer_apply_protocol(...)` 中按 `transport.interface_type` 分流
  - 非串口路径补充结构化日志，明确 `interface=...` 与协议应用结果

### 我的看法
设备层一定要“接口感知”。如果统一套串口流程，功能看似通用，实际是在引入隐性耦合。  
这次分流后，SPI/I2C/CAN 至少具备稳定的初始化边界，后续扩展各接口专属策略也更清晰。

---

## 20. 物理接口默认节不完整，且 CAN/SPI 参数未真正下沉到底层句柄

### 问题说明
链路层已支持读取 `transport_physical.ini`，但配置文件里缺少 `spi_default/i2c_default/can_default`，  
另外部分参数（例如 SPI 位序、CAN 回环/CAN FD）没有应用到实际设备句柄，导致“配置可写、运行不生效”。

### 问题代码形态（修复前）
- `config/transport_physical.ini` 只有 `transport.serial_default`，其余接口没有默认节。
- `src/app_link_adapter.c`
  - CAN 套接字只做 `socket/bind`，未设置 `CAN_RAW_LOOPBACK`、`CAN_RAW_FD_FRAMES`。
  - SPI 初始化未设置 `SPI_IOC_WR_LSB_FIRST`。
  - 物理配置加载失败分支缺少 `config_destroy` 清理。

### 修复思路
- 补全三类接口默认节，保证未命中具体节时仍有合理兜底。
- 在链路初始化时把关键参数真正应用到底层 fd/socket。
- 修正配置加载失败分支的资源释放，避免遗留对象。

### 实际改动
- `config/transport_physical.ini`
  - 新增 `[transport.can_default]`
  - 新增 `[transport.spi_default]`
  - 新增 `[transport.i2c_default]`
- `src/app_link_adapter.c`
  - CAN 初始化新增 `setsockopt(... CAN_RAW_LOOPBACK ...)`
  - CAN 初始化新增 `setsockopt(... CAN_RAW_FD_FRAMES ...)`
  - SPI 初始化新增 `ioctl(... SPI_IOC_WR_LSB_FIRST ...)`
  - `load_physical_config_for_device(...)` 加载失败分支补 `config_destroy(&cfg_mgr)`

### 我的看法
配置项“可配置”不等于“已生效”。  
这次修复的重点是把配置链路从 `ini -> 内存结构 -> 系统调用` 打通，避免参数停留在日志层面。

---


## 21. I2C/SPI 旧脚本仅生成 PTY，导致 ioctl 联调误判

### 问题说明
原 `create_virtual_i2c_nodes.sh` / `create_virtual_spi_nodes.sh` 只生成 PTY 对（`gw/sim`），
但脚本名会让使用者误以为可直接验证真实内核 `i2c-dev/spidev` 行为。
在网关初始化时会出现 `Inappropriate ioctl for device`（ENOTTY）告警，造成“接口实现异常”的错觉。

### 问题代码形态（修复前）
脚本固定走：
```bash
socat pty,link=i2c-gwN ... pty,link=i2c-simN ...
socat pty,link=spi-gwN ... pty,link=spi-simN ...
```
没有真实设备优先策略，也没有模式选择开关。

### 修复思路
- 继续沿用旧脚本名，避免迁移成本。
- 增加 `auto|real|pseudo` 模式（第 4 参数，默认 `auto`）：
  - `auto`：优先使用真实 `/dev/i2c-*`、`/dev/spidev*`；不足时回退 PTY。
  - `real`：强制真实设备，不足直接失败。
  - `pseudo`：强制 PTY，明确仅用于字节流联调。
- real 模式下在原目录创建 `i2c-gwN`/`spi-gwN` 软链，兼容现有 `gateway.ini` 路径写法。

### 实际改动
- `scripts/create_virtual_i2c_nodes.sh`
  - 新增模式参数与 `I2C_NODE_MODE` 环境变量。
  - 新增 real 设备发现与软链映射逻辑。
  - 保留 pseudo PTY 逻辑并在输出中强调 ioctl 限制。
- `scripts/create_virtual_spi_nodes.sh`
  - 新增模式参数与 `SPI_NODE_MODE` 环境变量。
  - 新增 real 设备发现与软链映射逻辑。
  - 保留 pseudo PTY 逻辑并在输出中强调 ioctl 限制。
- 文档同步：
  - `README.md`
  - `gateway.md`


---

## 22. I2C/SPI 复用流式读线程导致持续告警与接口语义偏差

### 问题说明
原设备层后台线程统一按串口流式模型循环 `read()`。
对于真实 I2C/SPI 设备，这种模型不符合主机事务语义，会在无事务数据时持续触发读错误日志。

### 问题代码
- `src/app_device.c`
  - `app_device_backgroundTask()` 统一对所有接口执行阻塞/轮询 `read()`。
- `src/app_device_layer.c`
  - 非串口接口曾绑定蓝牙串口帧钩子，导致接口语义耦合。

### 修复思路
- 为 I2C/SPI 引入接口专用后台任务，替换统一流式读模型。
- I2C/SPI 后台任务按配置执行周期事务（关闭轮询时仅保活等待，不刷读错误）。
- 非串口协议应用中，SPI/I2C 不再绑定蓝牙串口帧钩子。

### 实际改动
- 新增文件：
  - `include/app_iface_i2c.h`
  - `include/app_iface_spi.h`
  - `src/app_iface_i2c.c`
  - `src/app_iface_spi.c`
- `src/app_link_adapter.c`
  - I2C/SPI 初始化后绑定各自 `background_task`。
  - I2C 地址配置日志补全十六进制地址与错误文本。
- `src/app_device_layer.c`
  - `APP_INTERFACE_I2C/APP_INTERFACE_SPI` 不再绑定蓝牙帧钩子。
- `src/app_device.c`
  - 读错误日志增加 `errno` 上下文并做限频。
  - 设备打开失败日志补全 `errno` 文本。

### 验证结果
- 启动阶段 I2C/SPI 不再走统一流式读逻辑。
- 具备独立接口后台任务，可按配置开启事务轮询。

---

## 23. 物理层配置缺少事务轮询字段与地址解析歧义

### 问题说明
原配置仅有接口基础参数，缺少 I2C/SPI 事务轮询字段。
同时 I2C 地址仅按整数读取，`0x70` 等写法容易产生歧义。

### 问题代码
- `include/app_serial.h` 的 `spi/i2c` 结构缺少轮询参数。
- `src/app_serial.c` 中 `address` 通过整型读取，无法显式支持十六进制文本。

### 修复思路
- 扩展 `PhysicalTransportConfig`，增加 I2C/SPI 轮询参数。
- I2C 地址改为字符串解析（`strtoul(..., base=0)`），兼容 `112` 与 `0x70`。
- 在 `transport_physical.ini` 提供新字段模板。

### 实际改动
- `include/app_serial.h`
  - SPI 增加 `poll_interval_ms/transfer_len`。
  - I2C 增加 `poll_interval_ms/register_addr/register_addr_width/read_len`。
- `src/app_serial.c`
  - 加载上述新字段。
  - `address` 支持十进制与十六进制配置。
- `config/transport_physical.ini`
  - `transport.spi_default/spi0` 增加轮询字段。
  - `transport.i2c_default/i2c0` 增加轮询字段。

### 验证结果
- 轮询字段可从配置文件加载到运行时结构。
- I2C 地址支持 `0x..` 写法，减少配置歧义。

---


## 25. 上云消息缺少统一信封，单话题下无法稳定区分多接口设备数据

### 问题说明
项目采用单一上行话题（MQTT: `gateway/data`，DDS: `GatewayData`）发送设备数据。
原上行消息仅包含 `connection_type/id/data`，缺少接口来源与设备路径信息，
在 UART/I2C/SPI/CAN 混合接入时无法稳定区分来源设备与协议上下文。

### 问题代码
- `src/app_router.c`
  - `on_device_message()` 通过 `binary_to_json_safe()` 直接把二进制消息转旧 JSON：
    - `connection_type`
    - `id`
    - `data`
  - 缺少 `interface/device_path/protocol_*` 等区分字段。

### 修复思路
- 在路由层上行路径引入统一 Envelope 封包。
- 继续使用单话题上云，不拆分多 topic。
- Envelope 固定包含：
  - `schema_version`
  - `interface`
  - `device_path`
  - `protocol_family`
  - `protocol_name`
  - `timestamp_ms`
  - `payload_len`
  - `payload`（内含 `connection_type/id/data`）
- 设备标识统一使用 `device_path`，不引入逻辑 ID。

### 实际改动
- `src/app_router.c`
  - 删除旧上行转换路径 `binary_to_json_safe()` 的调用。
  - 新增 `binary_to_envelope_json()` 作为上行统一封包函数。
  - 新增 `router_protocol_family()`、`router_now_ms()`、`router_bin_to_hex()`。
  - `on_device_message()` 改为发布 Envelope JSON。
- `include/app_serial.h`
  - `PhysicalTransportConfig` 新增 `protocol_name` 字段。
- `src/app_device_layer.c`
  - 在协议应用阶段写入 `transport.protocol_name`，供上云封包使用。

### 验证结果
- 上云消息在单话题下可按 `interface + device_path + protocol_family + protocol_name` 区分来源。
- `payload` 保留原业务核心字段（`connection_type/id/data`），便于云端解包处理。

---

---

## 32. 其他模块注释同步不充分，跨模块阅读成本偏高

### 问题说明
在完成核心链路注释补齐后，mqtt/ota/buffer/config/dds 等模块仍有部分注释表达不统一，
导致阅读时在不同模块间切换成本较高，尤其是状态机、回调、重试与下载流程。

### 问题代码
- src/mqtt/app_mqtt_v2.c：重连、状态回调与发布路径说明不够统一。
- src/ota/app_ota.c：下载/验签/安装流程中部分说明风格不一致。
- src/dds/app_transport.c、src/dds/app_dds.c：关键流程注释有零散英文或混排。
- src/app_buffer.c、src/app_config.c、src/app_message.c 等：细粒度逻辑注释存在不一致。
- include/app_serial.h：protocol_name 来源描述已过时。

### 修复思路
- 只处理 影响理解的关键注释，不做格式化噪声修改。
- 统一描述方式：状态切换、回调触发、失败分支、资源释放路径优先补齐。
- 修正过时注释，确保与当前配置架构一致。

### 实际改动
- src/mqtt/app_mqtt_v2.c
  - 重连、连接状态、发布/订阅、线程循环等关键路径注释统一为中文说明。
- src/ota/app_ota.c
  - 检查更新、下载、验签、安装、回滚流程的关键注释补齐并统一。
- src/dds/app_transport.c、src/dds/app_dds.c
  - 传输初始化、主题注册、状态映射与发布订阅路径注释统一。
- src/app_buffer.c、src/app_config.c、src/app_message.c
  - 核心处理分支注释补齐。
- include/app_serial.h
  - protocol_name 注释改为来自 device_registry。

### 验证结果
- 主要模块注释风格与语义基本统一，跨模块阅读连续性提升。
- 配置来源说明与当前架构一致，减少误读风险。
