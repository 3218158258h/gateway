# Gateway Makefile
# 支持x86和ARM交叉编译

CC ?= gcc
CROSS_COMPILE ?= arm-linux-gnueabihf-
AR ?= ar

# 编译目标: x86 或 arm
TARGET ?= x86

ifeq ($(TARGET), arm)
    CC = $(CROSS_COMPILE)gcc
    AR = $(CROSS_COMPILE)ar
endif




# 目录
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
IDL_DIR = idl
OBJ_DIR = $(BUILD_DIR)/obj
BIN_DIR = $(BUILD_DIR)/bin

# 源文件 - 基础模块
BASE_SRCS = $(SRC_DIR)/app_config.c \
            $(SRC_DIR)/app_bluetooth.c \
            $(SRC_DIR)/app_buffer.c \
            $(SRC_DIR)/app_device.c \
            $(SRC_DIR)/app_device_layer.c \
            $(SRC_DIR)/app_link_adapter.c \
            $(SRC_DIR)/app_message.c \
            $(SRC_DIR)/app_persistence.c \
            $(SRC_DIR)/app_private_protocol.c \
            $(SRC_DIR)/app_protocol_config.c \
            $(SRC_DIR)/app_router.c \
            $(SRC_DIR)/app_runner.c \
            $(SRC_DIR)/app_serial.c \
            $(SRC_DIR)/app_task.c \
            $(SRC_DIR)/daemon/daemon_process.c \
            $(SRC_DIR)/daemon/daemon_runner.c

# 源文件 - MQTT模块
MQTT_SRCS = $(SRC_DIR)/mqtt/app_mqtt_v2.c 

# 源文件 - DDS模块
DDS_SRCS = $(SRC_DIR)/dds/app_dds.c \
           $(SRC_DIR)/dds/app_transport.c \
		   $(SRC_DIR)/dds/GatewayData.c

# 源文件 - OTA模块
OTA_SRCS = $(SRC_DIR)/ota/app_ota.c

# 第三方库源文件
THIRDPARTY_SRCS = thirdparty/cJSON/cJSON.c \
                  thirdparty/log.c/log.c \
				  thirdparty/sqlite3/sqlite3.c

# 主程序
MAIN_SRC = $(SRC_DIR)/main.c

# 所有源文件
SRCS = $(BASE_SRCS) $(MQTT_SRCS) $(DDS_SRCS)  $(OTA_SRCS) $(THIRDPARTY_SRCS) $(MAIN_SRC)

# 目标文件
OBJ_SRCS = $(BASE_SRCS) $(MQTT_SRCS) $(DDS_SRCS)  $(OTA_SRCS)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(OBJ_SRCS)) \
       $(OBJ_DIR)/cJSON.o \
       $(OBJ_DIR)/log.o \
	   $(OBJ_DIR)/sqlite3.o \
       $(OBJ_DIR)/main.o

# 目标文件
TARGET_BIN = $(BIN_DIR)/gateway
TARGET_LIB = $(BUILD_DIR)/libgateway.a

# 编译选项
CFLAGS = -Wall -Wextra -O2 -g -std=gnu99
CFLAGS += -I$(INC_DIR) -I$(IDL_DIR)
CFLAGS += -Ithirdparty/cJSON
CFLAGS += -Ithirdparty/log.c
CFLAGS += -Ithirdparty/sqlite3
CFLAGS += -D_GNU_SOURCE
CFLAGS += -DUSE_CYCLONE_DDS

# 链接库
LDFLAGS = -lpthread -lpaho-mqtt3c -lcurl -lssl -lcrypto -ldl

# DDS编译选项（固定启用）
# 可按需覆盖：make DDS_HOME=/opt/cyclonedds/install
DDS_HOME ?= /home/nvidia/cyclonedds/install
CFLAGS += -I$(DDS_HOME)/include
LDFLAGS += -L$(DDS_HOME)/lib
LDFLAGS += -lddsc
LDFLAGS += -Wl,-rpath,$(DDS_HOME)/lib

# 调试信息
$(info Building for $(TARGET))
$(info CC: $(CC))

# 默认目标
all: dirs $(TARGET_BIN)

# 创建目录
dirs:
	@mkdir -p $(OBJ_DIR)/mqtt $(OBJ_DIR)/dds  $(OBJ_DIR)/ota $(OBJ_DIR)/daemon $(BIN_DIR)

# 链接
$(TARGET_BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build complete: $@"

# 编译源文件 - 基础模块
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译源文件 - MQTT模块
$(OBJ_DIR)/mqtt/%.o: $(SRC_DIR)/mqtt/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译源文件 - DDS模块
$(OBJ_DIR)/dds/%.o: $(SRC_DIR)/dds/%.c
	$(CC) $(CFLAGS) -c -o $@ $<


# 编译源文件 - OTA模块
$(OBJ_DIR)/ota/%.o: $(SRC_DIR)/ota/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译第三方库
$(OBJ_DIR)/cJSON.o: thirdparty/cJSON/cJSON.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/log.o: thirdparty/log.c/log.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译 SQLite3
$(OBJ_DIR)/sqlite3.o: thirdparty/sqlite3/sqlite3.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 编译主程序
$(OBJ_DIR)/main.o: $(SRC_DIR)/main.c
	$(CC) $(CFLAGS) -c -o $@ $<

# 静态库
lib: $(TARGET_LIB)

$(TARGET_LIB): $(OBJS)
	$(AR) rcs $@ $^
	@echo "Static library created: $@"

# 清理
clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# 安装
install: $(TARGET_BIN)
	install -d /usr/local/bin
	install -d /etc/gateway
	install -d /etc/gateway/config
	install -d /var/log
	install -d /var/lib/gateway
	install -m 755 $(TARGET_BIN) /usr/local/bin/
	install -m 644 gateway.ini /etc/gateway/
	install -m 644 config/*.ini /etc/gateway/config/
	@echo "Install complete"

# 运行测试
test: $(TARGET_BIN)
	./$(TARGET_BIN)

# 帮助
help:
	@echo "Available targets:"
	@echo "  all       - Build gateway (default)"
	@echo "  lib       - Build static library"
	@echo "  clean     - Remove build files"
	@echo "  install   - Install to system"
	@echo "  test      - Run gateway"
	@echo ""
	@echo "Options:"
	@echo "  TARGET=x86   - Build for x86 (default)"
	@echo "  TARGET=arm   - Build for ARM (cross-compile)"
	@echo "  DDS_HOME=... - Cyclone DDS install path (default: /home/nvidia/cyclonedds/install)"
	@echo ""
	@echo "Dependencies (Ubuntu/Debian):"
	@echo "  sudo apt-get install libpaho-mqtt-dev libsqlite3-dev libcurl4-openssl-dev libssl-dev"

.PHONY: all dirs lib clean install test help
