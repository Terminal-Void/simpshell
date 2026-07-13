//
// Created by Terminal Void on 2026/7/7.
//

#include "executor.h"
#include "jobs.h"
#include "shell.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "terminal.h"

static void format_job_command(char *destination, const size_t destination_size,
                               const char *raw_command, const int is_background) {
    assert(destination != NULL);
    assert(destination_size > 0);
    assert(raw_command != NULL);

    size_t length = strlen(raw_command);

    while (length > 0 && isspace((unsigned char)raw_command[length - 1])) {
        length--;
    }

    // tokenize/parse 已确认这个末尾 & 是后台操作符，不是被引用的普通参数。
    if (is_background && length > 0 && raw_command[length - 1] == '&') {
        length--;
        while (length > 0 && isspace((unsigned char)raw_command[length - 1])) {
            length--;
        }
    }

    if (length >= destination_size) {
        length = destination_size - 1;
    }

    memcpy(destination, raw_command, length);
    destination[length] = '\0';
}

int execute_external(char **argv, const int is_background, const char *raw_cmd_string) {
    assert(argv != NULL);
    assert(argv[0] != NULL);
    assert(raw_cmd_string != NULL);

    char job_command[MAX_CMD_LEN];
    format_job_command(job_command, sizeof(job_command),
                       raw_cmd_string, is_background);

    const pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) { //Child

        if (setpgid(0, 0) < 0) {
            perror("setpgid");
            _exit(1);
        }

        //foreground child 也调用一次 tcsetpgrp()，parent 也调用一次，避免竞态
        if (!is_background && is_shell_interactive()) {
            if (give_terminal_to(getpid()) < 0) {
                _exit(1);
            }
        }

        //重置继承的信号行为
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        execvp(argv[0], argv);

        perror(argv[0]);
        _exit(127);
    }

    //Parent

    const pid_t pgid = pid;

    //Parent also calls setpgid to avoid a race with the child.
    if (setpgid(pid, pgid) < 0) {
        if (errno != EACCES && errno != ESRCH) {
            perror("setpgid");
            return 1;
        }
    }

    if (is_background) {
        const int job_id = create_job(pgid, job_command);

        if (job_id != -1) {
            printf("[%d] %d\n", job_id, pgid);
        } else {
            fprintf(stderr, "Shell: maximum number of jobs exceeded\n");
        }

        return 0;
    }

    /*
     * Foreground job:
     * give terminal to job's process group.
     */
    if (is_shell_interactive()) {
        if (give_terminal_to(pgid) < 0) {
            return 1;
        }
    }

    int status;
    int stopped = 0;

    while (1) {
        const pid_t waited = waitpid(-pgid, &status, WUNTRACED);

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("waitpid");
            break;
        }

        if (WIFSTOPPED(status)) {
            stopped = 1;
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            break;
        }
    }

    /*
     * Shell must reclaim terminal after foreground job exits or stops.
     */
    if (is_shell_interactive()) {
        reclaim_terminal();
    }

    if (stopped) {
        const int job_id = create_job(pgid, job_command);

        if (job_id != -1) {
            stop_job(job_id);
            printf("\n[%d]  + suspended  %s\n", job_id, job_command);
        } else {
            fprintf(stderr, "Shell: maximum number of jobs exceeded\n");
        }
    }

    return 0;
}
