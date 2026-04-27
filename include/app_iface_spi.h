#ifndef __APP_IFACE_SPI_H__
#define __APP_IFACE_SPI_H__

#include "app_serial.h"

/* SPI 专用后台任务：按配置执行周期事务并上报内部消息帧。 */
void *app_iface_spi_background_task(void *argv);

#endif
