//
// Created by Terminal Void on 2026/7/7.
//

#include "executor.h"
#include "builtins.h"
#include "jobs.h"
#include "shell.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
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

static void reset_child_signals(void) {
    // shell 自身忽略这些交互信号，但前台程序必须恢复默认行为，
    // 否则 Ctrl+C/Ctrl+Z 也会被 child 忽略。
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
}

static void close_pipeline_pipes(int (*pipes)[2], const size_t pipe_count) {
    if (pipes == NULL) {
        return;
    }

    // 只要还有进程持有 pipe 的写端，读端就收不到 EOF。
    // 因此 parent 和每个 child 都必须关闭自己不再使用的原始 fd。
    for (size_t i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
}

static int connect_pipeline_fds(const size_t command_index,
                                const size_t command_count,
                                int (*pipes)[2]) {
    if (command_count == 0 || command_index >= command_count) {
        fprintf(stderr, "shell: invalid pipeline command index\n");
        return -1;
    }

    // 单条命令不需要创建管道，此时 pipes == NULL 是正常状态。
    if (command_count == 1) {
        return 0;
    }

    // 多条命令必须已经成功创建 command_count - 1 个管道。
    if (pipes == NULL) {
        fprintf(stderr, "shell: pipeline descriptors are missing\n");
        return -1;
    }

    // 第 i 个命令从 pipe[i-1] 读取，并向 pipe[i] 写入：
    // cmd0 -> pipe0 -> cmd1 -> pipe1 -> cmd2
    if (command_index > 0 &&
        dup2(pipes[command_index - 1][0], STDIN_FILENO) < 0) {
        perror("dup2 stdin");
        return -1;
    }

    if (command_index + 1 < command_count &&
        dup2(pipes[command_index][1], STDOUT_FILENO) < 0) {
        perror("dup2 stdout");
        return -1;
    }

    return 0;
}

static int apply_redirections(const Command *command) {
    assert(command != NULL);

    if (command->input_file != NULL) {
        const int fd = open(command->input_file, O_RDONLY);
        if (fd < 0) {
            perror(command->input_file);
            return -1;
        }

        // dup2 后，程序继续读取标准输入即可，无需知道文件 fd 的存在。
        if (dup2(fd, STDIN_FILENO) < 0) {
            perror("dup2 input redirection");
            close(fd);
            return -1;
        }
        close(fd);
    }

    if (command->output_file != NULL) {
        int flags = O_WRONLY | O_CREAT;
        flags |= command->append_output ? O_APPEND : O_TRUNC;

        const int fd = open(command->output_file, flags, 0644);
        if (fd < 0) {
            perror(command->output_file);
            return -1;
        }

        // O_TRUNC 对应 >，O_APPEND 对应 >>。
        if (dup2(fd, STDOUT_FILENO) < 0) {
            perror("dup2 output redirection");
            close(fd);
            return -1;
        }
        close(fd);
    }

    return 0;
}

static int is_job_control_builtin(const char *name) {
    return strcmp(name, "fg") == 0 ||
           strcmp(name, "bg") == 0 ||
           strcmp(name, "wait") == 0;
}

static int validate_pipeline_builtins(const Pipeline *pipeline) {
    if (!pipeline->is_background && pipeline->cursor == 1) {
        return 0;
    }

    for (size_t i = 0; i < pipeline->cursor; i++) {
        const char *name = pipeline->cmd[i]->argv[0];
        if (is_job_control_builtin(name)) {
            /*
             * fg/bg/wait 必须操作父 shell 的 Job 表和控制终端；放进 child
             * 即使调用成功，也只能修改 fork 出来的私有副本，因此直接拒绝。
             */
            fprintf(stderr, "%s: cannot be used in a pipeline or background job\n",
                    name);
            return -1;
        }
    }

    return 0;
}

static void execute_command_in_child(const Command *command) {
    BuiltinType builtin_type = BUILTIN_NONE;
    const BuiltinFunc builtin = get_builtin_func(command->argv[0], &builtin_type);

    if (builtin != NULL) {
        // Pipeline 中的 builtin 也必须在 child 中执行，才能拥有独立的
        // stdin/stdout。cd/exit 在这里仅影响 child，不会改变父 shell。
        if (builtin_type == BUILTIN_PARENT &&
            is_job_control_builtin(command->argv[0])) {
            fprintf(stderr, "%s: cannot be used in a pipeline or background job\n",
                    command->argv[0]);
            _exit(1);
        }

        const int status = builtin(command->argv);
        // _exit() 不会刷新 stdio，所以 builtin 使用 printf 后要主动刷新。
        fflush(NULL);
        _exit(status);
    }

    execvp(command->argv[0], command->argv);
    const int saved_errno = errno;
    perror(command->argv[0]);
    // shell 约定：找不到命令为 127，找到但不能执行为 126。
    _exit(saved_errno == ENOENT || saved_errno == ENOTDIR ? 127 : 126);
}

static int execute_builtin_in_parent(const Command *command,
                                     const BuiltinFunc builtin) {
    // 单独的 cd/exit 等 builtin 必须在父 shell 中运行。
    // parent builtin 仍可带重定向，因此先保存 shell 的标准 fd，执行后恢复。
    const int saved_stdin = dup(STDIN_FILENO);
    if (saved_stdin < 0) {
        perror("dup stdin");
        return 1;
    }

    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        perror("dup stdout");
        close(saved_stdin);
        return 1;
    }

    // exec 成功后不会返回，保存用的临时 fd 不应泄漏给新程序。
    if (fcntl(saved_stdin, F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(saved_stdout, F_SETFD, FD_CLOEXEC) < 0) {
        perror("fcntl FD_CLOEXEC");
        close(saved_stdin);
        close(saved_stdout);
        return 1;
    }

    fflush(NULL);
    int result = 1;

    if (apply_redirections(command) == 0) {
        result = builtin(command->argv);
        fflush(NULL);
    }

    if (dup2(saved_stdin, STDIN_FILENO) < 0) {
        perror("restore stdin");
        result = 1;
    }
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
        perror("restore stdout");
        result = 1;
    }

    close(saved_stdin);
    close(saved_stdout);
    return result;
}

