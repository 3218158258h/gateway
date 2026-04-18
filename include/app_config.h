/**
 * @file app_config.h
 * @brief 配置管理系统 - 支持INI格式配置文件解析
 * 
 * 功能：
 * - INI格式配置文件解析
 * - 配置项读取/写入
 * - 参数类型转换
 * - 配置热加载
 * 
 * @author Gateway Team
 * @version 2.0
 */

#ifndef __APP_CONFIG_H__
#define __APP_CONFIG_H__

#include <stddef.h>

/* 最大配置项数量 */
#define CONFIG_MAX_ITEMS 128
/* 最大键名长度 */
#define CONFIG_MAX_KEY_LEN 64
/* 最大值长度 */
#define CONFIG_MAX_VALUE_LEN 256
/* 最大节名长度 */
#define CONFIG_MAX_SECTION_LEN 64
/* 最大配置文件路径长度 */
#define CONFIG_MAX_PATH_LEN 256

/* 配置值类型 */
typedef enum {
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_FLOAT
} ConfigType;

/* 单个配置项 */
typedef struct ConfigItem {
    char section[CONFIG_MAX_SECTION_LEN];   /* 节名 */
    char key[CONFIG_MAX_KEY_LEN];           /* 键名 */
    char value[CONFIG_MAX_VALUE_LEN];       /* 值(字符串形式) */
    ConfigType type;                         /* 类型 */
    char description[256];                   /* 描述 */
} ConfigItem;

/* 配置管理器 */
typedef struct ConfigManager {
    char file_path[CONFIG_MAX_PATH_LEN];    /* 配置文件路径 */
    ConfigItem items[CONFIG_MAX_ITEMS];     /* 配置项数组 */
    int item_count;                          /* 配置项数量 */
    int is_loaded;                           /* 是否已加载 */
} ConfigManager;

/* ========== 全局配置管理器 ========== */

/**
 * @brief 获取全局配置管理器
 * @return 配置管理器指针
 */
ConfigManager *config_get_global(void);

/**
 * @brief 设置全局配置管理器
 * @param config 配置管理器指针
 */
void config_set_global(ConfigManager *config);

/* ========== 初始化与销毁 ========== */

/**
 * @brief 初始化配置管理器
 * @param config 配置管理器指针
 * @param file_path 配置文件路径
 * @return 0成功, -1失败
 */
int config_init(ConfigManager *config, const char *file_path);

/**
 * @brief 销毁配置管理器
 * @param config 配置管理器指针
 */
void config_destroy(ConfigManager *config);

/* ========== 配置文件操作 ========== */

/**
 * @brief 加载配置文件
 * @param config 配置管理器指针
 * @return 0成功, -1失败
 */
int config_load(ConfigManager *config);

/**
 * @brief 保存配置到文件
 * @param config 配置管理器指针
 * @return 0成功, -1失败
 */
int config_save(ConfigManager *config);

/**
 * @brief 重新加载配置文件(热加载)
 * @param config 配置管理器指针
 * @return 0成功, -1失败
 */
int config_reload(ConfigManager *config);

/* ========== 配置项读取 ========== */

/**
 * @brief 获取字符串配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @param out_value 输出缓冲区
 * @param max_len 缓冲区最大长度
 * @return 0成功, -1失败(使用默认值)
 */
int config_get_string(ConfigManager *config, const char *section, const char *key,
                      const char *default_value, char *out_value, size_t max_len);

/**
 * @brief 获取整数配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值
 */
int config_get_int(ConfigManager *config, const char *section, const char *key,
                   int default_value);

/**
 * @brief 获取布尔配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值
 */
int config_get_bool(ConfigManager *config, const char *section, const char *key,
                    int default_value);

/**
 * @brief 获取浮点数配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值
 */
float config_get_float(ConfigManager *config, const char *section, const char *key,
                       float default_value);

/* ========== 配置项设置 ========== */

/**
 * @brief 设置字符串配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功, -1失败
 */
int config_set_string(ConfigManager *config, const char *section, const char *key,
                      const char *value);

/**
 * @brief 设置整数配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功, -1失败
 */
int config_set_int(ConfigManager *config, const char *section, const char *key,
                   int value);

/**
 * @brief 设置布尔配置
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功, -1失败
 */
int config_set_bool(ConfigManager *config, const char *section, const char *key,
                    int value);

/* ========== 调试功能 ========== */

/**
 * @brief 打印所有配置项(调试用)
 * @param config 配置管理器指针
 */
void config_print_all(ConfigManager *config);

#endif /* __APP_CONFIG_H__ */
