/**
 * @file main.c
 * @brief 网关主入口，按子命令分发到 app/daemon/ota 模块
 */

#include "app_runner.h"
#include "daemon_runner.h"
#include "app_ota.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void print_usage(const char *program_name)
{
    /* 帮助文本保留英文，便于与脚本和历史文档保持一致。 */
    printf("Usage: %s <command>\n", program_name);
    printf("Commands:\n");
    printf("  app     - Run the main gateway application\n");
    printf("  daemon  - Run as daemon process (supervisor)\n");
    printf("  ota     - Run OTA update checker\n");
    printf("  help    - Show this help message\n");
}

int main(int argc, char const *argv[])
{
    if (argc <= 1) // 没有命令参数
    {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (strcmp(argv[1], "app") == 0) // 运行应用主流程
    {
        return app_runner_run();
    }
    else if (strcmp(argv[1], "daemon") == 0) // 运行守护进程
    {
        return daemon_runner_run();
    }
    else if (strcmp(argv[1], "ota") == 0) // 运行 OTA 升级流程
    {
        OtaManager ota;
        ota_init(&ota, NULL);
        ota_upgrade(&ota);
    }
    else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    { // 显示帮助信息
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    else
    {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}
