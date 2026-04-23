/**
 * @file app_config.c
 * @brief 配置管理系统实现，负责 INI 配置的读取与保存
 *
 * 功能说明：
 * - INI 格式配置文件解析与保存
 * - 支持节 [section] 和键值对 key=value
 * - 支持字符串、整数、布尔值、浮点数类型
 * - 全局配置管理器支持
 */

#include "app_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>

/**
 * @brief 安全复制字符串到缓冲区
 *
 * @param out_value 输出缓冲区
 * @param max_len 输出缓冲区长度
 * @param value 源字符串
 */
static void copy_string_value(char *out_value, size_t max_len, const char *value)
{
    if (!out_value || max_len == 0) {
        return;
    }
    if (!value) {
        out_value[0] = '\0';
        return;
    }
    strncpy(out_value, value, max_len - 1);
    out_value[max_len - 1] = '\0';
}

/**
 * @brief 去除字符串首尾空白字符
 * 
 * @param str 输入字符串指针
 * @return 去除空白后的字符串指针
 */
static char *str_trim(char *str)
{
    char *end;
    
    /* 去除前导空白。 */
    while (isspace((unsigned char)*str)) {
        str++;
    }
    
    /* 空字符串直接返回。 */
    if (*str == '\0') {
        return str;
    }
    
    /* 去除尾部空白。 */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';
    
    return str;
}

/**
 * @brief 查找配置项
 * 
 * 在配置管理器中查找指定节和键的配置项。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @return 配置项指针，未找到返回NULL
 */
static ConfigItem *find_item(ConfigManager *config, const char *section, const char *key)
{
    for (int i = 0; i < config->item_count; i++) {
        if (strcmp(config->items[i].section, section) == 0 &&
            strcmp(config->items[i].key, key) == 0) {
            return &config->items[i];
        }
    }
    return NULL;
}

/**
 * @brief 添加或更新配置项
 * 
 * 如果配置项已存在则更新值，否则添加新配置项。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功，-1配置项数量已达上限
 */
static int set_item(ConfigManager *config, const char *section, const char *key, 
                    const char *value)
{
    ConfigItem *item = find_item(config, section, key);
    
    if (item) {
        // 更新已有项的值
        strncpy(item->value, value, CONFIG_MAX_VALUE_LEN - 1);
        item->value[CONFIG_MAX_VALUE_LEN - 1] = '\0';
    } else {
        // 检查是否达到最大配置项数量
        if (config->item_count >= CONFIG_MAX_ITEMS) {
            return -1;
        }
        
        // 添加新配置项
        item = &config->items[config->item_count];
        strncpy(item->section, section, CONFIG_MAX_SECTION_LEN - 1);
        item->section[CONFIG_MAX_SECTION_LEN - 1] = '\0';
        strncpy(item->key, key, CONFIG_MAX_KEY_LEN - 1);
        item->key[CONFIG_MAX_KEY_LEN - 1] = '\0';
        strncpy(item->value, value, CONFIG_MAX_VALUE_LEN - 1);
        item->value[CONFIG_MAX_VALUE_LEN - 1] = '\0';
        item->type = CONFIG_TYPE_STRING;  // 默认为字符串类型
        
        config->item_count++;
    }
    
    return 0;
}

/**
 * @brief 初始化配置管理器
 * 
 * 初始化配置管理器结构体，设置配置文件路径。
 * 
 * @param config 配置管理器指针
 * @param file_path 配置文件路径
 * @return 0成功，-1失败
 */
int config_init(ConfigManager *config, const char *file_path)
{
    if (!config || !file_path) {
        return -1;
    }
    
    // 清零结构体
    memset(config, 0, sizeof(ConfigManager));
    
    // 设置配置文件路径
    strncpy(config->file_path, file_path, CONFIG_MAX_PATH_LEN - 1);
    config->file_path[CONFIG_MAX_PATH_LEN - 1] = '\0';
    config->is_loaded = 0;
    
    return 0;
}

/**
 * @brief 销毁配置管理器
 * 
 * 释放配置管理器资源。
 * 
 * @param config 配置管理器指针
 */
void config_destroy(ConfigManager *config)
{
    if (config) {
        memset(config, 0, sizeof(ConfigManager));
    }
}

/**
 * @brief 加载配置文件
 * 
 * 解析INI格式配置文件，读取所有配置项。
 * 支持节[section]、键值对key=value、注释#和;。
 * 
 * @param config 配置管理器指针
 * @return 0成功，-1失败
 */
int config_load(ConfigManager *config)
{
    if (!config || !config->file_path[0]) {
        return -1;
    }
    
    // 打开配置文件
    FILE *fp = fopen(config->file_path, "r");
    if (!fp) {
        return -1;
    }
    
    char line[512];
    char current_section[CONFIG_MAX_SECTION_LEN] = {0};  // 当前节名
    int line_num = 0;
    
    // 逐行读取配置文件
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char *p = str_trim(line);
        
        // 跳过空行和注释行
        if (*p == '\0' || *p == '#' || *p == ';') {
            continue;
        }
        
        // 解析节名 [section]
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, str_trim(p + 1), CONFIG_MAX_SECTION_LEN - 1);
                current_section[CONFIG_MAX_SECTION_LEN - 1] = '\0';
            }
            continue;
        }
        
        // 解析键值对 key = value
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = str_trim(p);
            char *value = str_trim(eq + 1);
            
            // 去除值两端的引号
            if (*value == '"' || *value == '\'') {
                char quote = *value;
                value++;
                char *end = strrchr(value, quote);
                if (end) {
                    *end = '\0';
                }
            }
            
            // 添加配置项
            if (current_section[0] && key[0]) {
                set_item(config, current_section, key, value);
            }
        }
    }
    
    fclose(fp);
    config->is_loaded = 1;
    
    return 0;
}

