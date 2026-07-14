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

    if (!interactive_shell) { return; }
    //判断是否为Interactive Shell，即检查fd=STDIN_FILENO是否指向终端
    //只有Interactive Shell才启用job control.


    while (1) {
        //检查当前 shell 是否为终端的前台进程组
        //若当前 shell 还不是终端（fd=STDIN_FILENO指向终端）的前台进程组，就主动把自己停住；
        //等外层 shell 用 fg 把它放回前台后，从 kill() 后面继续执行，然后重新检查
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

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_DFL);


    shell_pgid = getpid();
    //把 shell 自己放进一个独立的 process group，并让 shell 成为这个 process group 的 leader。
    if (setpgid(shell_pgid, shell_pgid) < 0) {
        if (errno == EPERM && getpgrp() == shell_pgid) {
            //有些启动器/IDE环境运行时可能已经将进程放进一个独立的 process group，并让 shell 成为这个 process group 的 leader
            //如果是这种情况可以继续
        } else {
            perror("setpgid");
            exit(EXIT_FAILURE);
        }
    }

    //把当前终端的前台进程组设置为 shell 自己的 PGID。
    // 把终端的 foreground process group 设置为 shell 的 process group
    if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
        perror("tcsetpgrp");
        exit(EXIT_FAILURE);
    }

    //保存 shell 当前的终端模式
    //因为很多前台程序会修改终端模式，shell 需要能自行恢复自己的 terminal mode
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
