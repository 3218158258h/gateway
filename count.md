# 压测计数与结果记录

## 场景 1：高频消息上行（设备 -> 云端）

命令：
```bash
python3 scripts/simulate_lower_device.py \
  --port /tmp/gateway-vdev/sim0 \
  --device-id 0001 \
  --payload 01020304 \
  --count 1000 \
  --interval 0.01
```

结果（历史记录）：
- 100Hz * 1000 条，链路可稳定运行。
- 未观察到进程崩溃与明显乱码。

建议补采：
- 端到端送达率
- 重复率 / 乱序率
- P50/P95/P99 时延

## 场景 2：高频命令下行（云端 -> 设备）

建议命令（MQTT）：
```bash
python3 scripts/simulate_cloud_command.py \
  --topic gateway/command \
  --count 1000 \
  --interval 0.01
```

建议命令（DDS）：
```bash
test/publisher --device-type ble_mesh --count 1000 --interval-ms 10
```

建议记录：
- 下发成功条数
- 设备侧实际收到条数
- 回执/ACK 比例
- 失败重试次数

## 本轮修复后的新增观察点

- `[snapshot]`：启动配置是否符合预期
- `[device]`：设备初始化失败是否被隔离
- `[protocol]`：协议加载/校验失败原因
- `[health]`：传输层失败是否可量化追踪

