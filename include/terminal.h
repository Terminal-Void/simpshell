//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_TERMINAL_H
#define SIMPSHELL_TERMINAL_H

#include <sys/types.h>
#include <termios.h>

// 检测交互模式；仅在交互模式下建立 shell 进程组并接管控制终端。
void init_shell(void);

int get_shell_terminal(void);
pid_t get_shell_pgid(void);
int is_shell_interactive(void);
// 返回 terminal.c 内部保存模式的只读借用指针。
const struct termios *get_shell_tmodes(void);

// 前台 Job 运行前移交终端；结束/停止后由 shell 收回并恢复终端模式。
int give_terminal_to(pid_t pgid);
int reclaim_terminal(void);

#endif //SIMPSHELL_TERMINAL_H
