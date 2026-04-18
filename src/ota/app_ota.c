/**
 * @file app_ota.c
 * @brief A/B分区OTA升级系统实现
 * 
 * 功能说明：
 * - A/B分区双备份升级方案
 * - RSA签名验证确保固件安全
 * - 自动回滚机制
 * - 下载进度回调
 * - 状态机管理升级流程
 */

#include "app_ota.h"
#include "app_config.h"
#include "../thirdparty/log.c/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <curl/curl.h>
#include <openssl/sha.h>
#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>

#define OTA_CONFIG_FILE "/etc/gateway/boot.conf" // OTA启动配置文件路径
#define OTA_CONFIG_PUBLIC_KEY "/etc/gateway/public.key" // OTA公钥路径
#define OTA_CONFIG_VERSION_FILE "/etc/gateway/version" // OTA版本文件路径
#define OTA_CONFIG_PARTITION_A "/dev/mmcblk0p2" // 默认A分区设备
#define OTA_CONFIG_PARTITION_B "/dev/mmcblk0p3" // 默认B分区设备

/* 启动配置文件格式常量 */
#define BOOT_CONFIG_MAGIC   0x4F544142  // 魔数"OTAB"，用于校验配置文件
#define BOOT_CONFIG_VERSION 1            // 配置文件版本号

/**
 * @brief 启动配置结构体
 * 存储在/etc/gateway/boot.conf中，用于记录当前活跃分区和升级状态。
 */
typedef struct {
    uint32_t magic;              // 魔数，用于校验配置有效性
    uint32_t version;            // 配置版本
    uint32_t active_partition;   // 当前活跃分区：0=A分区，1=B分区
    uint32_t boot_count;         // 从该分区启动的次数
    uint32_t upgrade_pending;    // 是否有待处理的升级：0=无，1=有
    uint32_t checksum;           // 校验和
} BootConfig;

/* OTA状态字符串数组，用于日志输出 */
static const char *state_strings[] = {
    "IDLE",        // 空闲
    "CHECKING",    // 检查更新中
    "DOWNLOADING", // 下载中
    "VERIFYING",   // 验证中
    "INSTALLING",  // 安装中
    "REBOOTING",   // 重启中
    "SUCCESS",     // 成功
    "FAILED",      // 失败
    "ROLLBACK"     // 回滚中
};

/**
 * @brief 获取OTA状态字符串
 * @param state OTA状态枚举值
 * @return 状态字符串
 */
const char *ota_statestr(OtaState state)
{
    if (state >= OTA_STATE_IDLE && state <= OTA_STATE_ROLLBACK) {
        return state_strings[state];
    }
    return "UNKNOWN";
}

/* OTA错误字符串数组，用于错误信息输出 */
static const char *error_strings[] = {
    "OK",                          // 成功
    "Network error",               // 网络错误
    "Download failed",             // 下载失败
    "Checksum error",              // 校验和错误
    "Signature verification failed", // 签名验证失败
    "Write error",                 // 写入错误
    "Boot error",                  // 启动错误
    "Rollback failed",             // 回滚失败
    "Invalid parameter"            // 无效参数
};

/**
 * @brief 获取OTA错误字符串
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *ota_strerror(OtaError error)
{
    if (error >= OTA_ERR_NETWORK && error <= OTA_ERR_INVALID) {
        return error_strings[-error];
    }
    return "Unknown error";
}

/**
 * @brief 计算文件的SHA256哈希值
 * 使用OpenSSL库计算指定文件的SHA256哈希值。
 * @param file_path 文件路径
 * @param hash 输出哈希值缓冲区（32字节）
 * @return 0成功，-1失败
 */
static int calculate_sha256(const char *file_path, uint8_t *hash)
{
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        log_error("Failed to open file: %s", file_path);
        return -1;
    }
    
    // 初始化SHA256上下文
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    
    uint8_t buffer[4096];
    size_t len;
    
    // 分块读取文件并更新哈希
    while ((len = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        SHA256_Update(&ctx, buffer, len);
    }
    
    // 计算最终哈希值
    SHA256_Final(hash, &ctx);
    fclose(fp);
    
    return 0;
}

