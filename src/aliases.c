#include "aliases.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ALIAS_BUCKET_COUNT 64

struct AliasEntry {
    char *name;
    char *value;
    int in_use;
    struct AliasEntry *next;
};

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

void print_aliases(void) {
    for (size_t bucket = 0; bucket < ALIAS_BUCKET_COUNT; bucket++) {
        for (const AliasEntry *entry = alias_table[bucket];
             entry != NULL; entry = entry->next) {
            printf("alias %s='%s'\n", entry->name, entry->value);
        }
    }
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
