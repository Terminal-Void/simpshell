//
// Created by Terminal Void on 2026/7/7.
//

#include "builtins.h"
#include "jobs.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>


#include "terminal.h"

typedef struct {
    char *name;
    int (*func)(char **argv);
} BuiltinCommand;

//Built-ins
static int builtin_cd(char **argv);
static int builtin_exit(char **argv);
static int builtin_pwd(char **argv);
static int builtin_clear(char **argv);
static int builtin_export(char **argv);
static int builtin_exec(char **argv);
static int builtin_source(char **argv);

//Special Built-ins



//Additional Built-ins
static int builtin_ls(char **argv);
static int builtin_mkdir(char **argv);
static int builtin_rmdir(char **argv);
static int builtin_touch(char **argv);
static int builtin_rm(char **argv);

//Job Control Built-ins
int builtin_jobs(char **argv);
int builtin_bg(char **argv);
int builtin_fg(char **argv);
int builtin_wait(char **argv);

//helper
static Job *parse_job_arg(char **argv, const char *builtin_name, int *job_id_out);

BuiltinCommand builtins[] = {
    {"cd", builtin_cd},
    {"exit", builtin_exit},
    {"pwd", builtin_pwd},
    {"clear", builtin_clear},
    {"jobs", builtin_jobs},
    {"bg", builtin_bg},
    {"fg", builtin_fg},
    {NULL, NULL}
};

BuiltinFunc get_builtin_func(const char *cmd) {
    assert(cmd != NULL);

    for (int i = 0; builtins[i].name!=NULL; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            return builtins[i].func; // 找到了，把函数的内存地址交出去
        }
    }
    return NULL; // 没找到
}

int builtin_cd(char **argv) {

    if (argv[1]==NULL) {
        const char *home  = getenv("HOME");
        if (home == NULL) {
            fprintf(stderr,"cd: HOME environment variable not set\n");
            return 1;
        }
        else {
            if (chdir(home) != 0) {
                perror("cd");
                return 1;
            }
        }
    }
    else {
        if (chdir(argv[1]) != 0) {
            perror("cd");
            return 1; // 失败返回 1
        }
    }
    return 0;
}

int builtin_exit(char **argv) {
    (void)argv;
    printf("Bye!\n");
    exit(0); // 直接终止整个 Shell 进程
    //todo: 处理bg进程

}

int builtin_pwd(char **argv) {
    (void)argv;
    char cwd[1024];
    // getcwd 会把当前绝对路径写入 cwd 数组
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
        return 0; // 退出状态码：成功
    } else {
        // 如果因为某些极端原因（比如当前目录突然被其他程序删除了）导致获取失败
        perror("pwd");
        return 1; // 退出状态码：失败
    }
}

int builtin_clear(char **argv) {
    (void)argv;
    printf("\033[2J\033[1;1H");
    return 0;
}

int builtin_jobs(char **argv) {
    (void)argv;
    for (int i = 0; i < MAX_JOBS; i++) {

        Job *job = get_job_by_id(i+1);

        if (job == NULL) {
            continue;
        }

        // 模仿真实 Shell 的输出格式
        printf("[%d] %d %s\t\t%s\n",
               job->job_id,
               job->pgid,
               job->status == RUNNING ? "Running" : "Stopped",
               job->cmd);
    }
    return 0;
}

int builtin_bg(char **argv) {

    int job_id;
    const Job *job = parse_job_arg(argv,"bg",&job_id);

    if (job == NULL) {
        return 1;
    }

    const pid_t target_pgid = job->pgid;

    if (job->status == STOPPED) {
        if (kill(-target_pgid, SIGCONT) < 0) {
            perror("bg: kill");
            return 1;
        }
        run_job(job_id);
    }
    printf("[%d] %s &\n", job_id, job->cmd);

    return 0;
}

int builtin_fg(char **argv) {
    int job_id;
    const Job *job = parse_job_arg(argv,"fg",&job_id);
    //获取job，job_id

    if (job == NULL) {//job不存在(状态为DONE)
        return 1;
    }

    const pid_t target_pgid = job->pgid;
    printf("%s\n", job->cmd);

    //先将终端给即将fg的job
    if (give_terminal_to(job->pgid)<0) {
        return 1;
    }

    //无论当前状态为RUNNING/STOPPED，均发送SIGCONT
    if (kill(-target_pgid, SIGCONT) < 0) {
        perror("fg: kill");
        //如果失败，重新拿回终端
        reclaim_terminal();
        return 1;
    }

    //记录状态为RUNNING
    run_job(job_id);

    //等待job
    int status;

    while (1) {
        const pid_t waited = waitpid(-target_pgid, &status, WUNTRACED);

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("fg: waitpid");
            reclaim_terminal();
            return 1;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status) || WIFSTOPPED(status)) {
            break;
        }
    }

    if (reclaim_terminal()<0) {
        return 1;
    }

    //等待结束，如果是正常退出，从 job_table 里把它标记为 DONE
    if (WIFSTOPPED(status)) {
        // 如果是在前台运行时，用户又按了 Ctrl+Z 把它冻结了
        stop_job(job_id);
        // 打印挂起提示
        printf("\n[%d]  + suspended  %s\n", job->job_id, job->cmd);
    } else {
        // 如果是正常跑完，或者被 Ctrl+C 杀掉了
        remove_job(job_id);
    }

    return 0;
}

static Job *parse_job_arg(char **argv, const char *builtin_name, int *job_id_out) {
    assert(argv != NULL);
    assert(builtin_name != NULL);
    assert(job_id_out != NULL);

    if (argv[1] == NULL) {
        fprintf(stderr, "%s: current job implementation requires job id\n", builtin_name);
        return NULL;
    }

    const char *arg = argv[1];

    if (arg[0] == '%') {
        arg++;
    }

    char *endptr;
    errno = 0;

    const long ljob_id = strtol(arg, &endptr, 10);

    if (errno == ERANGE) {
        fprintf(stderr, "%s: %s: job id out of range\n", builtin_name, argv[1]);
        return NULL;
    }

    if (endptr == arg) {
        fprintf(stderr, "%s: %s: no such job\n", builtin_name, argv[1]);
        return NULL;
    }

    if (*endptr != '\0') {
        fprintf(stderr, "%s: %s: invalid characters in job id\n", builtin_name, argv[1]);
        return NULL;
    }

    if (ljob_id <= 0 || ljob_id > MAX_JOBS || ljob_id > INT_MAX) {
        fprintf(stderr, "%s: %s: no such job\n", builtin_name, argv[1]);
        return NULL;
    }

    const int job_id = (int)ljob_id;
    Job *job = get_job_by_id(job_id);

    if (job == NULL) {
        fprintf(stderr, "%s: %s: no such job\n", builtin_name, argv[1]);
        return NULL;
    }


    *job_id_out = job_id;

    return job;
}