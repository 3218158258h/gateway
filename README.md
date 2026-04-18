
## 项目概述
- 基于IMX6ULL平台的智能网关
- 当前仅支持蓝牙设备，后续将接入LoRa设备
- 待开发功能：OTA升级、守护进程

**编译运行**
```bash
sudo socat -d -d pty,link=/dev/ttyUSB0,raw,echo=0,mode=666 pty,link=/dev/ttyUSB1,raw,echo=0,mode=666 # 创建虚拟串口

cat /dev/ttyUSB0 | hexdump -C # 监听

echo -ne '\xF1\xDD\x04\x00\x01\x01\x02' > /dev/ttyUSB1 # 发送数据
```
```bash
#修改app_ota.c配置文件路径
# 修改Makefile
make USE_DDS=1
./gateway app
```

### 蓝牙设备
- 基于JDY-24M超级蓝牙模块
   - 网关端：接入1个蓝牙模块
   - 下位机端：接入1个蓝牙模块（可通过PC机串口模拟）
- 网关启动后通过AT指令对蓝牙模块进行初始化配置

**消息格式**：
   - 上位机发送JSON格式：
   ```json
   {
     "connection_type": 1,
     "id": "AABBCCDD",
     "data": "112233445566"
   }
   ```
### Message模块
- 处理消息格式转换（JSON与二进制）
- 消息结构：连接类型、设备ID、数据
  - 从二进制数据初始化消息
  - 从JSON字符串初始化消息
  - 消息序列化为二进制/JSON

### 设备模块
- 抽象设备通用操作，支持串口等设备类型
  - 后台线程负责数据读取与缓存
  - 虚函数表支持设备类型扩展
  - 读写缓冲区分离，通过回调处理数据
- 初始化、启动/停止、读写数据、注册回调、关闭设备

```
gateway/
├── src/             # 应用程序核心代码
│   ├── app_bluetooth.c/h    # 蓝牙设备相关
│   ├── app_buffer.c/h       # 缓冲区管理
│   ├── app_device.c/h       # 设备抽象层
│   ├── app_message.c/h      # 消息处理
│   ├── app_mqtt.c/h         # MQTT通信
│   ├── app_router.c/h       # 消息路由
│   ├── app_runner.c/h       # 应用运行管理
│   ├── app_serial.c/h       # 串口设备
│   ├── app_task.c/h         # 任务管理
├── daemon/          # 守护进程相关代码
├── init/            # 初始化脚本
├── ota/             # OTA升级相关代码
├── test/            # 测试代码
├── thirdparty/      # 第三方库
│   ├── cJSON/       # JSON解析库
│   ├── log.c/       # 日志库
├── Makefile         # 编译配置
├── README.md        # 项目说明
├── main.c           # 程序入口
```