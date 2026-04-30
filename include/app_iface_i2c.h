#ifndef __APP_IFACE_I2C_H__
#define __APP_IFACE_I2C_H__

#include "app_serial.h"

/**
 * @brief I2C 专用后台线程
 *
 * 线程行为：
 * 1. 按 transport.i2c.poll_interval_ms 周期执行一次寄存器轮询。
 * 2. 先写寄存器地址，再读 read_len 字节，形成标准 I2C 读事务。
 * 3. 将读取结果封装为网关内部消息帧并写入设备接收缓冲区。
 *
 * @param argv SerialDevice *
 * @return 始终返回 NULL
 */
void *app_iface_i2c_background_task(void *argv);

#endif
