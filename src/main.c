#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#include "jobs.h"
#include "shell.h"
#include "parser.h"
#include "builtins.h"
#include "executor.h"
#include "prompt.h"
#include "terminal.h"
#include "utils.h"


int main(const int argc, const char *argv[]) {

    (void)argc;
    (void)argv;

    init_shell();

    char input[MAX_CMD_LEN];
    char *cmd_argv[MAX_ARGS];

    while (1) {

        check_background_jobs();

        //0. 准备有关变量
        int free_count = 0;
        char *to_free[MAX_ARGS];
        int is_background = 0;

        // 1. 打印Prompt
        print_prompt();

        // 2. 读取用户输入
        if (fgets(input, MAX_CMD_LEN, stdin) == NULL) {
            // 处理按下 Ctrl+D (EOF) 的情况：优雅退出
            printf("\n");
            break;
        }

        // 3. 去除字符串末尾的换行符
        input[strcspn(input, "\n")] = '\0';

        // 如果用户什么都没输直接按了回车，跳过本次循环
        if (strlen(input) == 0) { continue; }

        DynamicTokenList *cmd_tokens=NULL;

        if (tokenize(input, &cmd_tokens)<0) {
            continue;
        }

        const int cmd_argc = parse_tokens_as_command(cmd_tokens, cmd_argv,
                                                     ARRAY_SIZE(cmd_argv),
                                                     &is_background);
        if (cmd_argc <= 0) {
            free_DynamicTokenList(cmd_tokens);
            continue;
        }

        expand_tilde(cmd_argv, to_free, &free_count);

        const BuiltinFunc builtin_func = get_builtin_func(cmd_argv[0]);

        if (builtin_func != NULL) {
            builtin_func(cmd_argv);
        }
        else {
            execute_external(cmd_argv,is_background);
        }

        // 释放内存
        free_expanded_args(to_free,free_count);
        free_DynamicTokenList(cmd_tokens);
        free_count=0;
    }
    return 0;
}
