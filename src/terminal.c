//
// Created by Terminal Void on 2026/7/7.
//
#include "terminal.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static int shell_terminal = 0;
static int interactive_shell = 0;
static pid_t shell_pgid = -1;
static struct termios shell_tmodes;

void init_shell(void) {
    shell_terminal = STDIN_FILENO;
    interactive_shell = isatty(shell_terminal);

    // 非交互输入没有控制终端，不能调用 tcsetpgrp 等 Job Control 接口。
    if (!interactive_shell) {
        return;
    }

    /*
     * shell 自己也可能由另一个 shell 启动在后台。只有当前进程组已经是
     * 终端前台组时，才有资格接管终端；否则向自身进程组发送 SIGTTIN，
     * 等外层 shell 执行 fg 后再从这里继续检查。
     */
    while (1) {
        const pid_t terminal_pgid = tcgetpgrp(shell_terminal);
        if (terminal_pgid < 0) {
            perror("tcgetpgrp");
            exit(EXIT_FAILURE);
        }

        shell_pgid = getpgrp();

        if (terminal_pgid == shell_pgid) {
            break;
        }

        if (kill(-shell_pgid, SIGTTIN) < 0) {
            perror("kill");
            exit(EXIT_FAILURE);
        }
    }

    /*
     * Prompt 所在的 shell 不应被 Ctrl+C/Ctrl+Z 杀死或停止；真正执行
     * 命令的 child 会在 fork 后恢复这些信号的默认处理方式。
     */
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);


    // 让 shell 成为独立进程组 leader，之后才能和每条 Job 进程组分离。
    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        if (errno == EPERM && getpgrp() == shell_pgid) {
            // IDE/启动器可能已经完成相同设置，此时可以继续。
        } else {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
    }

    // Prompt 显示前，终端前台进程组必须是 shell 自己。
    if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
        perror("tcsetpgrp");
        exit(EXIT_FAILURE);
    }

    // 前台程序可能修改回显/规范模式；保存基准状态供 reclaim_terminal 恢复。
    if (tcgetattr(shell_terminal, &shell_tmodes) < 0) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }

}

int get_shell_terminal(void) {
    return shell_terminal;
}

pid_t get_shell_pgid(void) {
    return shell_pgid;
}

int is_shell_interactive(void) {
    return interactive_shell;
}

int give_terminal_to(pid_t pgid) {
    if (!is_shell_interactive()) {
        return 0;
    }

    // 内核会把终端生成的 Ctrl+C/Ctrl+Z 信号发送给这个前台进程组。
    if (tcsetpgrp(get_shell_terminal(), pgid) < 0) {
        perror("tcsetpgrp");
        return -1;
    }

    return 0;
}

int reclaim_terminal(void) {
    if (!is_shell_interactive()) {
        return 0;
    }

    // Job 退出或停止后，先收回终端，再恢复 shell 保存的 termios。
    if (tcsetpgrp(get_shell_terminal(), get_shell_pgid()) < 0) {
        perror("tcsetpgrp");
        return -1;
    }

    if (tcsetattr(get_shell_terminal(), TCSADRAIN, get_shell_tmodes()) < 0) {
        perror("tcsetattr");
        return -1;
    }

    return 0;
}

const struct termios *get_shell_tmodes(void) {
    return &shell_tmodes;
}
