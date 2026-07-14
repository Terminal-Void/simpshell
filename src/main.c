#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>

#include "aliases.h"
#include "history.h"
#include "jobs.h"
#include "shell.h"
#include "parser.h"
#include "builtins.h"
#include "executor.h"
#include "prompt.h"
#include "terminal.h"
#include "utils.h"

static int finish_shell_exit(int *warning_shown, const int allow_warning) {
    // 先回收已经自然结束的后台进程，避免对过期 Job 发出退出警告。
    check_background_jobs();

    if (get_job_count() == 0) {
        return 1;
    }

    // 交互式 shell 第一次只警告；返回 0 表示 main() 应继续显示 Prompt。
    if (allow_warning && !*warning_shown) {
        fprintf(stderr, "shell: there are active jobs\n");
        print_active_jobs();
        *warning_shown = 1;
        clear_shell_exit_request();
        return 0;
    }

    // 第二次退出，或非交互式 shell 无法再次确认时，直接清理全部 Job。
    shutdown_active_jobs();
    return 1;
}

int main(const int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    init_shell();

    char *input = NULL;
    size_t input_capacity = 0;
    // 该变量只描述交互流程，不属于全局 shell 运行状态。
    int exit_warning_shown = 0;
    int exit_status = 0;

    while (1) {
        check_background_jobs();
        print_prompt();

        errno = 0;
        const ssize_t input_length = getline(&input, &input_capacity, stdin);
        if (input_length < 0) {
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }

            const int input_failed = ferror(stdin);
            if (input_failed) {
                perror("getline");
            } else if (is_shell_interactive()) {
                // 终端上的 Ctrl+D 不会回显换行，手动换行后再打印提示。
                printf("\n");
            }

            request_shell_exit(input_failed ? 1 : 0);
            const int allow_warning = is_shell_interactive() && !input_failed;
            if (!finish_shell_exit(&exit_warning_shown, allow_warning)) {
                // stdio 会记住 EOF；清除后第二次 Ctrl+D 才算新的退出请求。
                clearerr(stdin);
                continue;
            }

            exit_status = get_shell_exit_status();
            break;
        }

        if (input_length > 0 && input[input_length - 1] == '\n') {
            input[input_length - 1] = '\0';
        }

        if (input[0] == '\0') {
            continue;
        }

        // history 保存用户原始输入，而不是 alias 展开后的内部命令。
        add_history_entry(input);

        DynamicTokenList *cmd_tokens = tokenize(input);
        if (cmd_tokens == NULL) {
            exit_warning_shown = 0;
            continue;
        }

        Pipeline *pipeline = create_pipeline_from_tokens(cmd_tokens);
        if (pipeline == NULL) {
            free_DynamicTokenList(cmd_tokens);
            exit_warning_shown = 0;
            continue;
        }

        execute_pipeline(pipeline, input);

        // Pipeline 已复制 argv/重定向字符串，与 TokenList 各自拥有内存，
        // 所以执行结束后可以分别释放，不会产生悬空指针或 double free。
        free_Pipeline(pipeline);
        free_DynamicTokenList(cmd_tokens);

        // 只有资源释放完成后，才允许退出当前 shell 进程。
        if (is_shell_exit_requested()) {
            if (finish_shell_exit(&exit_warning_shown, is_shell_interactive())) {
                exit_status = get_shell_exit_status();
                break;
            }
            continue;
        }

        // 第一次退出警告后执行了普通命令，下一次退出需要重新确认。
        exit_warning_shown = 0;
    }

    free(input);
    free_aliases();
    free_history();
    printf("Bye!\n");
    return exit_status;
}
