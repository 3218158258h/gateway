# test/Makefile - 编译 DDS 测试程序

CC = gcc

# 目录
IDL_DIR = ../idl
DDS_SRC_DIR = ../src/dds

# DDS 库路径
DDS_HOME = /opt/ros/humble
DDS_INCLUDE = $(DDS_HOME)/include
DDS_LIB = $(DDS_HOME)/lib/aarch64-linux-gnu

# 编译选项
CFLAGS = -Wall -g
CFLAGS += -I$(IDL_DIR)
CFLAGS += -I$(DDS_INCLUDE)

# 链接选项
LDFLAGS = -L$(DDS_LIB)
LDFLAGS += -lddsc
LDFLAGS += -Wl,-rpath,$(DDS_LIB)

# 源文件
SUB_DATA_SRC = sub_data.c
SUB_CMD_SRC = sub_cmd.c
PUBLISHER_SRC = publisher.c
GATEWAY_DATA_SRC = $(DDS_SRC_DIR)/GatewayData.c

# 目标
all: sub_data publisher sub_cmd

sub_data: $(SUB_DATA_SRC) $(GATEWAY_DATA_SRC)
	$(CC) $(CFLAGS) -o $@ $(SUB_DATA_SRC) $(GATEWAY_DATA_SRC) $(LDFLAGS)
	@echo "编译完成: sub_data"

sub_cmd: $(SUB_CMD_SRC) $(GATEWAY_DATA_SRC)
	$(CC) $(CFLAGS) -o $@ $(SUB_CMD_SRC) $(GATEWAY_DATA_SRC) $(LDFLAGS)
	@echo "编译完成: sub_cmd"

publisher: $(PUBLISHER_SRC) $(GATEWAY_DATA_SRC)
	$(CC) $(CFLAGS) -o $@ $(PUBLISHER_SRC) $(GATEWAY_DATA_SRC) $(LDFLAGS)
	@echo "编译完成: publisher"

clean:
	rm -f sub_data publisher sub_cmd
.PHONY: all clean help
