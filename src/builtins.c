//
// Created by Terminal Void on 2026/7/7.
//

#include "builtins.h"
#include "aliases.h"
#include "executor.h"
#include "history.h"
#include "jobs.h"
#include "parser.h"
#include "utils.h"

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


#include "terminal.h"



typedef struct {
    char *name;
    int (*func)(char **argv);
    BuiltinType type;
} BuiltinCommand;

// 修改 shell 核心状态或提供基础用户功能的 builtins。
static int builtin_cd(char **argv);
static int builtin_exit(char **argv);
static int builtin_pwd(char **argv);
static int builtin_clear(char **argv);
static int builtin_export(char **argv);
static int builtin_unset(char **argv);
static int builtin_exec(char **argv);
static int builtin_source(char **argv);
static int builtin_echo(char **argv);
static int builtin_alias(char **argv);
static int builtin_unalias(char **argv);
static int builtin_history(char **argv);
static int builtin_sleep(char **argv);

// 课程要求的简化文件系统 builtins。
static int builtin_ls(char **argv);
static int builtin_mkdir(char **argv);
static int builtin_rmdir(char **argv);
static int builtin_touch(char **argv);
static int builtin_rm(char **argv);

// 必须访问父 shell Job 表和控制终端的 Job Control builtins。
int builtin_jobs(char **argv);
int builtin_bg(char **argv);
int builtin_fg(char **argv);
int builtin_wait(char **argv);

// 多个 builtin 共用的参数解析辅助函数。
static Job *parse_job_arg(char **argv, const char *builtin_name, int *job_id_out);
static int get_operand_start(char **argv, const char *builtin_name);
static int wait_for_job(Job *job, int job_id);

/*
 * 注册表同时决定函数入口和执行位置。BUILTIN_PARENT 表示单独前台执行
 * 时必须留在父 shell，才能保留目录、环境、alias 或 Job 表变化。
 */
BuiltinCommand builtins[] = {
    {"cd", builtin_cd, BUILTIN_PARENT},
    {"exit", builtin_exit, BUILTIN_PARENT},
    {"pwd", builtin_pwd,BUILTIN_REGULAR},
    {"clear", builtin_clear, BUILTIN_REGULAR},
    {"export", builtin_export, BUILTIN_PARENT},
    {"unset", builtin_unset, BUILTIN_PARENT},
    {"exec", builtin_exec, BUILTIN_PARENT},
    {"source", builtin_source, BUILTIN_PARENT},
    {"echo", builtin_echo, BUILTIN_REGULAR},
    {"alias", builtin_alias, BUILTIN_PARENT},
    {"unalias", builtin_unalias, BUILTIN_PARENT},
    {"history", builtin_history, BUILTIN_REGULAR},
    {"sleep", builtin_sleep, BUILTIN_REGULAR},
    {"ls", builtin_ls, BUILTIN_REGULAR},
    {"mkdir", builtin_mkdir, BUILTIN_REGULAR},
    {"rmdir", builtin_rmdir, BUILTIN_REGULAR},
    {"touch", builtin_touch, BUILTIN_REGULAR},
    {"rm", builtin_rm, BUILTIN_REGULAR},
    {"jobs", builtin_jobs, BUILTIN_PARENT},
    {"bg", builtin_bg,BUILTIN_PARENT},
    {"fg", builtin_fg,BUILTIN_PARENT},
    {"wait", builtin_wait,BUILTIN_PARENT},
    {NULL, NULL,BUILTIN_NONE}
};

BuiltinFunc get_builtin_func(const char *cmd,BuiltinType *type) {
    assert(cmd != NULL);
    for (int i = 0; builtins[i].name!=NULL; i++) {
        if (strcmp(cmd, builtins[i].name) == 0) {
            *type = builtins[i].type;
            return builtins[i].func; // 找到了，把函数的内存地址交出去
        }
    }
    *type = BUILTIN_NONE;
    return NULL; // 没找到
}

