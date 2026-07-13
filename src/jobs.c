//
// Created by Terminal Void on 2026/7/7.
//
#include "jobs.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include "shell.h"

static Job job_table[MAX_JOBS];
static int job_count = 0;

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
    for (int i = 0; i < MAX_JOBS; i++) {
        if (job_table[i].status == DONE) {
            continue;
        }

        for (size_t j = 0; j < job_table[i].process_count; j++) {
            if (job_table[i].pids[j] == pid) {
                return &job_table[i];
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

void check_background_jobs(void) {
    int status;
    pid_t dead_pid;

    while ((dead_pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        Job *job = get_job_by_pid(dead_pid);
        if (job == NULL) {
            continue;
        }

        const int job_id = job->job_id;

        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            record_job_process_exit(job_id, dead_pid);
            // Pipeline leader 退出不代表整个 Job 结束；必须等待 live_count 归零。
            if (job->live_count == 0) {
                printf("[%d] Done    %s\n", job_id, job->cmd);
                remove_job(job_id);
            }
        }
        else if (WIFSTOPPED(status)) {
            if (job->status != STOPPED) {
                stop_job(job_id);
                printf("\n[%d]  + suspended  %s\n", job_id, job->cmd);
            }
        }
        else if (WIFCONTINUED(status)) {
            run_job(job_id);
        }
    }
    if (dead_pid < 0 && errno != ECHILD) {
        perror("waitpid");
    }
}

int get_job_count(void) {
    return job_count;
}
