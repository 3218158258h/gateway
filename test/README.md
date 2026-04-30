该目录下的程序用于测试网关统一 Envelope 上云格式。

# 编译
在项目根目录执行：
```bash
make -C test
```

# 运行
## 订阅 GatewayData（DDS Envelope 解包）
```bash
./sub_data
```

## 发布 GatewayCommand（支持持续发布、递增载荷、设备类型）
```bash
# 单一设备类型
./publisher --device-type ble_mesh --count 100 --interval-ms 10 --start 1 --device-id 0001

# 多设备类型循环发送
./publisher --type-seq ble_mesh,lora --count 100 --interval-ms 200
```

可选参数：
- `--count N`：发送 N 条，`0` 表示持续发送
- `--interval-ms N`：发送间隔（毫秒）
- `--start N`：递增计数起始值
- `--device-id HEX4`：设备 ID（2 字节十六进制）
- `--connection-type N`：数值类型（0/1/2）
- `--device-type NAME`：类型别名（`none|lora|ble_mesh`）
- `--type-seq LIST`：类型序列（逗号分隔，循环使用）

## MQTT Envelope 解包
```bash
python3 mqtt_envelope_sub.py --host 127.0.0.1 --port 1883 --topic gateway/data
```

说明：
- 依赖 `paho-mqtt`：`pip3 install paho-mqtt`
- 输出字段包含：`interface/device_path/protocol_family/protocol_name/payload.*`
