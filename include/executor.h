//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_EXECUTOR_H
#define SIMPSHELL_EXECUTOR_H

#include "parser.h"

int execute_pipeline(const Pipeline *pipeline, const char *raw_command);

#endif //SIMPSHELL_EXECUTOR_H
