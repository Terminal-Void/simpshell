#include "history.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_CAPACITY 100

typedef struct {
    unsigned long number;
    char *command;
} HistoryEntry;

// 固定容量环形队列：满时覆盖最旧命令，避免历史记录无限增长。
static HistoryEntry history_entries[HISTORY_CAPACITY];
static size_t history_start = 0;
static size_t history_count = 0;
static unsigned long next_history_number = 1;

int add_history_entry(const char *command) {
    assert(command != NULL);

    char *copy = strdup(command);
    if (copy == NULL) {
        perror("strdup");
        return -1;
    }

    size_t index;
    if (history_count < HISTORY_CAPACITY) {
        // 未满时写入逻辑队尾，取模后可能回绕到数组前端。
        index = (history_start + history_count) % HISTORY_CAPACITY;
        history_count++;
    } else {
        // 满时覆盖逻辑队头，并把 start 向后移动一个槽位。
        index = history_start;
        free(history_entries[index].command);
        history_start = (history_start + 1) % HISTORY_CAPACITY;
    }

    history_entries[index].number = next_history_number++;
    history_entries[index].command = copy;
    return 0;
}

void print_history(void) {
    for (size_t i = 0; i < history_count; i++) {
        const size_t index = (history_start + i) % HISTORY_CAPACITY;
        printf("%5lu  %s\n",
               history_entries[index].number,
               history_entries[index].command);
    }
}

void free_history(void) {
    for (size_t i = 0; i < history_count; i++) {
        const size_t index = (history_start + i) % HISTORY_CAPACITY;
        free(history_entries[index].command);
        history_entries[index].command = NULL;
    }
    history_start = 0;
    history_count = 0;
    next_history_number = 1;
}
