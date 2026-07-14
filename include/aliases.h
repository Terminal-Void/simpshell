#ifndef SIMPSHELL_ALIASES_H
#define SIMPSHELL_ALIASES_H

typedef struct AliasEntry AliasEntry;

// 查询函数返回 alias 表内部字符串的借用指针，调用方不得修改或 free。
const char *get_alias(const char *name);
int set_alias(const char *name, const char *value);
int remove_alias(const char *name);
void print_aliases(void);
void free_aliases(void);

// Lexer 开始读取 alias 文本时设置递归保护，输入层弹出时再清除。
AliasEntry *begin_alias_expansion(const char *name);
// 返回 AliasEntry 内部 value 的借用指针，仅在该 entry 存活期间有效。
const char *get_alias_expansion_text(const AliasEntry *entry);
void end_alias_expansion(AliasEntry *entry);

#endif // SIMPSHELL_ALIASES_H