int builtin_cd(char **argv) {
    if (argv[1] != NULL && argv[2] != NULL) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    // chdir 前保存旧目录，成功后用它维护 OLDPWD。
    char old_cwd[PATH_MAX];
    if (getcwd(old_cwd, sizeof(old_cwd)) == NULL) {
        perror("cd: getcwd");
        return 1;
    }

    const char *target = argv[1];
    int print_directory = 0;

    if (target == NULL || strcmp(target, "~") == 0) {
        target = getenv("HOME");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: HOME environment variable not set\n");
            return 1;
        }
    } else if (strcmp(target, "-") == 0) {
        target = getenv("OLDPWD");
        if (target == NULL || target[0] == '\0') {
            fprintf(stderr, "cd: OLDPWD environment variable not set\n");
            return 1;
        }
        print_directory = 1;
    }

    if (chdir(target) < 0) {
        perror("cd");
        return 1;
    }

    char new_cwd[PATH_MAX];
    if (getcwd(new_cwd, sizeof(new_cwd)) == NULL) {
        perror("cd: getcwd");
        return 1;
    }

    int result = 0;
    if (setenv("OLDPWD", old_cwd, 1) < 0) {
        perror("cd: setenv OLDPWD");
        result = 1;
    }
    if (setenv("PWD", new_cwd, 1) < 0) {
        perror("cd: setenv PWD");
        result = 1;
    }
    if (print_directory) {
        printf("%s\n", new_cwd);
    }
    return result;
}

int builtin_exit(char **argv) {
    if (argv[1] != NULL && argv[2] != NULL) {
        fprintf(stderr, "exit: too many arguments\n");
        return 1;
    }

    long status = get_shell_last_status();
    if (argv[1] != NULL) {
        char *endptr;
        errno = 0;
        status = strtol(argv[1], &endptr, 10);
        if (errno == ERANGE || endptr == argv[1] || *endptr != '\0' ||
            status < 0 || status > 255) {
            fprintf(stderr, "exit: %s: numeric status must be between 0 and 255\n",
                    argv[1]);
            return 2;
        }
    }

    // 不能在这里直接 exit()，否则 main() 无法释放 Pipeline、Token 和输入缓冲。
    request_shell_exit((int)status);
    return (int)status;
}

int builtin_pwd(char **argv) {
    (void)argv;
    char cwd[PATH_MAX];
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

static int is_valid_identifier(const char *name, const size_t length) {
    if (length == 0 || !(isalpha((unsigned char)name[0]) || name[0] == '_')) {
        return 0;
    }

    for (size_t i = 1; i < length; i++) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_')) {
            return 0;
        }
    }

    return 1;
}

static int builtin_export(char **argv) {
    if (argv[1] == NULL) {
        extern char **environ;
        // environ 是当前进程环境，fork/exec 后会被外部命令继承。
        for (char **entry = environ; *entry != NULL; entry++) {
            printf("%s\n", *entry);
        }
        return 0;
    }

    int result = 0;

    for (size_t i = 1; argv[i] != NULL; i++) {
        const char *equals = strchr(argv[i], '=');
        const size_t name_length = equals == NULL
                                   ? strlen(argv[i])
                                   : (size_t)(equals - argv[i]);

        if (!is_valid_identifier(argv[i], name_length)) {
            fprintf(stderr, "export: %s: not a valid identifier\n", argv[i]);
            result = 1;
            continue;
        }

        char *name = strndup(argv[i], name_length);
        if (name == NULL) {
            perror("strndup");
            return 1;
        }

        const char *value = equals == NULL ? getenv(name) : equals + 1;
        if (value == NULL) {
            value = "";
        }

        if (setenv(name, value, 1) < 0) {
            perror("export");
            result = 1;
        }
        free(name);
    }

    return result;
}

static int builtin_unset(char **argv) {
    if (argv[1] == NULL) {
        fprintf(stderr, "unset: expected at least one variable name\n");
        return 1;
    }

    int result = 0;
    for (size_t i = 1; argv[i] != NULL; i++) {
        const size_t length = strlen(argv[i]);
        if (!is_valid_identifier(argv[i], length)) {
            fprintf(stderr, "unset: %s: not a valid identifier\n", argv[i]);
            result = 1;
            continue;
        }
        if (unsetenv(argv[i]) < 0) {
            perror("unset");
            result = 1;
        }
    }
    return result;
}

static int builtin_echo(char **argv) {
    size_t start = 1;
    int print_newline = 1;

    if (argv[1] != NULL && strcmp(argv[1], "-n") == 0) {
        print_newline = 0;
        start = 2;
    }

    for (size_t i = start; argv[i] != NULL; i++) {
        if (i > start) {
            putchar(' ');
        }
        fputs(argv[i], stdout);
    }
    if (print_newline) {
        putchar('\n');
    }
    return 0;
}

