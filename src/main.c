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

    char *input = NULL;
    size_t input_capacity = 0;
    char *cmd_argv[MAX_ARGS];

    while (1) {

        check_background_jobs();

        //0. 准备有关变量
        int is_background = 0;

        // 1. 打印Prompt
        print_prompt();

        // 2. 读取用户输入
        errno = 0;
        const ssize_t input_length = getline(&input, &input_capacity, stdin);
        if (input_length < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }

            if (ferror(stdin)) {
                perror("getline");
            } else {
                // 处理按下 Ctrl+D (EOF) 的情况：优雅退出
                printf("\n");
            }
            break;
        }

        // 3. 去除字符串末尾的换行符
        if (input_length > 0 && input[input_length - 1] == '\n') {
            input[input_length - 1] = '\0';
        }

        // 如果用户什么都没输直接按了回车，跳过本次循环
        if (strlen(input) == 0) { continue; }

        DynamicTokenList *cmd_tokens = tokenize(input);
        if (cmd_tokens == NULL) {
            continue;
        }

        const int cmd_argc = parse_tokens_as_command(cmd_tokens, cmd_argv,
                                                     ARRAY_SIZE(cmd_argv),
                                                     &is_background);
        if (cmd_argc <= 0) {
            free_DynamicTokenList(cmd_tokens);
            cmd_tokens=NULL;
            continue;
        }

        const BuiltinFunc builtin_func = get_builtin_func(cmd_argv[0]);

        if (builtin_func != NULL) {
            builtin_func(cmd_argv);
        }
        else {
            execute_external(cmd_argv,is_background,input);
        }

        // 释放内存
        free_DynamicTokenList(cmd_tokens);
        cmd_tokens=NULL;
    }
    free(input);
    return 0;
}
