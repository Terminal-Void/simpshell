//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_BUILTINS_H
#define SIMPSHELL_BUILTINS_H

typedef int (*BuiltinFunc)(char **argv);

typedef enum {
    BUILTIN_NONE=0,
    BUILTIN_PARENT,  // 必须修改 shell 自身状态
    BUILTIN_REGULAR  // 可以在 child 中运行
} BuiltinType;

BuiltinFunc get_builtin_func(const char *cmd,BuiltinType *type);


#endif //SIMPSHELL_BUILTINS_H