/**
 * @brief 验证固件RSA签名
 * 使用预置的公钥验证固件文件的RSA签名。
 * 确保固件来源可信，防止恶意固件注入。
 * @param file_path 固件文件路径
 * @param sig_path 签名文件路径
 * @param pub_key_path 公钥文件路径
 * @return 0验证通过，-1验证失败
 */
static int verify_signature(const char *file_path, const char *sig_path,
                            const char *pub_key_path)
{
    // 计算固件文件的SHA256哈希
    uint8_t hash[SHA256_DIGEST_LENGTH];
    if (calculate_sha256(file_path, hash) != 0) {
        return -1;
    }
    
    // 读取签名文件
    FILE *sig_fp = fopen(sig_path, "rb");
    if (!sig_fp) {
        log_error("Failed to open signature file");
        return -1;
    }
    
    uint8_t signature[256];
    size_t sig_len = fread(signature, 1, sizeof(signature), sig_fp);
    fclose(sig_fp);
    
    // 读取公钥文件
    FILE *pub_fp = fopen(pub_key_path, "r");
    if (!pub_fp) {
        log_error("Failed to open public key file");
        return -1;
    }
    
    RSA *rsa = PEM_read_RSA_PUBKEY(pub_fp, NULL, NULL, NULL);
    fclose(pub_fp);
    
    if (!rsa) {
        log_error("Failed to read public key");
        return -1;
    }
    
    // 使用RSA公钥验证签名
    int ret = RSA_verify(NID_sha256, hash, SHA256_DIGEST_LENGTH,
                        signature, sig_len, rsa);
    
    RSA_free(rsa);
    
    if (ret != 1) {
        log_error("Signature verification failed");
        return -1;
    }
    
    log_info("Signature verification passed");
    return 0;
}

/**
 * @brief 读取启动配置文件
 * 从指定路径读取BootConfig结构体。
 * @param path 配置文件路径
 * @param config 输出配置结构体
 * @return 0成功，-1失败
 */
static int read_boot_config(const char *path, BootConfig *config)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        log_error("Failed to open boot config file: %s", path);
        return -1;
    }
    
    ssize_t len = read(fd, config, sizeof(BootConfig));
    close(fd);
    
    if (len != sizeof(BootConfig)) {
        log_error("Failed to read boot config");  
        return -1;
    }
    
    // 验证魔数，确保配置文件有效
    if (config->magic != BOOT_CONFIG_MAGIC) {
        log_error("Invalid boot config magic");
        return -1;
    }
    return 0;
}

/**
 * @brief 写入启动配置文件
 * 将BootConfig结构体写入指定路径，并计算校验和。
 * @param path 配置文件路径
 * @param config 配置结构体
 * @return 0成功，-1失败
 */
static int write_boot_config(const char *path, BootConfig *config)
{
    int fd = open(path, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) {
        log_error("Failed to open boot config for writing");
        return -1;
    }
    
    // 设置魔数和版本
    config->magic = BOOT_CONFIG_MAGIC;
    config->version = BOOT_CONFIG_VERSION;
    
    // 计算校验和（简单累加和）
    config->checksum = 0;
    uint8_t *p = (uint8_t *)config;
    uint32_t sum = 0;
    for (size_t i = 0; i < sizeof(BootConfig) - 4; i++) {
        sum += p[i];
    }
    config->checksum = sum;
    
    ssize_t len = write(fd, config, sizeof(BootConfig));
    close(fd);
    
    if (len != sizeof(BootConfig)) {
        return -1;
    }
    
    return 0;
}

/**
 * @brief CURL写入回调函数
 * 用于将下载的数据写入文件。
 * @param ptr 数据指针
 * @param size 元素大小
 * @param nmemb 元素数量
 * @param userdata 用户数据（文件指针）
 * @return 实际写入字节数
 */
static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    FILE *fp = (FILE *)userdata;
    return fwrite(ptr, size, nmemb, fp);
}

/**
 * @brief CURL进度回调函数
 * 用于报告下载进度。
 * @param userdata 用户数据（OTA管理器指针）
 * @param dltotal 总下载字节数
 * @param dlnow 已下载字节数
 * @param ultotal 总上传字节数（未使用）
 * @param ulnow 已上传字节数（未使用）
 * @return 0继续下载，非0中止
 */
