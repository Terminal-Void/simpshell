//
// Created by Terminal Void on 2026/7/9.
//
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 本文件集中管理 parser 公共动态结构的分配与释放。
 * TokenList/Pipeline 一旦成功接收元素，就同时接管该元素的所有权。
 */

// 创建一个始终保持 C 字符串结尾 '\0' 的可增长缓冲区。
DynamicString* new_DynamicString(const size_t initial_capacity) {
    assert(initial_capacity > 0);
    DynamicString* ds = malloc(sizeof(*ds));
    if(ds == NULL) {
        perror("malloc");
        return NULL;
    }

    ds->dstr = malloc(initial_capacity);
    if(ds->dstr == NULL) {
        perror("malloc");
        free(ds);
        return NULL;
    }
    ds->capacity = initial_capacity;
    ds->cursor = 0;
    ds->dstr[0] = '\0';
    return ds;
}

void free_DynamicString(DynamicString* ds) {
    assert(ds != NULL);
    assert(ds->dstr != NULL);
    free(ds->dstr);
    free(ds);
}

int append_char(DynamicString* target,const char c) {
    // cursor 是当前长度，也是本次字符写入位置；capacity 是分配字节数。
    assert(target != NULL);
    assert(target->dstr != NULL);
    assert(target->cursor <= target->capacity);

    // capacity 表示分配的字节数；写入字符后还必须留 1 字节给 '\0'。
    if (target->cursor + 1 >= target->capacity) {
        const size_t new_capacity = (target->capacity) * 2;
        char *new_dstr = realloc(target->dstr, new_capacity);
        //不能直接写target->dstr = realloc(target->dstr, new_capacity);
        //否则若realloc返回NULL，覆盖target指针，会泄漏内存
        if (new_dstr == NULL) {
            perror("realloc");
            return -1;
        }
        target->dstr = new_dstr;
        target->capacity = new_capacity;
    }

    //追加字符，补齐\0
    target->dstr[target->cursor] = c;
    (target->cursor)++;
    target->dstr[target->cursor] = '\0';
    return 0;
}

int append_cstring(DynamicString* target, const char* cstring) {
    assert(target != NULL);
    assert(target->dstr != NULL);
    assert(target->cursor <= target->capacity);
    for (size_t i=0;cstring[i]!='\0';i++) {
        int status = 0;
        if ((status = append_char(target, cstring[i]))!=0) {
            return status;
        }
    }
    return 0;
}

// 只重置逻辑长度，保留已分配容量供下一个 Word 复用。
void clear_DynamicString(DynamicString* target) {
    assert(target != NULL);
    assert(target->dstr != NULL);
    assert(target->capacity > 0);

    target->cursor = 0;
    target->dstr[0] = '\0';
}

// 返回独立副本；调用方取得所有权并负责 free。
char* spawn_cstring_from_DynamicString(const DynamicString* target) {
    assert(target != NULL);
    assert(target->dstr != NULL);
    assert(target->capacity > 0);
    assert(target->dstr[target->cursor] == '\0');

    char* created_cstring = strdup(target->dstr);
    if(created_cstring == NULL) {
        perror("strdup");
        return NULL;
    }
    return created_cstring;
}


DynamicTokenList* new_DynamicTokenList(const size_t n) {
    assert(n > 0);
    DynamicTokenList* dtl = malloc(sizeof(*dtl));
    if(dtl == NULL) {
        perror("malloc");
        return NULL;
    }
    dtl->tokens = malloc(n*sizeof(*dtl->tokens));
    if(dtl->tokens == NULL) {
        perror("malloc");
        free(dtl);
        return NULL;
    }
    dtl->capacity = n;
    dtl->cursor = 0;
    dtl->tokens[0] = NULL;
    return dtl;
}

void free_DynamicTokenList(DynamicTokenList* dtl) {
    if (dtl == NULL) {
        return;
    }
    assert(dtl->tokens != NULL);
    // TokenList 同时拥有 Token 对象和其中的 text。
    for(size_t i = 0; i < dtl->cursor; i++) {
        assert(dtl->tokens[i] != NULL);
        free(dtl->tokens[i]->text);
        free(dtl->tokens[i]);
        dtl->tokens[i] = NULL;
    }
    free(dtl->tokens);
    free(dtl);
}

int append_tokens(DynamicTokenList* target, Token *token) {
    assert(target != NULL);
    assert(target->tokens != NULL);
    assert(target->capacity > 0);
    assert(token != NULL);

    // 多留一个槽位给结尾 NULL，方便按 C 数组习惯遍历。
    if (target->cursor + 1 >= target->capacity) {
        const size_t new_capacity = (target->capacity) * 2;
        Token **new_tokens = realloc(target->tokens, new_capacity * sizeof(*target->tokens));
        if (new_tokens == NULL) {
            perror("realloc");
            return -1;
        }
        target->tokens = new_tokens;
        target->capacity = new_capacity;
    }

    // 成功后 token 所有权转交给列表，并继续维持结尾 NULL。
    target->tokens[target->cursor] = token;
    (target->cursor)++;
    target->tokens[target->cursor] = NULL;
    return 0;
}

Pipeline *new_Pipeline(const size_t initial_cmd_n) {
    assert(initial_cmd_n > 0);
    Pipeline* pl = malloc(sizeof(*pl));
    if(pl == NULL) {
        perror("malloc");
        return NULL;
    }
    pl->cmd = malloc(initial_cmd_n*sizeof(*(pl->cmd)));
    if(pl->cmd == NULL) {
        perror("malloc");
        free(pl);
        return NULL;
    }
    pl->capacity = initial_cmd_n;
    pl->cursor = 0;
    pl->is_background = 0;
    return pl;
}

int append_command(Pipeline* pipeline, Command* command) {
    assert(pipeline != NULL);
    assert(pipeline->cmd != NULL);
    assert(command != NULL);

    if (pipeline->cursor >= pipeline->capacity) {
        const size_t new_capacity = pipeline->capacity * 2;
        Command **new_commands = realloc(
                pipeline->cmd,
                new_capacity * sizeof(*pipeline->cmd));

        if (new_commands == NULL) {
            perror("realloc");
            return -1;
        }

        pipeline->cmd = new_commands;
        pipeline->capacity = new_capacity;
    }

    pipeline->cmd[pipeline->cursor++] = command;
    return 0;
}

void free_Pipeline(Pipeline* pl) {
    if (pl == NULL) {
        return;
    }
    assert(pl->cmd != NULL);
    // Pipeline 递归拥有 Command、argv 字符串和重定向文件名。
    for(size_t i = 0; i < pl->cursor; i++) {
        Command *current_cmd = pl->cmd[i];
        assert(current_cmd != NULL);
        char **current_argv = current_cmd->argv;
        for (size_t j = 0; j < current_cmd->argc; j++) {
            char *current_arg = current_argv[j];
            assert(current_arg != NULL);
            free(current_arg);
        }
        free(current_argv);
        free(current_cmd->input_file);
        free(current_cmd->output_file);
        free(current_cmd);

    }
    free(pl->cmd);
    free(pl);
}
