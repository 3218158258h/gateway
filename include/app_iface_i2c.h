#ifndef __APP_IFACE_I2C_H__
#define __APP_IFACE_I2C_H__

#include "app_serial.h"

/*
 * I2C 专用后台线程：
 * 1) 按 poll_interval_ms 周期执行“写寄存器地址 + 读数据”事务；
 * 2) 支持 1/2 字节寄存器地址宽度；
 * 3) 读取结果封装为内部统一消息帧，进入后续路由链路。
 */
void *app_iface_i2c_background_task(void *argv);

#endif