static int builtin_alias(char **argv) {
    if (argv[1] == NULL) {
        print_aliases();
        return 0;
    }

    int result = 0;
    for (size_t i = 1; argv[i] != NULL; i++) {
        // argv[i] 已是去引号/展开后的完整参数，按第一个 '=' 分割名称和值。
        const char *equals = strchr(argv[i], '=');
        if (equals == NULL) {
            const char *value = get_alias(argv[i]);
            if (value == NULL) {
                fprintf(stderr, "alias: %s: not found\n", argv[i]);
                result = 1;
            } else {
                printf("alias %s='%s'\n", argv[i], value);
            }
            continue;
        }

        const size_t name_length = (size_t)(equals - argv[i]);
        if (!is_valid_identifier(argv[i], name_length)) {
            fprintf(stderr, "alias: %s: invalid alias name\n", argv[i]);
            result = 1;
            continue;
        }
        if (equals[1] == '\0') {
            fprintf(stderr, "alias: %.*s: command cannot be empty\n",
                    (int)name_length, argv[i]);
            result = 1;
            continue;
        }

        char *name = strndup(argv[i], name_length);
        if (name == NULL) {
            perror("strndup");
            return 1;
        }
        if (set_alias(name, equals + 1) < 0) {
            result = 1;
        }
        free(name);
    }
    return result;
}

static int builtin_unalias(char **argv) {
    if (argv[1] == NULL) {
        fprintf(stderr, "unalias: expected at least one alias name\n");
        return 1;
    }

    int result = 0;
    for (size_t i = 1; argv[i] != NULL; i++) {
        if (remove_alias(argv[i]) < 0) {
            fprintf(stderr, "unalias: %s: not found\n", argv[i]);
            result = 1;
        }
    }
    return result;
}

static int builtin_history(char **argv) {
    if (argv[1] != NULL) {
        fprintf(stderr, "history: this simplified version takes no arguments\n");
        return 1;
    }
    print_history();
    return 0;
}

static int builtin_sleep(char **argv) {
    if (argv[1] == NULL || argv[2] != NULL) {
        fprintf(stderr, "sleep: usage: sleep SECONDS\n");
        return 1;
    }

    char *endptr;
    errno = 0;
    const long seconds = strtol(argv[1], &endptr, 10);
    if (errno == ERANGE || endptr == argv[1] || *endptr != '\0' || seconds < 0) {
        fprintf(stderr, "sleep: %s: expected a non-negative integer\n", argv[1]);
        return 1;
    }

    struct timespec remaining = {.tv_sec = seconds, .tv_nsec = 0};
    while (nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR) {
            perror("sleep: nanosleep");
            return 1;
        }
    }
    return 0;
}

static int builtin_exec(char **argv) {
    if (argv[1] == NULL) {
        fprintf(stderr, "exec: expected a command\n");
        return 1;
    }

    execvp(argv[1], &argv[1]);
    const int saved_errno = errno;
    perror(argv[1]);
    return saved_errno == ENOENT || saved_errno == ENOTDIR ? 127 : 126;
}

static int builtin_source(char **argv) {
    if (argv[1] == NULL || argv[2] != NULL) {
        fprintf(stderr, "source: usage: source FILE\n");
        return 1;
    }

    FILE *file = fopen(argv[1], "r");
    if (file == NULL) {
        perror(argv[1]);
        return 1;
    }

    char *line = NULL;
    size_t capacity = 0;
    int result = 0;

    /*
     * source 不创建第二套解释器：逐行复用 tokenize -> Pipeline -> executor。
     * 独立前台调用时它运行在 parent，cd/export/alias 会作用于当前 shell；
     * 若 source 被放进 Pipeline/后台，则这些变化只存在于对应 child。
     * 每行执行后都立即更新该进程中的 $?，供文件下一行展开。
     */
    while (1) {
        errno = 0;
        const ssize_t length = getline(&line, &capacity, file);
        if (length < 0) {
            if (errno == EINTR) {
                clearerr(file);
                continue;
            }
            if (ferror(file)) {
                perror("source: getline");
                result = 1;
            }
            break;
        }

        if (length > 0 && line[length - 1] == '\n') {
            line[length - 1] = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }

        DynamicTokenList *tokens = tokenize(line);
        if (tokens == NULL) {
            result = 2;
            set_shell_last_status(result);
            continue;
        }

        if (tokens->cursor == 0) {
            free_DynamicTokenList(tokens);
            continue;
        }

        Pipeline *pipeline = create_pipeline_from_tokens(tokens);
        if (pipeline == NULL) {
            free_DynamicTokenList(tokens);
            result = 2;
            set_shell_last_status(result);
            continue;
        }

        result = execute_pipeline(pipeline, line);
        set_shell_last_status(result);
        free_Pipeline(pipeline);
        free_DynamicTokenList(tokens);

        // source 与当前 shell 共用状态；遇到 exit 后不能继续执行后续行。
        if (is_shell_exit_requested()) {
            break;
        }
    }

    free(line);
    fclose(file);
    return result;
}

