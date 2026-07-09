//
// Created by Terminal Void on 2026/7/9.
//

#ifndef SIMPSHELL_UTILS_H
#define SIMPSHELL_UTILS_H
#include <stddef.h>

#include "parser.h"


DynamicString* new_DynamicString(size_t initial_capacity);
void free_DynamicString(DynamicString* ds);
int append_char(DynamicString* target,char c);
void clear_DynamicString(DynamicString* target);
char* spawn_cstring_from_DynamicString(const DynamicString* target);

DynamicTokenList* new_DynamicTokenList(size_t n);
void free_DynamicTokenList(DynamicTokenList* dtl);
int append_tokens(DynamicTokenList* target, Token *token);

#endif //SIMPSHELL_UTILS_H
