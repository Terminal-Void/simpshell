//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_JOBS_H
#define SIMPSHELL_JOBS_H
#include "shell.h"
#include <stddef.h>
#include <sys/types.h>

typedef enum {
    DONE=0,
    RUNNING,
    STOPPED
} JobStatus;

typedef struct {
    int job_id;
    pid_t pgid;               // 整条 Pipeline 共用的进程组 ID
    pid_t *pids;              // Pipeline 中每个 child 的 PID，退出后置 0
    size_t process_count;     // pids 数组长度
    size_t live_count;        // 尚未退出的进程数，为 0 时 Job 才算 Done
    char cmd[MAX_CMD_LEN];
    JobStatus status;
} Job;

/*
 * jobs.c 拥有固定大小的 Job 表和每个 Job 的 pids 数组。
 * 调用方传入的 pids 只会被复制，不会转移所有权。
 */

int create_job(pid_t pgid, const char *cmd);
int create_job_for_processes(pid_t pgid, const char *cmd,
                             const pid_t *pids, size_t process_count);
void remove_job(int job_id);
void run_job(int job_id);
void stop_job(int job_id);
void record_job_process_exit(int job_id, pid_t pid);
// 非阻塞回收后台状态变化；通常在显示 Prompt 和 jobs 操作前调用。
void check_background_jobs(void);

// Job 表由 jobs.c 私有管理，其他模块通过接口显示或关闭任务。
void print_active_jobs(void);
int shutdown_active_jobs(void);

// 返回 jobs.c 内部对象的借用指针，不得 free；Job 删除后立即失效。
Job *get_job_by_id(int job_id);
int get_job_count(void);

#endif //SIMPSHELL_JOBS_H