/**
 * @brief 保存配置到文件
 * 
 * 将当前配置保存到配置文件，按节分组输出。
 * 
 * @param config 配置管理器指针
 * @return 0成功，-1失败
 */
int config_save(ConfigManager *config)
{
    if (!config || !config->file_path[0]) {
        return -1;
    }
    
    // 打开文件写入
    FILE *fp = fopen(config->file_path, "w");
    if (!fp) {
        return -1;
    }
    
    // 写入文件头注释
    fprintf(fp, "# Gateway Configuration File\n");
    fprintf(fp, "# Auto-generated by ConfigManager\n\n");
    
    char current_section[CONFIG_MAX_SECTION_LEN] = {0};
    
    // 遍历所有配置项并写入文件
    for (int i = 0; i < config->item_count; i++) {
        ConfigItem *item = &config->items[i];
        
        // 写入节名（如果与上一项不同）
        if (strcmp(current_section, item->section) != 0) {
            snprintf(current_section, sizeof(current_section), "%s", item->section);
            fprintf(fp, "\n[%s]\n", item->section);
        }
        
        // 写入键值对
        fprintf(fp, "%s = %s\n", item->key, item->value);
    }
    
    fclose(fp);
    return 0;
}

/**
 * @brief 获取字符串配置值
 * 
 * 从指定节获取指定键的字符串值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值（未找到时使用）
 * @param out_value 输出缓冲区
 * @param max_len 输出缓冲区大小
 * @return 0成功，-1未找到
 */
int config_get_string(ConfigManager *config, const char *section, const char *key,
                      const char *default_value, char *out_value, size_t max_len)
{
    if (!out_value || max_len == 0) {
        return -1;
    }

    // 参数校验
    if (!config || !section || !key) {
        copy_string_value(out_value, max_len, default_value);
        return -1;
    }
    
    // 查找配置项
    ConfigItem *item = find_item(config, section, key);
    if (item) {
        copy_string_value(out_value, max_len, item->value);
        return 0;
    }
    
    // 未找到，使用默认值
    copy_string_value(out_value, max_len, default_value);
    return -1;
}

/**
 * @brief 获取整数配置值
 * 
 * 从指定节获取指定键的整数值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值
 */
int config_get_int(ConfigManager *config, const char *section, const char *key,
                   int default_value)
{
    char value[CONFIG_MAX_VALUE_LEN];
    
    if (config_get_string(config, section, key, NULL, value, sizeof(value)) == 0) {
        return atoi(value);  // 字符串转整数
    }
    
    return default_value;
}

/**
 * @brief 获取布尔配置值
 * 
 * 从指定节获取指定键的布尔值。
 * 支持：true/false, yes/no, 1/0
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值（1为真，0为假）
 */
int config_get_bool(ConfigManager *config, const char *section, const char *key,
                    int default_value)
{
    char value[CONFIG_MAX_VALUE_LEN];
    
    if (config_get_string(config, section, key, NULL, value, sizeof(value)) == 0) {
        // 支持多种布尔值格式
        if (strcasecmp(value, "true") == 0 || 
            strcasecmp(value, "yes") == 0 ||
            strcmp(value, "1") == 0) {
            return 1;
        }
        if (strcasecmp(value, "false") == 0 || 
            strcasecmp(value, "no") == 0 ||
            strcmp(value, "0") == 0) {
            return 0;
        }
    }
    
    return default_value;
}

/**
 * @brief 获取浮点数配置值
 * 
 * 从指定节获取指定键的浮点数值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param default_value 默认值
 * @return 配置值或默认值
 */
float config_get_float(ConfigManager *config, const char *section, const char *key,
                       float default_value)
{
    char value[CONFIG_MAX_VALUE_LEN];
    
    if (config_get_string(config, section, key, NULL, value, sizeof(value)) == 0) {
        return (float)atof(value);  // 字符串转浮点数
    }
    
    return default_value;
}

/**
 * @brief 设置字符串配置值
 * 
 * 设置指定节和键的字符串值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功，-1失败
 */
int config_set_string(ConfigManager *config, const char *section, const char *key,
                      const char *value)
{
    if (!config || !section || !key || !value) {
        return -1;
    }
    
    return set_item(config, section, key, value);
}

/**
 * @brief 设置整数配置值
 * 
 * 设置指定节和键的整数值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值
 * @return 0成功，-1失败
 */
int config_set_int(ConfigManager *config, const char *section, const char *key,
                   int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);  // 整数转字符串
    return config_set_string(config, section, key, buf);
}

/**
 * @brief 设置布尔配置值
 * 
 * 设置指定节和键的布尔值。
 * 
 * @param config 配置管理器指针
 * @param section 节名
 * @param key 键名
 * @param value 值（非0为true，0为false）
 * @return 0成功，-1失败
 */
int config_set_bool(ConfigManager *config, const char *section, const char *key,
                    int value)
{
    return config_set_string(config, section, key, value ? "true" : "false");
}