static int progress_callback(void *userdata, curl_off_t dltotal, curl_off_t dlnow,
                            curl_off_t ultotal, curl_off_t ulnow)
{
    OtaManager *manager = (OtaManager *)userdata;
    
    // 计算下载进度百分比
    if (dltotal > 0) {
        manager->download_progress = (int)(dlnow * 100 / dltotal);
        // 调用进度回调
        if (manager->on_progress) {
            manager->on_progress(manager, manager->download_progress);
        }
    }
    
    return 0;
}

/**
 * @brief 初始化OTA管理器
 * 初始化OTA管理器，读取当前分区状态和版本信息。
 * @param manager OTA管理器指针
 * @param config 配置参数，NULL使用默认配置
 * @return 0成功，-1失败
 */
int ota_init(OtaManager *manager, const OtaConfig *config)
{
    if (!manager) return -1;
    
    // 清零管理器结构体
    memset(manager, 0, sizeof(OtaManager));
    
    // 加载配置
    if (config) {
        memcpy(&manager->config, config, sizeof(OtaConfig));
    } else {
        // 使用默认配置
        strcpy(manager->config.partition_a, OTA_CONFIG_PARTITION_A);      // A分区设备
        strcpy(manager->config.partition_b, OTA_CONFIG_PARTITION_B);      // B分区设备
        strcpy(manager->config.boot_config, OTA_CONFIG_FILE); // 启动配置文件
        strcpy(manager->config.public_key_path, OTA_CONFIG_PUBLIC_KEY); // 公钥路径
        manager->config.check_interval = 86400;    // 检查间隔：24小时
        manager->config.max_boot_attempts = 3;     // 最大启动尝试次数
        manager->config.auto_rollback = 1;         // 启用自动回滚
    }
    
    // 初始化A分区信息
    strncpy(manager->partition_a.name, "A", sizeof(manager->partition_a.name));// A分区名称
    strncpy(manager->partition_a.device, manager->config.partition_a,// A分区设备
            sizeof(manager->partition_a.device));
    
    // 初始化B分区信息
    strncpy(manager->partition_b.name, "B", sizeof(manager->partition_b.name));
    strncpy(manager->partition_b.device, manager->config.partition_b,
            sizeof(manager->partition_b.device));
    
    // 读取启动配置，确定当前活跃分区
    BootConfig boot_cfg;
    if (read_boot_config(manager->config.boot_config, &boot_cfg) == 0) {// 启动配置有效
        if (boot_cfg.active_partition == 0) {// A分区为活动分区
            manager->active_partition = &manager->partition_a;
            manager->inactive_partition = &manager->partition_b;
        } else {
            manager->active_partition = &manager->partition_b;
            manager->inactive_partition = &manager->partition_a;
        }
        manager->active_partition->boot_count = boot_cfg.boot_count;
    } else {
        // 默认使用A分区
        manager->active_partition = &manager->partition_a;
        manager->inactive_partition = &manager->partition_b;
    }
    
    // 读取当前版本号
    FILE *fp = fopen(OTA_CONFIG_VERSION_FILE, "r");
    if (fp) {
        fgets(manager->current_version, sizeof(manager->current_version), fp);
        fclose(fp);
        // 去除换行符
        char *nl = strchr(manager->current_version, '\n');
        if (nl) *nl = '\0';
    }
    
    manager->state = OTA_STATE_IDLE;
    manager->is_initialized = 1;
    
    log_info("OTA initialized: current=%s, active=%s",
             manager->current_version, manager->active_partition->name);
    
    return 0;
}

/**
 * @brief 使用默认配置初始化OTA管理器
 * @param manager OTA管理器指针
 * @return 0成功，-1失败
 */
int ota_init_default(OtaManager *manager)
{
    return ota_init(manager, NULL);
}

/**
 * @brief 关闭OTA管理器
 * @param manager OTA管理器指针
 */
void ota_close(OtaManager *manager)
{
    if (!manager) return;
    
    manager->is_initialized = 0;
    log_info("OTA closed");
}

