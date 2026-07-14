//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_SHELL_H
#define SIMPSHELL_SHELL_H

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_JOBS 100

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

// exit builtin 只设置请求，由 main() 在释放当前命令资源后统一退出。
void request_shell_exit(int status);
int is_shell_exit_requested(void);
int get_shell_exit_status(void);
void clear_shell_exit_request(void);

// 最近一条命令的退出状态，供 $? 参数展开和 shell 默认退出码使用。
void set_shell_last_status(int status);
int get_shell_last_status(void);

#endif //SIMPSHELL_SHELL_H
