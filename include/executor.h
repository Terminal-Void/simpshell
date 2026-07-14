//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_EXECUTOR_H
#define SIMPSHELL_EXECUTOR_H

#include "parser.h"

/*
 * 执行已经完成解析的 Pipeline，并返回 shell 风格状态码。
 * raw_command 只用于 Job 显示，函数不会取得 Pipeline 或字符串所有权。
 */
int execute_pipeline(const Pipeline *pipeline, const char *raw_command);

#endif //SIMPSHELL_EXECUTOR_H