/**
 * @brief 检查是否有可用更新
 * 从服务器下载版本信息文件，比较版本号。
 * @param manager OTA管理器指针
 * @param out_version 输出新版本号
 * @param max_len 输出缓冲区大小
 * @return 0有更新，1无更新，-1失败
 */
int ota_check_update(OtaManager *manager, char *out_version, size_t max_len)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = OTA_STATE_CHECKING;
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_CHECKING);
    }
    
    // 初始化CURL
    CURL *curl = curl_easy_init();
    if (!curl) {
        log_error("Failed to init curl");
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 构建版本文件URL
    char version_url[512];
    snprintf(version_url, sizeof(version_url), "%s/version.txt",
             manager->config.version_url);
    
    // 下载版本信息到临时文件
    FILE *fp = tmpfile();// 创建临时文件
    curl_easy_setopt(curl, CURLOPT_URL, version_url);// 设置URL
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);// 设置写入目标
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);// 设置超时时间30秒
    
    CURLcode res = curl_easy_perform(curl);// 执行下载
    curl_easy_cleanup(curl);// 清理CURL资源
    
    if (res != CURLE_OK) {// 检查下载是否成功
        log_error("Failed to fetch version info: %s", curl_easy_strerror(res));
        fclose(fp);
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 读取版本号
    rewind(fp);// 重置文件指针到开头
    char new_version[64] = {0};
    fgets(new_version, sizeof(new_version), fp);// 读取版本号
    fclose(fp);
    
    // 去除换行符
    char *nl = strchr(new_version, '\n');
    if (nl) *nl = '\0';
    
    // 比较版本号
    if (strcmp(new_version, manager->current_version) > 0) {
        strncpy(manager->new_version, new_version, sizeof(manager->new_version) - 1);
        if (out_version && max_len > 0) {
            strncpy(out_version, new_version, max_len - 1);
        }
        
        log_info("New version available: %s", new_version);
        manager->state = OTA_STATE_IDLE;
        return 0;  // 有新版本
    }

    log_debug("No update available");
    manager->state = OTA_STATE_IDLE;
    return 1;  // 无新版本
}

/**
 * @brief 下载固件
 * 从服务器下载新版本固件到临时目录。
 * @param manager OTA管理器指针
 * @return 0成功，-1失败
 */
int ota_download(OtaManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = OTA_STATE_DOWNLOADING;
    manager->download_progress = 0;
    
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_DOWNLOADING);
    }
    
    // 构建固件下载URL
    char firmware_url[512];
    snprintf(firmware_url, sizeof(firmware_url), "%s/firmware-%s.bin",
             manager->config.firmware_url, manager->new_version);
    
    log_info("Downloading firmware from: %s", firmware_url);
    
    // 打开临时文件
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "/tmp/firmware-%s.bin", manager->new_version);
    
    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        log_error("Failed to create temp file");
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 初始化CURL并下载
    CURL *curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 设置CURL选项
    curl_easy_setopt(curl, CURLOPT_URL, firmware_url);// 设置URL
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);// 设置写入目标
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);  // 进度回调
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, manager); // 传递进度回调参数
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);// 启用进度回调
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);           // 超时5分钟
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);      // 跟随重定向
    
    CURLcode res = curl_easy_perform(curl);// 执行下载
    curl_easy_cleanup(curl);
    fclose(fp);
    
    if (res != CURLE_OK) {// 检查下载是否成功
        log_error("Download failed: %s", curl_easy_strerror(res));
        remove(tmp_path);
        manager->state = OTA_STATE_FAILED;
        
        if (manager->on_error) {
            manager->on_error(manager, OTA_ERR_DOWNLOAD);
        }
        return -1;
    }
    
    log_info("Download complete: %s", tmp_path);
    manager->state = OTA_STATE_IDLE;
    return 0;
}

/**
 * @brief 验证固件签名
 * @param manager OTA管理器指针
 * @return 0验证通过，-1验证失败
 */
