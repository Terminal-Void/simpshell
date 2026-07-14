//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_SHELL_H
#define SIMPSHELL_SHELL_H

#define MAX_CMD_LEN 1024
#define MAX_ARGS 64
#define MAX_JOBS 100

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/*
 * exit 请求状态与 last_status 相互独立：前者描述“是否准备离开 shell”，
 * 后者描述“上一条命令结果”。exit builtin 只设置请求，main() 在释放
 * Token/Pipeline 并处理 active jobs 后才真正结束进程。
 */
void request_shell_exit(int status);
int is_shell_exit_requested(void);
int get_shell_exit_status(void);
void clear_shell_exit_request(void);

// 最近一条命令的退出状态，供 $? 参数展开和 shell 默认退出码使用。
void set_shell_last_status(int status);
int get_shell_last_status(void);

#endif //SIMPSHELL_SHELL_H
