//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_BUILTINS_H
#define SIMPSHELL_BUILTINS_H

typedef int (*BuiltinFunc)(char **argv);

typedef enum {
    BUILTIN_NONE=0,
    // cd/export/alias/jobs 等依赖当前 shell 状态，独立前台时留在 parent。
    BUILTIN_PARENT,
    // echo/sleep/文件命令可走普通 child 路径，参与 pipe、后台和信号处理。
    BUILTIN_REGULAR
} BuiltinType;

// 找到时返回函数指针并写入类型；不是 builtin 时返回 NULL/BUILTIN_NONE。
BuiltinFunc get_builtin_func(const char *cmd,BuiltinType *type);


#endif //SIMPSHELL_BUILTINS_H
