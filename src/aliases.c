#include "aliases.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIAS_BUCKET_COUNT 64

struct AliasEntry {
    char *name;              // alias 表独占。
    char *value;             // alias 表独占；LexerInput 只借用该指针。
    int in_use;              // 当前输入栈中是否已有该 alias，防止递归。
    struct AliasEntry *next; // 同一哈希桶内的冲突链。
};

// 静态存储自动初始化为 NULL；每个桶是一条单向链表。
static AliasEntry *alias_table[ALIAS_BUCKET_COUNT];

static size_t hash_alias_name(const char *name) {
    unsigned long hash = 5381;
    for (size_t i = 0; name[i] != '\0'; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)name[i];
    }
    return hash % ALIAS_BUCKET_COUNT;
}

static AliasEntry *find_alias_entry(const char *name) {
    const size_t bucket = hash_alias_name(name);
    for (AliasEntry *entry = alias_table[bucket]; entry != NULL; entry = entry->next) {
        if (strcmp(entry->name, name) == 0) {
            return entry;
        }
    }
    return NULL;
}

const char *get_alias(const char *name) {
    assert(name != NULL);
    const AliasEntry *entry = find_alias_entry(name);
    return entry == NULL ? NULL : entry->value;
}

AliasEntry *begin_alias_expansion(const char *name) {
    /*
     * tokenizer 准备把 alias value 压入 LexerInput 栈之前调用这里。
     * in_use 是“当前调用链正在展开”的标志，不是永久禁用标志：
     * 输入层弹出时会由 end_alias_expansion() 清除它。
     *
     * 例如 alias a='a'：第一次允许展开并设置 in_use，第二次查找
     * 发现 in_use 已经为真，于是把第二个 a 当普通 Word，停止递归。
     */
    AliasEntry *entry = find_alias_entry(name);
    if (entry == NULL || entry->in_use) {
        return NULL;
    }
    entry->in_use = 1;
    return entry;
}

const char *get_alias_expansion_text(const AliasEntry *entry) {
    assert(entry != NULL);
    return entry->value;
}

void end_alias_expansion(AliasEntry *entry) {
    /* alias 输入层生命周期结束，允许该 alias 在后续命令中再次展开。 */
    assert(entry != NULL);
    assert(entry->in_use);
    entry->in_use = 0;
}

int set_alias(const char *name, const char *value) {
    assert(name != NULL);
    assert(value != NULL);

    AliasEntry *entry = find_alias_entry(name);
    if (entry != NULL) {
        // 先成功复制新值再释放旧值，内存不足时原 alias 仍保持有效。
        char *new_value = strdup(value);
        if (new_value == NULL) {
            perror("strdup");
            return -1;
        }
        free(entry->value);
        entry->value = new_value;
        return 0;
    }

    entry = calloc(1, sizeof(*entry));
    if (entry == NULL) {
        perror("calloc");
        return -1;
    }

    entry->name = strdup(name);
    entry->value = strdup(value);
    if (entry->name == NULL || entry->value == NULL) {
        perror("strdup");
        free(entry->name);
        free(entry->value);
        free(entry);
        return -1;
    }

    const size_t bucket = hash_alias_name(name);
    entry->next = alias_table[bucket];
    alias_table[bucket] = entry;
    return 0;
}

int remove_alias(const char *name) {
    assert(name != NULL);

    const size_t bucket = hash_alias_name(name);
    /*
     * link 始终指向“当前节点由哪个指针引用”。使用二级指针后，删除
     * 链表头和中间节点可以共用同一段代码，无需单独保存 previous。
     */
    AliasEntry **link = &alias_table[bucket];

    while (*link != NULL) {
        AliasEntry *entry = *link;
        if (strcmp(entry->name, name) == 0) {
            *link = entry->next;
            free(entry->name);
            free(entry->value);
            free(entry);
            return 0;
        }
        link = &entry->next;
    }
    return -1;
}

static size_t count_aliases(void) {
    size_t count = 0;

    for (size_t bucket = 0; bucket < ALIAS_BUCKET_COUNT; bucket++) {
        for (const AliasEntry *entry = alias_table[bucket];
             entry != NULL; entry = entry->next) {
            count++;
        }
    }

    return count;
}

static int compare_alias_names(const void *left, const void *right) {
    const AliasEntry *const *left_entry = left;
    const AliasEntry *const *right_entry = right;
    return strcmp((*left_entry)->name, (*right_entry)->name);
}

int print_aliases(void) {
    const size_t count = count_aliases();
    if (count == 0) {
        return 0;
    }

    /*
     * 哈希表继续负责快速查找；这里只建立一个借用节点的临时快照，
     * 按名称排序后输出，因此不会改变桶内链表或 AliasEntry 的所有权。
     */
    const AliasEntry **entries = malloc(count * sizeof(*entries));
    if (entries == NULL) {
        perror("malloc");
        return 1;
    }

    size_t index = 0;
    for (size_t bucket = 0; bucket < ALIAS_BUCKET_COUNT; bucket++) {
        for (const AliasEntry *entry = alias_table[bucket];
             entry != NULL; entry = entry->next) {
            entries[index++] = entry;
        }
    }

    qsort(entries, count, sizeof(*entries), compare_alias_names);
    for (size_t i = 0; i < count; i++) {
        printf("alias %s='%s'\n", entries[i]->name, entries[i]->value);
    }

    free(entries);
    return 0;
}

void free_aliases(void) {
    for (size_t bucket = 0; bucket < ALIAS_BUCKET_COUNT; bucket++) {
        AliasEntry *entry = alias_table[bucket];
        while (entry != NULL) {
            AliasEntry *next = entry->next;
            free(entry->name);
            free(entry->value);
            free(entry);
            entry = next;
        }
        alias_table[bucket] = NULL;
    }
}