int ota_verify(OtaManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = OTA_STATE_VERIFYING;
    
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_VERIFYING);
    }
    
    // 固件文件路径
    char firmware_path[256];
    snprintf(firmware_path, sizeof(firmware_path), "/tmp/firmware-%s.bin",
             manager->new_version);
    
    // 签名文件路径
    char sig_path[256];
    snprintf(sig_path, sizeof(sig_path), "/tmp/firmware-%s.sig",
             manager->new_version);
    
    // 下载签名文件
    char sig_url[512];
    snprintf(sig_url, sizeof(sig_url), "%s/firmware-%s.sig",
             manager->config.firmware_url, manager->new_version);
    
    CURL *curl = curl_easy_init();
    if (curl) {
        FILE *fp = fopen(sig_path, "wb");
        if (fp) {
            curl_easy_setopt(curl, CURLOPT_URL, sig_url);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
            curl_easy_perform(curl);
            fclose(fp);
        }
        curl_easy_cleanup(curl);
    }
    
    // 验证RSA签名
    if (verify_signature(firmware_path, sig_path,
                        manager->config.public_key_path) != 0) {
        log_error("Signature verification failed");
        manager->state = OTA_STATE_FAILED;
        
        if (manager->on_error) {
            manager->on_error(manager, OTA_ERR_SIGNATURE);
        }
        return -1;
    }
    
    log_info("Firmware verified successfully");
    manager->state = OTA_STATE_IDLE;
    return 0;
}

/**
 * @brief 安装固件
 * 将固件写入非活跃分区，更新启动配置。
 * @param manager OTA管理器指针
 * @return 0成功，-1失败
 */
