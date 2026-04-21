### 高频消息下的数据上行
在100Hz的频率下发送1000条消息
```bash
python3 scripts/simulate_lower_device.py --port /tmp/gateway-vdev/sim0 --device-id 0001 --payload 01020304 --count 1000 --interval 0.01
```
- 结果：一切正常，无丢包，无乱码，无异常退出。

### 高频命令下的数据下行
在100Hz的频率下发送1000条命令
```bash