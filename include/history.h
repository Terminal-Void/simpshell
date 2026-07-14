#ifndef SIMPSHELL_HISTORY_H
#define SIMPSHELL_HISTORY_H

// 内部复制 command；固定容量满后覆盖最旧记录。
int add_history_entry(const char *command);
void print_history(void);
void free_history(void);

#endif // SIMPSHELL_HISTORY_H
