#ifndef SIMPSHELL_HISTORY_H
#define SIMPSHELL_HISTORY_H

int add_history_entry(const char *command);
void print_history(void);
void free_history(void);

#endif // SIMPSHELL_HISTORY_H