static void terminate_pipeline(const pid_t pgid, const pid_t *pids,
                               const size_t process_count) {
    /*
     * fork 中途失败时，已经创建的 child 不能遗留在后台。先按进程组
     * 终止整条 Pipeline，再逐个 waitpid 回收，避免僵尸进程。
     */
    if (pgid > 0) {
        kill(-pgid, SIGTERM);
        // stopped 进程要先继续运行，才能处理尚未递送的 SIGTERM。
        kill(-pgid, SIGCONT);
    }

    for (size_t i = 0; i < process_count; i++) {
        if (pids[i] <= 0) {
            continue;
        }
        while (waitpid(pids[i], NULL, 0) < 0 && errno == EINTR) {
        }
    }
}

int execute_pipeline(const Pipeline *pipeline, const char *raw_command) {
    assert(pipeline != NULL);
    assert(pipeline->cursor > 0);
    assert(raw_command != NULL);

    /*
     * 执行分为两条路径：
     *
     * 1. 单独前台 BUILTIN_PARENT：留在 parent，执行前临时应用重定向；
     * 2. 其他命令：创建 pipe 和进程组，每个 Command fork 一个 child。
     */
    if (validate_pipeline_builtins(pipeline) < 0) {
        return 1;
    }

    const size_t command_count = pipeline->cursor;
    const size_t pipe_count = command_count - 1;
    const Command *first_command = pipeline->cmd[0];

    BuiltinType first_builtin_type = BUILTIN_NONE;
    const BuiltinFunc first_builtin = get_builtin_func(
            first_command->argv[0], &first_builtin_type);

    // 只有会修改 shell 状态的 builtin 才在 parent 中执行；echo/sleep 等
    // 普通 builtin 走 child 路径，才能正确参与管道、后台任务和终端信号。
    if (command_count == 1 && !pipeline->is_background &&
        first_builtin != NULL && first_builtin_type == BUILTIN_PARENT) {
        return execute_builtin_in_parent(first_command, first_builtin);
    }

    // N 个命令需要 N-1 个 pipe。每个元素保存 [read_fd, write_fd]。
    int (*pipes)[2] = NULL;
    if (pipe_count > 0) {
        pipes = calloc(pipe_count, sizeof(*pipes));
        if (pipes == NULL) {
            perror("calloc");
            return 1;
        }

        for (size_t i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");
                close_pipeline_pipes(pipes, i);
                free(pipes);
                return 1;
            }
        }
    }

    // 保存所有 child PID：既用于等待，也会复制进后台 Job。
    pid_t *pids = calloc(command_count, sizeof(*pids));
    if (pids == NULL) {
        perror("calloc");
        close_pipeline_pipes(pipes, pipe_count);
        free(pipes);
        return 1;
    }

    // fork 会复制用户态 stdio buffer。提前刷新可避免同一段缓冲输出
    // 被 parent 和多个 child 重复打印。
    fflush(NULL);

    pid_t pgid = 0;
    size_t created_count = 0;

    for (size_t i = 0; i < command_count; i++) {
        const pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            close_pipeline_pipes(pipes, pipe_count);
            terminate_pipeline(pgid, pids, created_count);
            free(pids);
            free(pipes);
            return 1;
        }

        if (pid == 0) {
            // 第一个 child 以自身 PID 建立进程组；后续 child 加入该组。
            // 整条 Pipeline 因而能统一接收终端发送的 SIGINT/SIGTSTP。
            const pid_t child_pgid = (pgid == 0) ? getpid() : pgid;
            if (setpgid(0, child_pgid) < 0) {
                perror("setpgid");
                _exit(1);
            }

            reset_child_signals();

            if (connect_pipeline_fds(i, command_count, pipes) < 0) {
                close_pipeline_pipes(pipes, pipe_count);
                _exit(1);
            }

            close_pipeline_pipes(pipes, pipe_count);

            // 显式重定向在 pipe 连接之后应用，因此会覆盖默认 pipe fd。
            if (apply_redirections(pipeline->cmd[i]) < 0) {
                _exit(1);
            }

            execute_command_in_child(pipeline->cmd[i]);
        }

        if (pgid == 0) {
            pgid = pid; // 父进程将子进程组pgid设为第一个子进程pid
        }

        // parent 也调用 setpgid，和 child 形成竞态保护：谁先运行都可以。
        if (setpgid(pid, pgid) < 0 && errno != EACCES && errno != ESRCH) {
            perror("setpgid");
            pids[created_count++] = pid;
            close_pipeline_pipes(pipes, pipe_count);
            terminate_pipeline(pgid, pids, created_count);
            free(pids);
            free(pipes);
            return 1;
        }

        pids[created_count++] = pid;
    }

    // parent 不参与 Pipeline 数据传输，fork 完后必须关闭全部 pipe。
    close_pipeline_pipes(pipes, pipe_count);
    free(pipes);

    char job_command[MAX_CMD_LEN];
    format_job_command(job_command, sizeof(job_command),
                       raw_command, pipeline->is_background);

    if (pipeline->is_background) {
        // Job 会复制 PID 数组，因此 executor 可以在登记后释放本地 pids。
        const int job_id = create_job_for_processes(
                pgid, job_command, pids, created_count);
        if (job_id < 0) {
            fprintf(stderr, "shell: maximum number of jobs exceeded\n");
            terminate_pipeline(pgid, pids, created_count);
            free(pids);
            return 1;
        }

        // 后台启动成功即返回 0；后续状态由 Job 表回收，不改写当前 $?。
        printf("[%d] %d\n", job_id, pgid);
        free(pids);
        return 0;
    }

    if (is_shell_interactive() && give_terminal_to(pgid) < 0) {
        terminate_pipeline(pgid, pids, created_count);
        free(pids);
        return 1;
    }

    size_t remaining = created_count;
    int stopped = 0;
    int result = 0;
    // 未实现 pipefail 时，Pipeline 状态按 shell 默认规则取最后一个命令。
    const pid_t last_pid = pids[command_count - 1];

    // waitpid(-pgid, ...) 等待该进程组中的任意 child。
    // 不能在第一个进程退出时返回，必须等 live child 全部结束。
    while (remaining > 0) {
        int status;
        const pid_t waited = waitpid(-pgid, &status, WUNTRACED);

        if (waited < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ECHILD) {
                break;
            }

            perror("waitpid");
            result = 1;
            break;
        }

        if (WIFSTOPPED(status)) {
            // Ctrl+Z 会发给整个前台进程组。记录剩余 PID 后交给 jobs 管理。
            stopped = 1;
            result = 128 + WSTOPSIG(status);
            break;
        }

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            remaining--;

            for (size_t i = 0; i < created_count; i++) {
                if (pids[i] == waited) {
                    pids[i] = 0;
                    break;
                }
            }

            if (waited == last_pid) {
                if (WIFEXITED(status)) {
                    result = WEXITSTATUS(status);
                } else {
                    result = 128 + WTERMSIG(status);
                }
            }
        }
    }

    if (is_shell_interactive() && reclaim_terminal() < 0) {
        result = 1;
    }

    if (stopped) {
        const int job_id = create_job_for_processes(
                pgid, job_command, pids, created_count);
        if (job_id >= 0) {
            stop_job(job_id);
            printf("\n[%d]  + suspended  %s\n", job_id, job_command);
        } else {
            fprintf(stderr, "shell: maximum number of jobs exceeded\n");
            terminate_pipeline(pgid, pids, created_count);
            result = 1;
        }
    }

    free(pids);
    return result;
}