static int get_operand_start(char **argv, const char *builtin_name) {
    if (argv[1] != NULL && strcmp(argv[1], "--") == 0) {
        return 2;
    }

    if (argv[1] != NULL && argv[1][0] == '-' && argv[1][1] != '\0') {
        fprintf(stderr, "%s: unsupported option: %s\n", builtin_name, argv[1]);
        return -1;
    }

    return 1;
}

static int list_path(const char *path, const int print_heading) {
    struct stat info;
    if (lstat(path, &info) < 0) {
        perror(path);
        return 1;
    }

    if (!S_ISDIR(info.st_mode)) {
        printf("%s\n", path);
        return 0;
    }

    DIR *directory = opendir(path);
    if (directory == NULL) {
        perror(path);
        return 1;
    }

    if (print_heading) {
        printf("%s:\n", path);
    }

    errno = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        printf("%s\n", entry->d_name);
    }

    int result = 0;
    if (errno != 0) {
        perror("readdir");
        result = 1;
    }
    closedir(directory);
    return result;
}

static int builtin_ls(char **argv) {
    const int start = get_operand_start(argv, "ls");
    if (start < 0) {
        return 1;
    }

    if (argv[start] == NULL) {
        return list_path(".", 0);
    }

    int result = 0;
    const int multiple = argv[start + 1] != NULL;
    for (int i = start; argv[i] != NULL; i++) {
        if (list_path(argv[i], multiple) != 0) {
            result = 1;
        }
        if (multiple && argv[i + 1] != NULL) {
            putchar('\n');
        }
    }
    return result;
}

static int builtin_mkdir(char **argv) {
    const int start = get_operand_start(argv, "mkdir");
    if (start < 0) {
        return 1;
    }
    if (argv[start] == NULL) {
        fprintf(stderr, "mkdir: expected at least one directory\n");
        return 1;
    }

    int result = 0;
    for (int i = start; argv[i] != NULL; i++) {
        if (mkdir(argv[i], 0777) < 0) {
            perror(argv[i]);
            result = 1;
        }
    }
    return result;
}

static int builtin_rmdir(char **argv) {
    const int start = get_operand_start(argv, "rmdir");
    if (start < 0) {
        return 1;
    }
    if (argv[start] == NULL) {
        fprintf(stderr, "rmdir: expected at least one empty directory\n");
        return 1;
    }

    int result = 0;
    for (int i = start; argv[i] != NULL; i++) {
        if (rmdir(argv[i]) < 0) {
            perror(argv[i]);
            result = 1;
        }
    }
    return result;
}

static int builtin_touch(char **argv) {
    const int start = get_operand_start(argv, "touch");
    if (start < 0) {
        return 1;
    }
    if (argv[start] == NULL) {
        fprintf(stderr, "touch: expected at least one file\n");
        return 1;
    }

    int result = 0;
    for (int i = start; argv[i] != NULL; i++) {
        // 文件存在时直接更新时间；不存在时再用 O_EXCL 原子创建。
        if (utimensat(AT_FDCWD, argv[i], NULL, 0) == 0) {
            continue;
        }

        if (errno != ENOENT) {
            perror(argv[i]);
            result = 1;
            continue;
        }

        const int fd = open(argv[i], O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            // 检查与创建之间可能被其他进程创建，EEXIST 时重试更新时间。
            if (errno == EEXIST && utimensat(AT_FDCWD, argv[i], NULL, 0) == 0) {
                continue;
            }
            perror(argv[i]);
            result = 1;
            continue;
        }
        close(fd);
    }
    return result;
}

static int is_dangerous_rm_path(const char *path) {
    return strcmp(path, "/") == 0 ||
           strcmp(path, ".") == 0 ||
           strcmp(path, "..") == 0;
}

