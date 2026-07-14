#ifndef SIMPSHELL_ALIASES_H
#define SIMPSHELL_ALIASES_H

typedef struct AliasEntry AliasEntry;

const char *get_alias(const char *name);
int set_alias(const char *name, const char *value);
int remove_alias(const char *name);
void print_aliases(void);
void free_aliases(void);

// Lexer 开始读取 alias 文本时设置 in_use，输入栈弹出时再清除。
AliasEntry *begin_alias_expansion(const char *name);
const char *get_alias_expansion_text(const AliasEntry *entry);
void end_alias_expansion(AliasEntry *entry);

#endif // SIMPSHELL_ALIASES_H