int ota_install(OtaManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = OTA_STATE_INSTALLING;
    
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_INSTALLING);
    }
    
    // 固件文件路径
    char firmware_path[256];
    snprintf(firmware_path, sizeof(firmware_path), "/tmp/firmware-%s.bin",
             manager->new_version);
    
    // 打开固件文件
    FILE *fp = fopen(firmware_path, "rb");
    if (!fp) {
        log_error("Failed to open firmware file");
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 获取固件文件大小
    fseek(fp, 0, SEEK_END);
    size_t firmware_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // 打开目标分区设备
    int fd = open(manager->inactive_partition->device, O_WRONLY);
    if (fd < 0) {
        log_error("Failed to open partition: %s",
                  manager->inactive_partition->device);
        fclose(fp);
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 写入固件到分区
    uint8_t buffer[4096];
    size_t total_written = 0;
    ssize_t len;
    
    while ((len = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        ssize_t written = write(fd, buffer, len);
        if (written != len) {
            log_error("Write failed: %zd != %zd", written, len);
            close(fd);
            fclose(fp);
            manager->state = OTA_STATE_FAILED;
            
            if (manager->on_error) {
                manager->on_error(manager, OTA_ERR_WRITE);
            }
            return -1;
        }
        total_written += written;
        
        // 更新写入进度
        manager->download_progress = (int)(total_written * 100 / firmware_size);
        if (manager->on_progress) {
            manager->on_progress(manager, manager->download_progress);
        }
    }
    
    // 确保数据写入磁盘
    fsync(fd);
    close(fd);
    fclose(fp);
    
    log_info("Firmware written to partition %s: %zu bytes",
             manager->inactive_partition->name, total_written);
    
    // 更新启动配置
    BootConfig boot_cfg;
    read_boot_config(manager->config.boot_config, &boot_cfg);
    
    // 设置新的活跃分区
    boot_cfg.active_partition = (manager->inactive_partition == &manager->partition_b) ? 1 : 0;
    boot_cfg.boot_count = 0;
    boot_cfg.upgrade_pending = 1;  // 标记有待处理的升级
    
    if (write_boot_config(manager->config.boot_config, &boot_cfg) != 0) {
        log_error("Failed to update boot config");
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    // 清理临时文件
    remove(firmware_path);
    
    log_info("Installation complete, upgrade pending");
    manager->state = OTA_STATE_SUCCESS;
    
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_SUCCESS);
    }
    
    return 0;
}

/**
 * @brief 执行完整OTA升级流程
 * 依次执行：检查更新 -> 下载 -> 验证 -> 安装
 * @param manager OTA管理器指针
 * @return 0成功，1无更新，-1失败
 */
int ota_upgrade(OtaManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    log_info("Starting OTA upgrade...");
    
    // 步骤1：检查更新
    char version[64];
    if (ota_check_update(manager, version, sizeof(version)) != 0) {
        log_info("No update available");
        return 1;
    }
    
    // 步骤2：下载固件
    if (ota_download(manager) != 0) {
        log_error("Download failed");
        return -1;
    }
    
    // 步骤3：验证签名
    if (ota_verify(manager) != 0) {
        log_error("Verification failed");
        return -1;
    }
    
    // 步骤4：安装固件
    if (ota_install(manager) != 0) {
        log_error("Installation failed");
        return -1;
    }
    
    log_info("OTA upgrade successful, reboot required");
    return 0;
}

/**
 * @brief 执行回滚操作
 * 将启动配置切换回之前的分区，用于升级失败后恢复。
 * @param manager OTA管理器指针
 * @return 0成功，-1失败
 */
int ota_rollback(OtaManager *manager)
{
    if (!manager || !manager->is_initialized) return -1;
    
    manager->state = OTA_STATE_ROLLBACK;
    
    if (manager->on_state_changed) {
        manager->on_state_changed(manager, OTA_STATE_ROLLBACK);
    }
    
    log_warn("Performing rollback...");
    
    // 读取当前启动配置
    BootConfig boot_cfg;
    read_boot_config(manager->config.boot_config, &boot_cfg);
    
    // 切换回之前的分区
    boot_cfg.active_partition = (boot_cfg.active_partition == 0) ? 1 : 0;
    boot_cfg.boot_count = 0;
    boot_cfg.upgrade_pending = 0;  // 清除升级标记
    
    if (write_boot_config(manager->config.boot_config, &boot_cfg) != 0) {
        log_error("Failed to update boot config for rollback");
        manager->state = OTA_STATE_FAILED;
        return -1;
    }
    
    log_info("Rollback complete, reboot required");
    manager->state = OTA_STATE_IDLE;
    
    return 0;
}

/**
 * @brief 获取当前OTA状态
 * @param manager OTA管理器指针
 * @return OTA状态枚举值
 */
OtaState ota_get_state(OtaManager *manager)
{
    return manager ? manager->state : OTA_STATE_IDLE;
}

/**
 * @brief 获取下载/安装进度
 * @param manager OTA管理器指针
 * @return 进度百分比（0-100）
 */
int ota_get_progress(OtaManager *manager)
{
    return manager ? manager->download_progress : 0;
}

/**
 * @brief 获取当前版本号
 * @param manager OTA管理器指针
 * @param out_version 输出版本号缓冲区
 * @param max_len 缓冲区大小
 * @return 0成功，-1失败
 */
int ota_get_current_version(OtaManager *manager, char *out_version, size_t max_len)
{
    if (!manager || !out_version) return -1;
    
    strncpy(out_version, manager->current_version, max_len - 1);
    out_version[max_len - 1] = '\0';
    return 0;
}

/**
 * @brief 获取分区信息
 * @param manager OTA管理器指针
 * @param partition_a 输出A分区信息
 * @param partition_b 输出B分区信息
 */
void ota_get_partitions(OtaManager *manager, OtaPartition *partition_a, OtaPartition *partition_b)
{
    if (!manager) return;
    
    if (partition_a) memcpy(partition_a, &manager->partition_a, sizeof(OtaPartition));
    if (partition_b) memcpy(partition_b, &manager->partition_b, sizeof(OtaPartition));
}

/**
 * @brief 设置状态变化回调
 * @param manager OTA管理器指针
 * @param callback 回调函数指针
 */
void ota_on_state_changed(OtaManager *manager,
                          void (*callback)(OtaManager *, OtaState))
{
    if (manager) manager->on_state_changed = callback;
}

/**
 * @brief 设置进度回调
 * @param manager OTA管理器指针
 * @param callback 回调函数指针
 */
void ota_on_progress(OtaManager *manager,
                     void (*callback)(OtaManager *, int))
{
    if (manager) manager->on_progress = callback;
}

/**
 * @brief 设置错误回调
 * @param manager OTA管理器指针
 * @param callback 回调函数指针
 */
void ota_on_error(OtaManager *manager,
                  void (*callback)(OtaManager *, OtaError))
{
    if (manager) manager->on_error = callback;
}
