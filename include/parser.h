//
// Created by Terminal Void on 2026/7/7.
//

#ifndef SIMPSHELL_PARSER_H
#define SIMPSHELL_PARSER_H
#include <stddef.h>

typedef enum {
    TOK_WORD=0,       // 普通参数，例如 echo，或引用后的 "hello world"
    TOK_PIPE=1,       // |
    TOK_REDIR_IN=2,   // <
    TOK_REDIR_OUT=3,  // >
    TOK_REDIR_APP=4,  // >>
    TOK_AMP=5         // &
} TokenType;

typedef struct {
    TokenType type;
    // TOK_WORD 独占 text；操作符 Token 的 text 为 NULL。
    char *text;
} Token;

/*
 * 一条可执行命令。argv、重定向文件名都由 Command 独占，
 * create_pipeline_from_tokens() 会从 TokenList 中复制字符串，
 * 因此 Pipeline 与 TokenList 可以分别释放。
 */
typedef struct {
    char **argv;       // 有效命令中以 NULL 结尾；新建空 Command 时可为 NULL。
    size_t argc;       // 不包含结尾 NULL。
    char *input_file;  // NULL 表示没有 < 重定向。
    char *output_file; // NULL 表示没有 > 或 >> 重定向。
    int append_output; // 0 对应 >，1 对应 >>。
} Command;

/* Pipeline 按输入顺序拥有 Command*；cursor 即实际命令数量。 */
typedef struct {
    Command **cmd;
    size_t cursor;
    size_t capacity;
    int is_background;
} Pipeline;

typedef struct {
    char *dstr;
    size_t cursor;   // 当前字符串长度，也是下一个字符写入位置。
    size_t capacity; // 已分配字节数，包含结尾 '\0' 的空间。
} DynamicString;

/* TokenList 独占其中所有 Token* 及 TOK_WORD 的 text。 */
typedef struct {
    Token **tokens;
    size_t cursor;
    size_t capacity;
} DynamicTokenList;

// 将原始输入完成 alias、波浪号和参数展开并转换为 TokenList；失败返回 NULL。
DynamicTokenList* tokenize(const char *input);

// 兼容单命令调用方的轻量转换接口；不支持 pipe 和重定向。
int parse_tokens_as_command(const DynamicTokenList *tokens, char **cmd_argv,
                            size_t max_args, int *is_background);

// 将 TokenList 深拷贝为执行阶段使用的 Pipeline。
Pipeline* create_pipeline_from_tokens(const DynamicTokenList *tokens);

#endif //SIMPSHELL_PARSER_H
