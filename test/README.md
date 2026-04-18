- 该目录下的订阅者发布者用来测试网关在dds模式下的功能
# 编译
gcc -o subscriber subscriber.c GatewayData.c \
    -I../src/dds \
    -I${DDS_HOME}/include \
    -L${DDS_HOME}/lib \
    -lddsc \
    -Wl,-rpath,${DDS_HOME}/lib


gcc -o publisher publisher.c -lddsc

# 运行
export LD_LIBRARY_PATH=/home/nvidia/cyclonedds/install/lib:$LD_LIBRARY_PATH

./dds_subscriber

./dds_publisher
