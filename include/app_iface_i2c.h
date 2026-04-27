#ifndef __APP_IFACE_I2C_H__
#define __APP_IFACE_I2C_H__

#include "app_serial.h"

/* I2C 专用后台任务：按配置轮询寄存器并上报内部消息帧。 */
void *app_iface_i2c_background_task(void *argv);

#endif
