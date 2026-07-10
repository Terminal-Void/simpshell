//
// Created by Terminal Void on 2026/7/9.
//
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//创建与初始化
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

//释放
void free_DynamicString(DynamicString* ds) {
    assert(ds != NULL);
    assert(ds->dstr != NULL);
    free(ds->dstr);
    free(ds);
}

//添加字符串
int append_char(DynamicString* target,const char c) {
    //cursor：本次即将写入的位置，即上次写入\0的位置
    //capacity: 最大下标，含有\0的空间
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

//软清空内容
void clear_DynamicString(DynamicString* target) {
    assert(target != NULL);
    assert(target->dstr != NULL);
    assert(target->capacity > 0);

    target->cursor = 0;
    target->dstr[0] = '\0';
}

//从变长字符串中复制产生C风格字符串，需要手动free
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

    //追加 token，补齐 NULL
    target->tokens[target->cursor] = token;
    (target->cursor)++;
    target->tokens[target->cursor] = NULL;
    return 0;
}

char* spawn_cmdstring_from_DynamicTokenList(const DynamicTokenList* source) {
    assert(source != NULL);
    DynamicString* dyn_cmdstring = new_DynamicString(32);
    if(dyn_cmdstring == NULL) {
        perror("malloc");
        return NULL;
    }
    for (size_t i=0;i<source->cursor;i++) {
        assert(source->tokens[i] != NULL);
        if (i > 0 && append_char(dyn_cmdstring, ' ') < 0) {
            free_DynamicString(dyn_cmdstring);
            return NULL;
        }
        const Token *this_token = source->tokens[i];
        int status = 0;
        if (this_token->type == TOK_WORD) {
            status=append_cstring(dyn_cmdstring,this_token->text);
        }
        else {
            if (this_token->type == TOK_PIPE) {
                status=append_char(dyn_cmdstring,'|');
            }
            else if (this_token->type == TOK_REDIR_IN) {
                status=append_char(dyn_cmdstring,'<');
            }
            else if (this_token->type == TOK_REDIR_OUT) {
                status=append_char(dyn_cmdstring,'>');
            }
            else if (this_token->type == TOK_REDIR_APP) {
                status=append_cstring(dyn_cmdstring,">>");
            }
            else if (this_token->type == TOK_AMP) {
                status=append_char(dyn_cmdstring,'&');
            }
        }
        if (status!=0) {
            free_DynamicString(dyn_cmdstring);
            return NULL;
        }
    }
    char* cmd_string = spawn_cstring_from_DynamicString(dyn_cmdstring);
    free_DynamicString(dyn_cmdstring);
    return cmd_string;
}