static int builtin_rm(char **argv) {
    const int start = get_operand_start(argv, "rm");
    if (start < 0) {
        return 1;
    }
    if (argv[start] == NULL) {
        fprintf(stderr, "rm: expected at least one file\n");
        return 1;
    }

    int result = 0;
    for (int i = start; argv[i] != NULL; i++) {
        const char *path = argv[i];

        if (is_dangerous_rm_path(path)) {
            fprintf(stderr, "rm: refusing to remove dangerous path: %s\n", path);
            result = 1;
            continue;
        }

        struct stat info;
        if (lstat(path, &info) < 0) {
            perror(path);
            result = 1;
            continue;
        }

        // lstat 不跟随符号链接：链接到目录时只删除链接本身，不碰目标目录。
        if (S_ISDIR(info.st_mode)) {
            fprintf(stderr, "rm: %s: is a directory; use rmdir for empty directories\n",
                    path);
            result = 1;
            continue;
        }

        if (!S_ISREG(info.st_mode) && !S_ISLNK(info.st_mode)) {
            fprintf(stderr,
                    "rm: %s: refusing to remove non-regular file type\n",
                    path);
            result = 1;
            continue;
        }

        // 只使用 unlink，不提供 -r/-f，从实现层面杜绝递归删除。
        if (unlink(path) < 0) {
            perror(path);
            result = 1;
        }
    }

    return result;
}

int builtin_jobs(char **argv) {
    (void)argv;
    check_background_jobs();
    print_active_jobs();
    return 0;
}

int builtin_bg(char **argv) {
    check_background_jobs();

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
    check_background_jobs();

    int job_id;
    const Job *job = parse_job_arg(argv,"fg",&job_id);

    if (job == NULL) {
        return 1;
    }

    const pid_t target_pgid = job->pgid;
    printf("%s\n", job->cmd);

    // 先交出控制终端，之后 Ctrl+C/Ctrl+Z 才会发送给目标进程组。
    if (give_terminal_to(job->pgid)<0) {
        return 1;
    }

    // 无论原状态为 RUNNING/STOPPED 都发送 SIGCONT，统一恢复执行。
    if (kill(-target_pgid, SIGCONT) < 0) {
        perror("fg: kill");
        // 恢复失败时立即把终端还给 shell。
        reclaim_terminal();
        return 1;
    }

    // Job 表同步标记为 RUNNING。
    run_job(job_id);

    // 等待整个 job，而不是任意一个进程退出后就提前返回。
    int stopped = 0;

    while (job->live_count > 0) {
        int status;
        const pid_t waited = waitpid(-target_pgid, &status, WUNTRACED);

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD) {
                break;
            }

            perror("fg: waitpid");
            reclaim_terminal();
            return 1;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            record_job_process_exit(job_id, waited);
            continue;
        }

        if (WIFSTOPPED(status)) {
            stopped = 1;
            break;
        }
    }

    if (reclaim_terminal()<0) {
        return 1;
    }

    // 根据“再次停止”或“全部退出”决定保留还是删除 Job 槽位。
    if (stopped) {
        // 如果是在前台运行时，用户又按了 Ctrl+Z 把它冻结了
        stop_job(job_id);
        // 打印挂起提示
        printf("\n[%d]  + suspended  %s\n", job->job_id, job->cmd);
    } else if (job->live_count == 0) {
        // 如果是正常跑完，或者被 Ctrl+C 杀掉了
        remove_job(job_id);
    }

    return 0;
}

static int wait_for_job(Job *job, const int job_id) {
    assert(job != NULL);

    if (job->status == STOPPED) {
        fprintf(stderr, "wait: %%%d: job is stopped; use bg or fg first\n", job_id);
        return 1;
    }

    while (job->live_count > 0) {
        int status;
        const pid_t waited = waitpid(-job->pgid, &status, WUNTRACED);

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }
            perror("wait");
            return 1;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            record_job_process_exit(job_id, waited);
            continue;
        }

        if (WIFSTOPPED(status)) {
            stop_job(job_id);
            fprintf(stderr, "wait: %%%d: job stopped while waiting\n", job_id);
            return 1;
        }
    }

    if (job->live_count == 0) {
        remove_job(job_id);
    }
    return 0;
}

int builtin_wait(char **argv) {
    check_background_jobs();

    if (argv[1] != NULL) {
        if (argv[2] != NULL) {
            fprintf(stderr, "wait: usage: wait [%%JOB_ID]\n");
            return 1;
        }

        int job_id;
        Job *job = parse_job_arg(argv, "wait", &job_id);
        return job == NULL ? 1 : wait_for_job(job, job_id);
    }

    int result = 0;
    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        Job *job = get_job_by_id(job_id);
        if (job != NULL && wait_for_job(job, job_id) != 0) {
            result = 1;
        }
    }
    return result;
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
