/**
 * @file app_ota.h
 * @brief A/B 分区 OTA 升级系统
 *
 * 功能：
 * - A/B 双分区升级
 * - 数字签名验证
 * - 安全启动链
 * - 自动回滚
 * - 升级状态管理
 *
 * @author Gateway Team
 * @version 2.0
 */

#ifndef __APP_OTA_H__
#define __APP_OTA_H__

#include <stddef.h>
#include <stdint.h>

/* OTA 状态。 */
typedef enum {
    OTA_STATE_IDLE = 0,         /* 空闲 */
    OTA_STATE_CHECKING,         /* 检查更新中 */
    OTA_STATE_DOWNLOADING,      /* 下载中 */
    OTA_STATE_VERIFYING,        /* 验证中 */
    OTA_STATE_INSTALLING,       /* 安装中 */
    OTA_STATE_REBOOTING,        /* 重启中 */
    OTA_STATE_SUCCESS,          /* 成功 */
    OTA_STATE_FAILED,           /* 失败 */
    OTA_STATE_ROLLBACK          /* 回滚中 */
} OtaState;

/* OTA 错误码。 */
typedef enum {
    OTA_OK = 0,
    OTA_ERR_NETWORK = -1,
    OTA_ERR_DOWNLOAD = -2,
    OTA_ERR_CHECKSUM = -3,
    OTA_ERR_SIGNATURE = -4,
    OTA_ERR_WRITE = -5,
    OTA_ERR_BOOT = -6,
    OTA_ERR_ROLLBACK = -7,
    OTA_ERR_INVALID = -8
} OtaError;

/* 分区信息。 */
typedef struct OtaPartition {
    char name[32];        /* 分区名称。 */
    char device[64];      /* 设备名称。 */
    uint64_t size;        /* 分区大小（字节）。 */
    uint64_t used;        /* 已用空间（字节）。 */
    int is_active;        /* 是否为活动分区。 */
    int is_valid;         /* 是否有效。 */
    int boot_count;       /* 启动次数。 */
} OtaPartition;

/* OTA 配置。 */
typedef struct OtaConfig {
    char version_url[256];      /* 版本信息URL */
    char firmware_url[256];     /* 固件下载URL */
    char public_key_path[256];  /* 公钥路径 */
    char partition_a[64];       /* A分区设备 */
    char partition_b[64];       /* B分区设备 */
    char boot_config[64];       /* 启动配置文件 */
    int check_interval;         /* 检查间隔(秒) */
    int max_boot_attempts;      /* 最大启动尝试次数 */
    int auto_rollback;          /* 是否自动回滚 */
} OtaConfig;

/* OTA 管理器。 */
typedef struct OtaManager {
    OtaConfig config;                /* OTA 配置。 */
    OtaState state;                  /* OTA 状态。 */
    OtaPartition partition_a;        /* A 分区信息。 */
    OtaPartition partition_b;        /* B 分区信息。 */
    OtaPartition *active_partition;  /* 当前活动分区指针。 */
    OtaPartition *inactive_partition; /* 当前非活动分区指针。 */

    char current_version[32];        /* 当前版本号。 */
    char new_version[32];            /* 新版本号。 */
    int download_progress;           /* 下载进度（0-100）。 */
    int is_initialized;              /* 是否已初始化。 */

    /* 回调。 */
    void (*on_state_changed)(struct OtaManager *manager, OtaState state);
    void (*on_progress)(struct OtaManager *manager, int progress);
    void (*on_error)(struct OtaManager *manager, OtaError error);
} OtaManager;

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化OTA管理器
 * @param manager 管理器指针
 * @param config 配置
 * @return 0成功, -1失败
 */
int ota_init(OtaManager *manager, const OtaConfig *config);

/**
 * @brief 使用默认配置初始化
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_init_default(OtaManager *manager);

/**
 * @brief 关闭OTA管理器
 * @param manager 管理器指针
 */
void ota_close(OtaManager *manager);

/* ========== 升级操作 ========== */

/**
 * @brief 检查更新
 * @param manager 管理器指针
 * @param out_version 输出新版本号
 * @param max_len 缓冲区长度
 * @return 0有更新, 1无更新, -1失败
 */
int ota_check_update(OtaManager *manager, char *out_version, size_t max_len);

/**
 * @brief 下载固件
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_download(OtaManager *manager);

/**
 * @brief 验证固件
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_verify(OtaManager *manager);

/**
 * @brief 安装固件
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_install(OtaManager *manager);

/**
 * @brief 执行升级(完整流程)
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_upgrade(OtaManager *manager);

/**
 * @brief 回滚到上一版本
 * @param manager 管理器指针
 * @return 0成功, -1失败
 */
int ota_rollback(OtaManager *manager);

/* ========== 状态查询 ========== */

/**
 * @brief 获取OTA状态
 * @param manager 管理器指针
 * @return 状态
 */
OtaState ota_get_state(OtaManager *manager);

/**
 * @brief 获取下载进度
 * @param manager 管理器指针
 * @return 进度百分比(0-100)
 */
int ota_get_progress(OtaManager *manager);

/**
 * @brief 获取当前版本
 * @param manager 管理器指针
 * @param out_version 输出版本号
 * @param max_len 缓冲区长度
 * @return 0成功, -1失败
 */
int ota_get_current_version(OtaManager *manager, char *out_version, size_t max_len);

/**
 * @brief 获取分区信息
 * @param manager 管理器指针
 * @param partition_a 输出A分区信息
 * @param partition_b 输出B分区信息
 */
void ota_get_partitions(OtaManager *manager, OtaPartition *partition_a, OtaPartition *partition_b);

/* ========== 回调设置 ========== */

/**
 * @brief 设置状态变化回调
 */
void ota_on_state_changed(OtaManager *manager,
                          void (*callback)(OtaManager *, OtaState));

/**
 * @brief 设置进度回调
 */
void ota_on_progress(OtaManager *manager,
                     void (*callback)(OtaManager *, int));

/**
 * @brief 设置错误回调
 */
void ota_on_error(OtaManager *manager,
                  void (*callback)(OtaManager *, OtaError));

/* ========== 工具函数 ========== */

/**
 * @brief 获取错误描述
 * @param error 错误码
 * @return 错误描述字符串
 */
const char *ota_strerror(OtaError error);

/**
 * @brief 获取状态描述
 * @param state 状态
 * @return 状态描述字符串
 */
const char *ota_statestr(OtaState state);

#endif /* __APP_OTA_H__ */
