//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_PARSER_H
#define SIMPSHELL_PARSER_H
#include <stddef.h>

typedef enum {
    TOK_WORD=0,       // 普通单词，比如 echo、hello world
    TOK_PIPE=1,       // |
    TOK_REDIR_IN=2,   // <
    TOK_REDIR_OUT=3,  // >
    TOK_REDIR_APP=4,  // >>
    TOK_AMP=5         // &
} TokenType;

typedef struct {
    TokenType type;
    char *text;     // 只有 TOK_WORD 需要 text
} Token;

typedef struct {
    char **argv;
    size_t argc;
    char *input_file;
    char *output_file;
    int append_output;
} Command;

typedef struct {
    Command **cmd;
    size_t cursor;
    size_t capacity;
    int is_background;
} Pipeline;

typedef struct {
    char *dstr;
    size_t cursor; //下一个即将写入的位置，即上一个\0的位置
    size_t capacity;  //最大下标
} DynamicString;

typedef struct {
    Token **tokens;
    size_t cursor;
    size_t capacity;
} DynamicTokenList;

DynamicTokenList* tokenize(const char *input);
int parse_tokens_as_command(const DynamicTokenList *tokens, char **cmd_argv,
                            size_t max_args, int *is_background);
Pipeline* create_pipeline_from_tokens(const DynamicTokenList *tokens);

#endif //SIMPSHELL_PARSER_H
