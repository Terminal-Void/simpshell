//
// Created by Terminal Void on 2026/7/7.
//
#include "jobs.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include "shell.h"

static Job job_table[MAX_JOBS];
static int job_count = 0;

// 每个阶段给程序少量时间自行清理；轮询间隔避免忙等占满 CPU。
#define SHUTDOWN_GRACE_MS 250
#define SHUTDOWN_POLL_NS 10000000L

//通过job_id返回对应job指针
Job *get_job_by_id(const int job_id) {
    const int index = job_id - 1;
    assert(job_id>0 && job_id <= MAX_JOBS);
    if (job_table[index].status != DONE) {
        return &job_table[index];
    }
    return NULL;
}

int create_job_for_processes(const pid_t pgid, const char *cmd,
                             const pid_t *pids, const size_t process_count) {
    assert(pgid > 0);
    assert(cmd != NULL);
    assert(pids != NULL);
    assert(process_count > 0);

    for (int i = 0; i < MAX_JOBS; i++) {
        // 如果这个槽位是空的，或者任务已经 DONE 被清理了
        if (job_table[i].status == DONE) {
            const int job_id = i+1;
            job_table[i].job_id=job_id;
            job_table[i].pgid = pgid;
            // Job 拥有自己的 PID 数组副本，避免依赖 executor 的临时内存。
            job_table[i].pids = calloc(process_count, sizeof(*job_table[i].pids));
            if (job_table[i].pids == NULL) {
                perror("calloc");
                return -1;
            }

            for (size_t j = 0; j < process_count; j++) {
                job_table[i].pids[j] = pids[j];
                if (pids[j] > 0) {
                    job_table[i].live_count++;
                }
            }

            if (job_table[i].live_count == 0) {
                free(job_table[i].pids);
                job_table[i].pids = NULL;
                return -1;
            }

            job_table[i].process_count = process_count;
            job_table[i].status = RUNNING;
            strncpy(job_table[i].cmd,cmd,sizeof(job_table[i].cmd)-1);
            job_table[i].cmd[sizeof(job_table[i].cmd) - 1] = '\0';
            // 正常情况下不会产生作用，因cmd小于MAX_CMD_LEN，
            // 不会出现strncpy因为cmd长度大于sizeof(job_table[i].cmd)-1而截断不添加\0的情况
            job_count++;
            return job_id;
        }
    }
    return -1; // 表满
}

int create_job(const pid_t pgid, const char *cmd) {
    const pid_t pid = pgid;
    return create_job_for_processes(pgid, cmd, &pid, 1);
}

void run_job(const int job_id) {
    const int index = job_id - 1;
    assert(job_id>0 && job_id <= MAX_JOBS);
    assert(job_table[index].status != DONE);
    job_table[index].status = RUNNING;
}

void remove_job(const int job_id) {
    const int index = job_id - 1;
    assert(job_id>0 && job_id <= MAX_JOBS);
    assert(job_table[index].status != DONE);
    free(job_table[index].pids);
    job_table[index].pids = NULL;
    job_table[index].process_count = 0;
    job_table[index].live_count = 0;
    job_table[index].status = DONE;
    job_count--;

}

void record_job_process_exit(const int job_id, const pid_t pid) {
    const int index = job_id - 1;
    assert(job_id > 0 && job_id <= MAX_JOBS);
    assert(job_table[index].status != DONE);

    for (size_t i = 0; i < job_table[index].process_count; i++) {
        if (job_table[index].pids[i] == pid) {
            // PID 置 0 可防止同一退出事件被重复计数。
            job_table[index].pids[i] = 0;
            if (job_table[index].live_count > 0) {
                job_table[index].live_count--;
            }
            return;
        }
    }
}

static Job *get_job_by_pid(const pid_t pid) {
    // waitpid 返回的是具体 PID，而不是 PGID，因此需要反查它属于哪个 Job。
    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        Job *job = get_job_by_id(job_id);
        if (job == NULL) {
            continue;
        }
        for (size_t j = 0; j < job->process_count; j++) {
            if (job->pids[j] == pid) {
                return job;
            }
        }
    }
    return NULL;
}

void stop_job(const int job_id) {
    const int index = job_id - 1;
    assert(job_id > 0 && job_id <= MAX_JOBS);
    assert(job_table[index].status != DONE);
    job_table[index].status = STOPPED;
}

static void handle_job_wait_status(const pid_t pid, const int status,
                                   const int announce) {
    // waitpid() 返回具体 PID，需要先定位它所属的 Pipeline Job。
    // announce 为 0 时只更新状态，退出清理阶段不会重复打印 Done 信息。
    Job *job = get_job_by_pid(pid);
    if (job == NULL) {
        return;
    }

    const int job_id = job->job_id;

    if (WIFEXITED(status) || WIFSIGNALED(status)) {
        record_job_process_exit(job_id, pid);
        // Pipeline leader 退出不代表整个 Job 结束；必须等待 live_count 归零。
        if (job->live_count == 0) {
            if (announce) {
                printf("[%d] Done    %s\n", job_id, job->cmd);
            }
            remove_job(job_id);
        }
        return;
    }

    if (WIFSTOPPED(status)) {
        if (job->status != STOPPED) {
            stop_job(job_id);
            if (announce) {
                printf("\n[%d]  + suspended  %s\n", job_id, job->cmd);
            }
        }
        return;
    }

    if (WIFCONTINUED(status)) {
        run_job(job_id);
    }
}

