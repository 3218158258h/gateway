# 问题复盘：I2C 在虚拟机环境下轮询失败（高价值排障案例）

## 1. 背景

在完成 UART + MQTT 主链路验证后，我开始补测 I2C 接口。  
目标是用 `i2c-stub` 在虚拟机内模拟从设备，再让网关通过 I2C 轮询读取数据。

---

## 2. 现象

网关日志持续报错：

- `errno=95 (Operation not supported)`
- 位置：`src/app_iface_i2c.c` 轮询读事务

同时命令行验证出现：

- `i2cset` 可执行
- `i2ctransfer` 报错：`Adapter does not have I2C transfers capability`

---

## 3. 我的排查过程（面试重点）

我不是一开始就知道是驱动能力问题，而是按“证据收敛”方式逐步定位：

1. 先排除基础环境问题  
- 检查 `i2c-tools` 是否安装  
- 检查 `/dev/i2c-*` 是否存在  
- 检查用户组权限（`i2c` 组）

2. 再排除配置问题  
- 核对 `device_registry` 路径与接口类型  
- 核对 `address/register_addr/read_len` 参数  
- 确认网关确实启动了 I2C 设备并进入轮询

3. 最后做能力对照验证  
- 用 `i2cset/i2cget` 验证“简单 SMBus 命令可用”  
- 用 `i2ctransfer` 验证“原生 I2C 组合事务不可用”  
- 对照网关代码轮询方式（`I2C_RDWR`）与报错 `errno=95`

通过这三步，我把问题从“配置/权限/业务逻辑”收敛到“适配器能力边界”。

---

## 4. 根因结论

根因不是协议写错，也不是网关逻辑写错，而是当前适配器/驱动在虚拟机环境下**不支持原生 I2C transfer 能力**。  
网关 I2C 轮询使用 `I2C_RDWR`（组合事务：写寄存器地址 + 读数据），该能力不被当前适配器支持，因此返回 `Operation not supported`。

一句话总结：

**能 `i2cset` 不代表能 `i2ctransfer`；前者常走 SMBus 子集，后者需要更完整的 I2C transfer 能力。**

---

## 5. 关键证据链

1. 网关日志：`[i2c] poll_failed ... errno=95(Operation not supported)`  
2. 工具对照：`i2ctransfer` 明确提示不具备 transfer capability  
3. 行为一致性：`i2cset` 可用但网关轮询失败，符合“SMBus 可用 / I2C_RDWR 不可用”的典型特征

---

## 6. 已采取动作

1. 修复 `debug_i2c_stub.sh` 的总线兼容问题  
- 当前内核忽略 `bus_num` 参数时，脚本可自动识别实际创建的总线并写入 map 文件

2. 调整测试策略  
- 在基础功能阶段先完成 UART/MQTT/DDS 全链路  
- I2C 读轮询暂按“能力受限”场景处理，避免阻塞主线测试

---

## 7. 临时规避方案

在当前虚拟机环境下：

- 若要保持 I2C 设备可初始化但不刷错误日志，可将：
  - `read_len = 0`
  - `poll_interval_ms = 0`

这样可保留设备注册流程，但关闭主动轮询读事务。

---

## 8. 后续优化方向（可作为面试延展）

1. 在 I2C 初始化时增加能力探测（`I2C_FUNCS`）  
- 提前判断是否支持 `I2C_FUNC_I2C`

2. 增加降级路径  
- 若不支持 `I2C_RDWR`，尝试 SMBus 兼容读法（适配可支持的设备）

3. 增加一次性能力告警  
- 避免 `errno=95` 高频刷屏，提升可观测性质量

---

## 9. 面试表达模板

“我在虚拟机做 I2C 联调时遇到 `errno=95`，最初不确定是配置还是代码问题。  
我先排除了权限和配置，再用 `i2cset` 与 `i2ctransfer` 做能力对照，最终定位为适配器驱动不支持原生 I2C transfer，而网关轮询使用的是 `I2C_RDWR`。  
所以这不是业务代码 bug，而是驱动能力边界。我随后修复了脚本兼容、调整了测试策略，并规划了能力探测和降级方案。”  
