#ifndef __APP_IFACE_SPI_H__
#define __APP_IFACE_SPI_H__

#include "app_serial.h"

/**
 * @brief SPI 专用后台线程函数
 *
 * 行为说明：
 * 1. 按 `transport.spi.poll_interval_ms` 周期执行 SPI 事务。
 * 2. 当 `transport.spi.transfer_len == 0` 时仅休眠，不发起事务。
 * 3. 读到的原始字节会封装为内部统一消息格式后写入设备接收缓冲区，
 *    再触发设备接收任务进入路由链路。
 *
 * @param argv `SerialDevice *`
 * @return 始终返回 `NULL`
 */
void *app_iface_spi_background_task(void *argv);

#endif