void check_background_jobs(void) {
    int status;
    pid_t waited;

    // WNOHANG 保证 Prompt 前的状态刷新不会阻塞 shell。
    while ((waited = waitpid(-1, &status,
                             WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        handle_job_wait_status(waited, status, 1);
    }
    if (waited < 0 && errno != ECHILD) {
        perror("waitpid");
    }
}

void print_active_jobs(void) {
    // 统一提供给 jobs builtin 和退出警告使用，避免两处复制输出格式。
    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        const Job *job = get_job_by_id(job_id);
        if (job == NULL) {
            continue;
        }

        printf("[%d] %d %s\t\t%s\n",
               job->job_id,
               job->pgid,
               job->status == RUNNING ? "Running" : "Stopped",
               job->cmd);
    }
}

static void reap_available_jobs(void) {
    int status;
    pid_t waited;

    // 退出宽限期内只回收已经结束的 child，不等待仍在运行的进程。
    while ((waited = waitpid(-1, &status, WNOHANG)) > 0) {
        handle_job_wait_status(waited, status, 0);
    }
    if (waited < 0 && errno != ECHILD) {
        perror("waitpid");
    }
}

static int signal_active_job_groups(const int signal_number) {
    int result = 0;

    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        const Job *job = get_job_by_id(job_id);
        if (job == NULL) {
            continue;
        }

        // PID 为负数时，kill() 会向整个进程组发送信号，即整条 Pipeline。
        if (kill(-job->pgid, signal_number) < 0 && errno != ESRCH) {
            perror("kill");
            result = 1;
        }
    }
    return result;
}

static int continue_stopped_jobs(void) {
    int result = 0;

    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        const Job *job = get_job_by_id(job_id);
        if (job == NULL || job->status != STOPPED) {
            continue;
        }

        // stopped 进程恢复运行后，才能处理 pending 的 SIGHUP/SIGTERM。
        if (kill(-job->pgid, SIGCONT) < 0 && errno != ESRCH) {
            perror("kill SIGCONT");
            result = 1;
        }
    }
    return result;
}

static long long monotonic_milliseconds(void) {
    // 单调时钟不受系统时间调整影响，适合计算等待截止时间。
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        perror("clock_gettime");
        return -1;
    }
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static void wait_for_jobs_for(const long long duration_ms) {
    // 在宽限期内采用“非阻塞回收 + 短暂休眠”，既响应及时又不会忙等。
    const long long start = monotonic_milliseconds();
    if (start < 0) {
        return;
    }

    const long long deadline = start + duration_ms;
    const struct timespec pause = {.tv_sec = 0, .tv_nsec = SHUTDOWN_POLL_NS};

    while (job_count > 0) {
        reap_available_jobs();
        if (job_count == 0) {
            return;
        }

        const long long now = monotonic_milliseconds();
        if (now < 0 || now >= deadline) {
            return;
        }

        // 短暂休眠后再次检查，避免在宽限期内持续占用 CPU。
        struct timespec remaining = pause;
        while (nanosleep(&remaining, &remaining) < 0 && errno == EINTR) {
        }
    }
}

static void discard_stale_jobs(void) {
    // ECHILD 表示内核中已无可等待 child，只需清理内存中的过期记录。
    for (int job_id = 1; job_id <= MAX_JOBS; job_id++) {
        if (get_job_by_id(job_id) != NULL) {
            remove_job(job_id);
        }
    }
}

static void reap_remaining_jobs(void) {
    // SIGKILL 后阻塞等待，保证 shell 不留下僵尸 child 就退出。
    while (job_count > 0) {
        int status;
        const pid_t waited = waitpid(-1, &status, 0);

        if (waited > 0) {
            handle_job_wait_status(waited, status, 0);
            continue;
        }
        if (waited < 0 && errno == EINTR) {
            continue;
        }
        if (waited < 0 && errno != ECHILD) {
            perror("waitpid");
        }
        // ECHILD 表示这些 PID 已经无法再 wait，清掉内存中的过期记录。
        discard_stale_jobs();
        return;
    }
}

int shutdown_active_jobs(void) {
    int result = 0;

    // 第一步：先处理在退出请求到达前已经自然结束的进程。
    reap_available_jobs();
    if (job_count == 0) {
        return 0;
    }

    // 第二步：模拟终端断开，让程序有机会保存状态并正常退出。
    result |= signal_active_job_groups(SIGHUP);
    result |= continue_stopped_jobs();
    wait_for_jobs_for(SHUTDOWN_GRACE_MS);

    if (job_count > 0) {
        // 第三步：对忽略 SIGHUP 的任务明确请求终止。
        result |= signal_active_job_groups(SIGTERM);
        result |= continue_stopped_jobs();
        wait_for_jobs_for(SHUTDOWN_GRACE_MS);
    }

    if (job_count > 0) {
        // 最后兜底：SIGKILL 无法被捕获或忽略，随后必须 waitpid() 回收。
        result |= signal_active_job_groups(SIGKILL);
        reap_remaining_jobs();
    }

    return result;
}

int get_job_count(void) {
    return job_count;
}
