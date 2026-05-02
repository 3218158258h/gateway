#ifndef __APP_IFACE_SPI_H__
#define __APP_IFACE_SPI_H__

#include "app_serial.h"

/*
 * SPI 专用后台线程：
 * 1) 按 poll_interval_ms 周期发起 SPI 事务；
 * 2) 当 transfer_len=0 时仅休眠，不主动访问总线；
 * 3) 把读取结果封装为内部统一消息帧并投递到设备接收任务。
 */
void *app_iface_spi_background_task(void *argv);

#endif
