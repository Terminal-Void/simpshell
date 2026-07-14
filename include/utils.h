//
// Created by Terminal Void on 2026/7/9.
//

#ifndef SIMPSHELL_UTILS_H
#define SIMPSHELL_UTILS_H
#include <stddef.h>

#include "parser.h"


/* DynamicString 始终维护结尾 '\0'；spawn 返回的新字符串由调用方 free。 */
DynamicString* new_DynamicString(size_t initial_capacity);
void free_DynamicString(DynamicString* ds);
int append_char(DynamicString* target,char c);
void clear_DynamicString(DynamicString* target);
char* spawn_cstring_from_DynamicString(const DynamicString* target);

/* append_tokens 成功后 Token 所有权转交给列表。 */
DynamicTokenList* new_DynamicTokenList(size_t n);
void free_DynamicTokenList(DynamicTokenList* dtl);
int append_tokens(DynamicTokenList* target, Token *token);
int append_cstring(DynamicString* target, const char* cstring);

/* append_command 成功后 Command 所有权转交给 Pipeline。 */
Pipeline* new_Pipeline(size_t initial_cmd_n);
int append_command(Pipeline* pipeline, Command* command);
void free_Pipeline(Pipeline* pipeline);

#endif //SIMPSHELL_UTILS_H
