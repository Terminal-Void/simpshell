//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_PARSER_H
#define SIMPSHELL_PARSER_H
#include <stddef.h>

typedef enum {
    TOK_WORD,       // 普通单词，比如 echo、hello world
    TOK_PIPE,       // |
    TOK_REDIR_IN,   // <
    TOK_REDIR_OUT,  // >
    TOK_REDIR_APP,  // >>
    TOK_AMP,        // &
    TOK_END
} TokenType;

typedef struct {
    TokenType type;
    char *text;     // 只有 TOK_WORD 需要 text
} Token;

typedef struct {
    char **argv;
    int argc;
    char *input_file;
    char *output_file;
    int append_output;
} Command;

typedef struct {
    Command *cmds;
    int cmd_count;
    int is_background;
    char *raw_text;
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

int tokenize(const char *input, DynamicTokenList **out_tokens);
int parse_tokens_as_command(const DynamicTokenList *tokens, char **cmd_argv,
                            size_t max_args, int *is_background);
int parse_command(char *input, char **cmd_argv, int *is_background);
void expand_tilde(char **argv, char **to_free, int *free_count);
void free_expanded_args(char **to_free, int free_count);
void free_emitted_tokens(Token **tokens, int free_count);
#endif //SIMPSHELL_PARSER_H
