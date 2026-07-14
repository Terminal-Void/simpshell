#include "shell.h"

// exit builtin 只记录请求，真正的资源清理和进程退出由 main() 完成。
static int exit_requested = 0;
static int requested_exit_status = 0;

void request_shell_exit(const int status) {
    exit_requested = 1;
    requested_exit_status = status;
}

int is_shell_exit_requested(void) {
    return exit_requested;
}

int get_shell_exit_status(void) {
    return requested_exit_status;
}

void clear_shell_exit_request(void) {
    // 第一次发现 active jobs 时取消本次请求，但 main() 会保留“已经警告”状态。
    exit_requested = 0;
    requested_exit_status = 0;
}
