//
// Created by Terminal Void on 2026/7/7.
//

#include "parser.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aliases.h"
#include "shell.h"
#include "utils.h"


static int append_token(DynamicTokenList *tokens, TokenType type, char *text) {
    /*
     * text 的所有权随 Token 一起转交给 TokenList。任一步失败时由本函数
     * 负责释放，调用方无需区分 Token 分配失败还是列表扩容失败。
     */
    Token *token = malloc(sizeof(*token));
    if (token == NULL) {
        perror("malloc");
        free(text);
        return -1;
    }

    token->type = type;
    token->text = text;

    if (append_tokens(tokens, token) < 0) {
        free(token->text);
        free(token);
        return -1;
    }

    return 0;
}

/*
 * 参数展开需要知道字符原来处于哪种引用环境：
 *
 * - 未引用字符中的 $VAR 会展开，并允许进行简化的空白字段分割；
 * - 单引号、反斜杠保护的字符及已完成的波浪号展开按字面量处理；
 * - 双引号中的 $VAR 会展开，但展开结果保持在同一个参数中。
 *
 * Token 最终仍然只保存展开后的字符串，这些模式只在 tokenize()
 * 构造当前 Word 的过程中临时存在。
 */
typedef enum {
    WORD_CHAR_UNQUOTED,
    WORD_CHAR_LITERAL,
    WORD_CHAR_DOUBLE_QUOTED
} WordCharMode;

typedef struct {
    char *text;          // 去除引号后的原始 Word，参数尚未展开。
    unsigned char *modes;// text 中每个字符对应的 WordCharMode。
    size_t *empty_quote_positions; // ''/"" 这种零宽引用在 text 中的位置。
    size_t length;
    size_t capacity;
    size_t empty_quote_count;
    size_t empty_quote_capacity;
    int started;        // 即使 text 为空，出现过空引号也算 Word 已开始。
    int alias_eligible; // 出现引用、转义或波浪号展开后禁止 alias 匹配。
} WordBuffer;

/* 一个 Word 完成参数展开和字段分割后，可能产生 0、1 或多个 argv 字段。 */
typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} ExpandedFields;

static int word_buffer_init(WordBuffer *word, const size_t initial_capacity) {
    assert(word != NULL);
    assert(initial_capacity > 0);

    memset(word, 0, sizeof(*word));
    word->text = malloc(initial_capacity);
    word->modes = malloc(initial_capacity * sizeof(*word->modes));
    if (word->text == NULL || word->modes == NULL) {
        perror("malloc");
        free(word->text);
        free(word->modes);
        memset(word, 0, sizeof(*word));
        return -1;
    }

    word->capacity = initial_capacity;
    word->text[0] = '\0';
    word->alias_eligible = 1;
    return 0;
}

static void word_buffer_clear(WordBuffer *word) {
    assert(word != NULL);
    assert(word->text != NULL);
    assert(word->modes != NULL);

    word->length = 0;
    word->text[0] = '\0';
    word->started = 0;
    word->alias_eligible = 1;
    word->empty_quote_count = 0;
}

static void word_buffer_free(WordBuffer *word) {
    if (word == NULL) {
        return;
    }
    free(word->text);
    free(word->modes);
    free(word->empty_quote_positions);
    memset(word, 0, sizeof(*word));
}

static int word_buffer_reserve(WordBuffer *word, const size_t required) {
    if (required <= word->capacity) {
        return 0;
    }

    size_t new_capacity = word->capacity;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    char *new_text = malloc(new_capacity);
    unsigned char *new_modes = malloc(new_capacity * sizeof(*new_modes));
    if (new_text == NULL || new_modes == NULL) {
        perror("malloc");
        free(new_text);
        free(new_modes);
        return -1;
    }

    memcpy(new_text, word->text, word->length + 1);
    memcpy(new_modes, word->modes, word->length * sizeof(*new_modes));
    free(word->text);
    free(word->modes);
    word->text = new_text;
    word->modes = new_modes;
    word->capacity = new_capacity;
    return 0;
}

static int word_buffer_append_char(WordBuffer *word, const char c,
                                   const WordCharMode mode) {
    if (word_buffer_reserve(word, word->length + 2) < 0) {
        return -1;
    }

    word->text[word->length] = c;
    word->modes[word->length] = (unsigned char)mode;
    word->length++;
    word->text[word->length] = '\0';
    word->started = 1;
    return 0;
}

static int word_buffer_append_cstring(WordBuffer *word, const char *text,
                                      const WordCharMode mode) {
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (word_buffer_append_char(word, text[i], mode) < 0) {
            return -1;
        }
    }
    return 0;
}

static int word_buffer_record_empty_quote(WordBuffer *word) {
    /*
     * 空引号不产生字符，却可能强制保留空参数，并会截断变量名扫描。
     * 记录其零宽位置，展开阶段才能区分 `${X}""` 与 `""${X}`。
     */
    if (word->empty_quote_count == word->empty_quote_capacity) {
        const size_t new_capacity = word->empty_quote_capacity == 0
                                    ? 4
                                    : word->empty_quote_capacity * 2;
        size_t *new_positions = realloc(
                word->empty_quote_positions,
                new_capacity * sizeof(*word->empty_quote_positions));
        if (new_positions == NULL) {
            perror("realloc");
            return -1;
        }
        word->empty_quote_positions = new_positions;
        word->empty_quote_capacity = new_capacity;
    }

    word->empty_quote_positions[word->empty_quote_count++] = word->length;
    return 0;
}

static void free_expanded_fields(ExpandedFields *fields) {
    if (fields == NULL) {
        return;
    }
    for (size_t i = 0; i < fields->count; i++) {
        free(fields->items[i]);
    }
    free(fields->items);
    memset(fields, 0, sizeof(*fields));
}

static int append_expanded_field(ExpandedFields *fields,
                                 const DynamicString *field) {
    if (fields->count == fields->capacity) {
        const size_t new_capacity = fields->capacity == 0
                                    ? 4
                                    : fields->capacity * 2;
        char **new_items = realloc(
                fields->items, new_capacity * sizeof(*fields->items));
        if (new_items == NULL) {
            perror("realloc");
            return -1;
        }
        fields->items = new_items;
        fields->capacity = new_capacity;
    }

    char *text = spawn_cstring_from_DynamicString(field);
    if (text == NULL) {
        return -1;
    }
    fields->items[fields->count++] = text;
    return 0;
}

static int is_parameter_name_start(const int c) {
    return isalpha((unsigned char)c) || c == '_';
}

static int is_parameter_name_char(const int c) {
    return isalnum((unsigned char)c) || c == '_';
}

static int is_field_separator(const char c) {
    // 当前使用默认 IFS 的空白部分；自定义 IFS 可在后续扩展。
    return c == ' ' || c == '\t' || c == '\n';
}

static int word_has_empty_quote_at(const WordBuffer *word,
                                   const size_t position) {
    for (size_t i = 0; i < word->empty_quote_count; i++) {
        if (word->empty_quote_positions[i] == position) {
            return 1;
        }
        if (word->empty_quote_positions[i] > position) {
            break;
        }
    }
    return 0;
}

/*
 * LexerInput 表示“当前要读取的一层字符输入”。
 *
 * 最底层通常是用户输入，例如：
 *
 *     ll|cat
 *
 * 当 ll 被展开成 echo hello 时，不直接修改原字符串，而是把
 * "echo hello" 创建成一个新的 LexerInput，压到原始输入层上面：
 *
 *     顶部: alias 文本 echo hello
 *     底部: 原始输入 ll|cat
 *
 * previous 指向下一层输入，因此所有 LexerInput 通过它组成一个
 * 后进先出的输入栈。lexer_getc() 总是优先读取顶部输入层。
 */
typedef struct LexerInput {
    const char *text;       // 本层字符串；这里只借用，不负责 free。
    size_t position;        // 下一个要读取的字符下标。
    size_t length;          // 本层字符串长度。

    /*
     * 每个输入层拥有独立的一个字符回退槽。
     * 例如原始层读出 | 后发现前面的 ll 是 alias，| 必须留在原始层，
     * 等 alias 层处理完以后再读取。
     */
    int has_pushback;
    int pushed_char;

    /*
     * alias 文本结束后可以返回一次虚拟空格，防止 alias 的最后一个
     * Word 和原始输入中的后续字符粘在一起。
     */
    int add_separator;
    int separator_emitted;

    /* alias 文本是否以空格或制表符结尾。 */
    int trailing_blank;

    /*
     * 根输入层的 alias 为 NULL；alias 输入层保存对应的 AliasEntry。
     * 输入层弹出时，可以据此清除 AliasEntry->in_use。
     */
    AliasEntry *alias;

    struct LexerInput *previous; // 下一层输入；栈底输入的 previous 为 NULL。
} LexerInput;

/*
 * Lexer 是输入栈的控制器，同时保存贯穿整条命令的词法上下文。
 * input 指向栈顶，last_input 记录上一次产生字符的输入层。
 * 后三个字段属于整条命令，而不是某一个输入层，因此放在这里。
 */
typedef struct {
    LexerInput *input;
    LexerInput *last_input;

    int command_position;   // 当前 Word 是否处于命令名称位置。
    int redirection_target; // 当前 Word 是否是重定向后的文件名。
    int expand_next_alias;  // alias 以空白结尾时，允许展开下一个 Word。
} Lexer;

static int lexer_push_input(Lexer *lexer, const char *text,
                            AliasEntry *alias, const int add_separator) {
    /*
     * LexerInput 对象由输入栈拥有，在 lexer_pop_input() 中释放。
     * text 和 alias 只是借用：根层 text 属于 tokenize() 的调用方
     * （main 或 source），alias 层 text 属于全局 AliasEntry。
     */
    LexerInput *input = calloc(1, sizeof(*input));
    if (input == NULL) {
        perror("calloc");
        return -1;
    }

    input->text = text;
    input->length = strlen(text);
    input->add_separator = add_separator;
    input->trailing_blank = input->length > 0 &&
                            (text[input->length - 1] == ' ' ||
                             text[input->length - 1] == '\t');
    input->alias = alias;
    input->previous = lexer->input;
    lexer->input = input;
    return 0;
}

static int lexer_init(Lexer *lexer, const char *input) {
    /*
     * 一条新命令开始时，第一个 Word 位于命令名称位置，因此允许
     * alias 展开。随后把用户输入作为唯一的根输入层压入栈中。
     */
    memset(lexer, 0, sizeof(*lexer));
    lexer->command_position = 1;
    return lexer_push_input(lexer, input, NULL, 0);
}

static void lexer_pop_input(Lexer *lexer) {
    LexerInput *finished = lexer->input;
    lexer->input = finished->previous;

    if (finished->alias != NULL) {
        /*
         * alias 层读完后解除递归保护。否则 alias 执行过一次后会一直
         * 保持 in_use，后续命令就无法再展开它。
         */
        end_alias_expansion(finished->alias);
        if (finished->trailing_blank) {
            // alias 值以空白结尾时，继续检查原输入中的下一个 Word。
            lexer->expand_next_alias = 1;
        }
    }
    free(finished);
}

static void lexer_destroy(Lexer *lexer) {
    /*
     * 正常情况下输入层会在 EOF 时自动弹出；如果中途发生错误，
     * 这里负责清理所有剩余层，并恢复每个 alias 的 in_use 状态。
     */
    while (lexer->input != NULL) {
        lexer_pop_input(lexer);
    }
}

static int lexer_getc(Lexer *lexer) {
    while (lexer->input != NULL) {
        LexerInput *input = lexer->input;

        /* 回退字符优先于真实文本。 */
        if (input->has_pushback) {
            input->has_pushback = 0;
            lexer->last_input = input;
            return input->pushed_char;
        }
        if (input->position < input->length) {
            // 当前层仍有真实字符，返回它并推进 position。
            lexer->last_input = input;
            return (unsigned char)input->text[input->position++];
        }

        /*
         * alias 文本读完后补一次虚拟空格。它不是用户输入的字符，
         * 但能保证 alias 最后的 Word 在语义上和后续输入分开。
         */
        if (input->add_separator && !input->separator_emitted) {
            input->separator_emitted = 1;
            lexer->last_input = input;
            return ' ';
        }

        /* 当前层耗尽，弹出后继续读取 previous 指向的下一层。 */
        lexer_pop_input(lexer);
    }
    return EOF;
}

static void lexer_ungetc(Lexer *lexer, const int c) {
    assert(c != EOF);
    assert(lexer->last_input != NULL);
    assert(!lexer->last_input->has_pushback);

    /*
     * 回退字符属于产生它的输入层，而不是当前的 lexer->input。
     * 例如原始层先读出 |，随后压入 alias 层；| 仍必须回到原始层，
     * 等 alias 层读完后再处理。
     */
    lexer->last_input->has_pushback = 1;
    lexer->last_input->pushed_char = c;
}

static int lexer_peek(Lexer *lexer) {
    /* peek 等价于“读取一个字符，再把它放回原来的输入层”。 */
    const int c = lexer_getc(lexer);
    if (c != EOF) {
        lexer_ungetc(lexer, c);
    }
    return c;
}

static int lexer_push_alias(Lexer *lexer, AliasEntry *alias) {
    /*
     * alias 展开不是复制已有 Token，而是把 alias value 作为新的字符
     * 输入层重新交给同一个 tokenizer。这样 alias 中的引号、管道、
     * 重定向和嵌套 alias 都会复用同一套词法规则。
     */
    const char *text = get_alias_expansion_text(alias);
    if (lexer_push_input(lexer, text, alias, 1) < 0) {
        end_alias_expansion(alias);
        return -1;
    }

    // alias 文本的第一个 Word 也可能是另一个 alias，需要继续检查。
    lexer->expand_next_alias = 1;
    return 0;
}

static int is_word_boundary(const int c) {
    return c == EOF ||
           c == '/' ||
           c == ' ' ||
           c == '\t' ||
           c == '|' ||
           c == '<' ||
           c == '>' ||
           c == '&';
}

static int is_double_quote_escape_target(const int c) {
    return c == '$' ||
           c == '`' ||
           c == '"' ||
           c == '\\' ||
           c == '\n';
}

static int append_parameter_value(ExpandedFields *fields,
                                  DynamicString *current_field,
                                  const char *value, const int split_fields,
                                  int *field_forced) {
    if (!split_fields) {
        // 双引号中的空展开也必须保留一个参数位置。
        *field_forced = 1;
    }

    for (size_t i = 0; value[i] != '\0'; i++) {
        if (split_fields && is_field_separator(value[i])) {
            /*
             * 只有“未引用参数展开产生的空白”参与这里的字段分割；
             * 用户直接输入的空白已在 tokenizer 主循环中结束 Word。
             */
            if (current_field->cursor > 0) {
                if (append_expanded_field(fields, current_field) < 0) {
                    return -1;
                }
                clear_DynamicString(current_field);
                *field_forced = 0;
            }
            continue;
        }

        if (append_char(current_field, value[i]) < 0) {
            return -1;
        }
    }
    return 0;
}

static int append_named_parameter(ExpandedFields *fields,
                                  DynamicString *current_field,
                                  const char *name, const size_t length,
                                  const int split_fields, int *field_forced) {
    char *owned_name = strndup(name, length);
    if (owned_name == NULL) {
        perror("strndup");
        return -1;
    }

    const char *value = getenv(owned_name);
    const int result = append_parameter_value(
            fields, current_field, value == NULL ? "" : value,
            split_fields, field_forced);
    free(owned_name);
    return result;
}

static int append_last_status(ExpandedFields *fields,
                              DynamicString *current_field,
                              const int split_fields, int *field_forced) {
    char status_text[16];
    snprintf(status_text, sizeof(status_text), "%d", get_shell_last_status());
    return append_parameter_value(
            fields, current_field, status_text, split_fields, field_forced);
}

/*
 * 展开一个已经完成 alias 判定的 Word。
 *
 * 参数展开在 alias 展开之后进行，因此 `$CMD` 即使展开成某个 alias
 * 名称，也不会再次触发 alias。未引用变量使用默认空白做简化字段
 * 分割；双引号中的变量只追加到当前字段；单引号和转义字符保持字面量。
 */
static int expand_word_fields(const WordBuffer *word, ExpandedFields *fields) {
    DynamicString *current_field = new_DynamicString(32);
    if (current_field == NULL) {
        return -1;
    }

    size_t empty_quote_index = 0;
    int field_forced = 0;

    for (size_t i = 0; i < word->length; i++) {
        while (empty_quote_index < word->empty_quote_count &&
               word->empty_quote_positions[empty_quote_index] == i) {
            field_forced = 1;
            empty_quote_index++;
        }

        const WordCharMode mode = (WordCharMode)word->modes[i];
        const char c = word->text[i];

        if (c != '$' || mode == WORD_CHAR_LITERAL) {
            if (append_char(current_field, c) < 0) {
                goto fail;
            }
            continue;
        }

        const int split_fields = mode == WORD_CHAR_UNQUOTED;
        if (i + 1 >= word->length ||
            word_has_empty_quote_at(word, i + 1) ||
            word->modes[i + 1] != mode) {
            if (append_char(current_field, '$') < 0) {
                goto fail;
            }
            continue;
        }

        const char next = word->text[i + 1];
        if (next == '?') {
            if (append_last_status(
                    fields, current_field, split_fields, &field_forced) < 0) {
                goto fail;
            }
            i++;
            continue;
        }

        if (next == '{') {
            const size_t name_start = i + 2;
            size_t closing = name_start;
            while (closing < word->length &&
                   !word_has_empty_quote_at(word, closing) &&
                   word->modes[closing] == mode &&
                   word->text[closing] != '}') {
                closing++;
            }

            if (closing >= word->length ||
                word->modes[closing] != mode ||
                word->text[closing] != '}') {
                fprintf(stderr, "shell: bad substitution: missing }\n");
                goto fail;
            }

            const size_t name_length = closing - name_start;
            if (name_length == 1 && word->text[name_start] == '?') {
                if (append_last_status(
                        fields, current_field,
                        split_fields, &field_forced) < 0) {
                    goto fail;
                }
            } else {
                if (name_length == 0 ||
                    !is_parameter_name_start(word->text[name_start])) {
                    fprintf(stderr, "shell: bad substitution\n");
                    goto fail;
                }
                for (size_t j = name_start + 1; j < closing; j++) {
                    if (!is_parameter_name_char(word->text[j])) {
                        fprintf(stderr, "shell: bad substitution\n");
                        goto fail;
                    }
                }
                if (append_named_parameter(
                        fields, current_field,
                        &word->text[name_start], name_length,
                        split_fields, &field_forced) < 0) {
                    goto fail;
                }
            }

            i = closing;
            continue;
        }

        if (is_parameter_name_start(next)) {
            const size_t name_start = i + 1;
            size_t name_end = name_start + 1;
            while (name_end < word->length &&
                   !word_has_empty_quote_at(word, name_end) &&
                   word->modes[name_end] == mode &&
                   is_parameter_name_char(word->text[name_end])) {
                name_end++;
            }

            if (append_named_parameter(
                    fields, current_field,
                    &word->text[name_start], name_end - name_start,
                    split_fields, &field_forced) < 0) {
                goto fail;
            }
            i = name_end - 1;
            continue;
        }

        // 暂未实现 $$、$!、$1 等特殊参数，先保留为普通字面量。
        if (append_char(current_field, '$') < 0) {
            goto fail;
        }
    }

    while (empty_quote_index < word->empty_quote_count &&
           word->empty_quote_positions[empty_quote_index] == word->length) {
        field_forced = 1;
        empty_quote_index++;
    }

    if (current_field->cursor > 0 || field_forced) {
        if (append_expanded_field(fields, current_field) < 0) {
            goto fail;
        }
    }

    free_DynamicString(current_field);
    return 0;

fail:
    free_DynamicString(current_field);
    free_expanded_fields(fields);
    return -1;
}


static int finish_word(DynamicTokenList *tokens, const WordBuffer *word,
                       Lexer *lexer) {
    /*
     * finish_word() 在空白、操作符或 EOF 到来时调用。
     *
     * 返回值：
     *   -1：发生错误；
     *    0：Word 已处理完成，参数展开后可能生成 0、1 或多个 TOK_WORD；
     *    1：当前 Word 是 alias，已压入新的输入层，没有生成 Token。
     */
    char *alias_name = strdup(word->text);
    if (alias_name == NULL) {
        perror("strdup");
        return -1;
    }

    /*
     * alias 只在未引用的命令位置展开：
     *
     * - quoted Word 不能展开，例如 "ll"、'll'、\\ll；
     * - 重定向目标不能展开，例如 echo hi > output；
     * - 命令名称可以展开；
     * - alias 尾随空格时，expand_next_alias 允许展开下一个 Word。
     */
    const int can_expand = word->alias_eligible && !lexer->redirection_target &&
                           (lexer->command_position || lexer->expand_next_alias);
    lexer->expand_next_alias = 0;

    if (can_expand) {
        AliasEntry *alias = begin_alias_expansion(alias_name);
        if (alias != NULL) {
            free(alias_name);
            if (lexer_push_alias(lexer, alias) < 0) {
                return -1;
            }
            // alias 文本会重新进入 lexer，因此这里不生成 TOK_WORD。
            return 1;
        }
    }

    /* alias 没有命中后才做参数展开，避免 `$CMD` 的结果再次触发 alias。 */
    ExpandedFields fields = {0};
    if (expand_word_fields(word, &fields) < 0) {
        free(alias_name);
        return -1;
    }

    if (lexer->redirection_target && fields.count != 1) {
        fprintf(stderr, "shell: ambiguous redirect: %s\n", alias_name);
        free(alias_name);
        free_expanded_fields(&fields);
        return -1;
    }

    free(alias_name);

    for (size_t i = 0; i < fields.count; i++) {
        char *text = fields.items[i];
        fields.items[i] = NULL;
        if (append_token(tokens, TOK_WORD, text) < 0) {
            free_expanded_fields(&fields);
            return -1;
        }
    }

    if (lexer->redirection_target && fields.count == 1) {
        lexer->redirection_target = 0;
    } else if (lexer->command_position && fields.count > 0) {
        lexer->command_position = 0;
    }

    free_expanded_fields(&fields);
    return 0;
}

static int emit_operator(DynamicTokenList *tokens, Lexer *lexer,
                         const TokenType type) {
    /*
     * 操作符直接进入 TokenList，同时更新后续 Word 的上下文：
     * 管道后的 Word 是新命令的命令名称，重定向后的 Word 是文件名。
     */
    if (append_token(tokens, type, NULL) < 0) {
        return -1;
    }

    lexer->expand_next_alias = 0;
    if (type == TOK_PIPE) {
        lexer->command_position = 1;
        lexer->redirection_target = 0;
    } else if (type == TOK_REDIR_IN || type == TOK_REDIR_OUT ||
               type == TOK_REDIR_APP) {
        lexer->redirection_target = 1;
    }
    return 0;
}

DynamicTokenList *tokenize(const char *input) {
    assert(input != NULL);

    /*
     * tokenize() 是字符级扫描的总入口。它负责维护引号和当前 Word，
     * 但不直接关心字符来自用户输入还是 alias 输入；所有字符统一
     * 通过 Lexer 输入栈取得。
     */
    Lexer lexer;
    if (lexer_init(&lexer, input) < 0) {
        return NULL;
    }

    DynamicTokenList *tokens = new_DynamicTokenList(16);
    if (tokens == NULL) {
        lexer_destroy(&lexer);
        return NULL;
    }

    int in_dquote = 0;
    int in_squote = 0;
    size_t dquote_start = 0;
    size_t squote_start = 0;

    /*
     * 扫描优先级：反斜杠先决定下一个字符是否为字面量；随后更新单双
     * 引号状态；只有不在引号中时，空白和 | < > & 才具有语法含义。
     */

    WordBuffer word;
    if (word_buffer_init(&word, 32) < 0) {
        free_DynamicTokenList(tokens);
        lexer_destroy(&lexer);
        return NULL;
    }

    for (;;) {
        const int current = lexer_getc(&lexer);
        if (current == EOF) {
            /*
             * EOF 可能刚好出现在一个 Word 后面。先完成最后的 Word；
             * 如果它触发 alias，新的输入层会被压入，继续下一轮循环。
             */
            if (word.started) {
                const int status = finish_word(tokens, &word, &lexer);
                if (status < 0) {
                    goto fail;
                }
                word_buffer_clear(&word);
                if (status > 0) {
                    continue;
                }
            }
            break;
        }

        const char c = (char)current;

        // 单引号内反斜杠是普通字符；双引号内只转义 shell 规定的字符。
        if (c == '\\' && in_squote == 0) {
            const int next = lexer_getc(&lexer);
            word.alias_eligible = 0;

            if (next != EOF &&
                (!in_dquote || is_double_quote_escape_target(next))) {
                if (word_buffer_append_char(
                        &word, (char)next, WORD_CHAR_LITERAL) < 0) {
                    goto fail;
                }
            } else {
                if (word_buffer_append_char(
                        &word, c, WORD_CHAR_LITERAL) < 0) {
                    goto fail;
                }
                if (next != EOF) {
                    lexer_ungetc(&lexer, next);
                }
            }
            continue;
        }

        //处理出现单引号的情况
        //在双引号内，单引号可以直接当作普通字符，剩余部分按双引号内的字符逻辑处理
        if (c == '\'' && in_dquote == 0) {
            if (!in_squote) {
                in_squote = 1;
                squote_start = word.length;
            } else {
                in_squote = 0;
                if (word.length == squote_start &&
                    word_buffer_record_empty_quote(&word) < 0) {
                    goto fail;
                }
            }
            word.started = 1;
            word.alias_eligible = 0;
            continue;
        }

        //处理出现双引号的情况
        //在单引号内，双引号只是普通字符
        if (c == '\"' && in_squote == 0) {
            if (!in_dquote) {
                in_dquote = 1;
                dquote_start = word.length;
            } else {
                in_dquote = 0;
                if (word.length == dquote_start &&
                    word_buffer_record_empty_quote(&word) < 0) {
                    goto fail;
                }
            }
            word.started = 1;
            word.alias_eligible = 0;
            continue;
        }

        // 波浪号只在 Word 开头且后接 / 或 Token 边界时展开。
        if (c == '~' && in_squote == 0 && in_dquote == 0) {
            if (!word.started && is_word_boundary(lexer_peek(&lexer))) {
                const char *home = getenv("HOME");
                if (home == NULL) {
                    fprintf(stderr,"error parsing tilde: HOME environment variable not set\n");
                    goto fail;
                }
                if (word_buffer_append_cstring(
                        &word, home, WORD_CHAR_LITERAL) < 0) {
                    goto fail;
                }
                // alias 判定应早于波浪号展开，展开后的路径不能再匹配 alias。
                word.alias_eligible = 0;
            }
            else {
                if (word_buffer_append_char(
                        &word, c, WORD_CHAR_UNQUOTED) < 0) {
                    goto fail;
                }
            }
            continue;
        }

        if (!in_squote && !in_dquote && (c == ' ' || c == '\t')) {
            // 空白结束当前 Word；连续空白本身不会生成 Token。
            if (word.started) {
                if (finish_word(tokens, &word, &lexer) < 0) {
                    goto fail;
                }
                word_buffer_clear(&word);
            }
            continue;
        }


        // 普通状态下，识别特殊符号：| < > >> &
        if (!in_squote && !in_dquote && (c == '|' || c == '<' || c == '>' || c == '&')) {
            // 操作符前先完成当前 Word，再生成操作符 Token。

            //emit 当前 word (如有)
            if (word.started) {
                const int status = finish_word(tokens, &word, &lexer);
                if (status < 0) {
                    goto fail;
                }
                word_buffer_clear(&word);
                if (status > 0) {
                    /*
                     * 当前 Word 被 alias 替换。操作符已经从原始层取出，
                     * 先回退到它所属的输入层，等待 alias 层处理完。
                     */
                    lexer_ungetc(&lexer, current);
                    continue;
                }
            }

            // emit 特殊 token
            TokenType type;
            if (c == '|') {
                type = TOK_PIPE;
            }
            else if (c == '<') {
                type = TOK_REDIR_IN;
            }
            else if (c == '>') {
                const int next = lexer_getc(&lexer);
                if (next == '>') {
                    type = TOK_REDIR_APP;
                }
                else {
                    if (next != EOF) {
                        lexer_ungetc(&lexer, next);
                    }
                    type = TOK_REDIR_OUT;
                }
            }
            else {
                type = TOK_AMP;
            }

            if (emit_operator(tokens, &lexer, type) < 0) {
                goto fail;
            }
            continue;
        }


        //处理普通字符
        const WordCharMode mode = in_squote
                                  ? WORD_CHAR_LITERAL
                                  : in_dquote
                                    ? WORD_CHAR_DOUBLE_QUOTED
                                    : WORD_CHAR_UNQUOTED;
        if (word_buffer_append_char(&word, c, mode) < 0) {
            goto fail;
        }
    }

    if (in_squote || in_dquote) {
        fprintf(stderr, "shell: unclosed quote\n");
        goto fail;
    }

    word_buffer_free(&word);
    lexer_destroy(&lexer);
    return tokens;

fail:
    word_buffer_free(&word);
    free_DynamicTokenList(tokens);
    lexer_destroy(&lexer);
    return NULL;
}

int parse_tokens_as_command(const DynamicTokenList *tokens, char **cmd_argv,
                            const size_t max_args, int *is_background) {
    assert(tokens != NULL);
    assert(cmd_argv != NULL);
    assert(max_args > 0);
    assert(is_background != NULL);

    size_t argc = 0;
    *is_background = 0;
    cmd_argv[0] = NULL;

    for (size_t i = 0; i < tokens->cursor; i++) {
        const Token *token = tokens->tokens[i];
        assert(token != NULL);

        if (token->type == TOK_AMP) {
            if (i != tokens->cursor - 1) {
                fprintf(stderr, "shell: & must appear at end\n");
                cmd_argv[0] = NULL;
                return -1;
            }
            *is_background = 1;
            continue;
        }

        if (token->type != TOK_WORD) {
            fprintf(stderr, "shell: pipes and redirections are not supported yet\n");
            cmd_argv[0] = NULL;
            return -1;
        }

        if (argc >= max_args - 1) {
            fprintf(stderr, "shell: too many arguments\n");
            cmd_argv[0] = NULL;
            return -1;
        }

        cmd_argv[argc++] = token->text;
    }

    cmd_argv[argc] = NULL;

    return (int)argc;
}



static Command *new_command(void) {
    // calloc 让 argv 和重定向指针天然从 NULL 开始，便于统一错误清理。
    Command *command = calloc(1, sizeof(*command));
    if (command == NULL) {
        perror("calloc");
        return NULL;
    }

    return command;
}

static int append_argument(Command *command, const char *argument) {
    assert(command != NULL);
    assert(argument != NULL);

    char **new_argv = realloc(
            command->argv,
            (command->argc + 2) * sizeof(*command->argv));

    if (new_argv == NULL) {
        perror("realloc");
        return -1;
    }

    command->argv = new_argv;
    // Command 深拷贝参数，不依赖 TokenList 中文本的生命周期。
    command->argv[command->argc] = strdup(argument);
    if (command->argv[command->argc] == NULL) {
        perror("strdup");
        return -1;
    }

    command->argc++;
    command->argv[command->argc] = NULL;
    return 0;
}

static int set_redirection(char **target, const char *filename,
                           const char *operator_name) {
    assert(target != NULL);
    assert(filename != NULL);
    assert(operator_name != NULL);

    if (*target != NULL) {
        fprintf(stderr, "shell: duplicate %s redirection\n", operator_name);
        return -1;
    }

    // 与 argv 相同，重定向文件名也由 Command 独占一份副本。
    *target = strdup(filename);
    if (*target == NULL) {
        perror("strdup");
        return -1;
    }

    return 0;
}

Pipeline *create_pipeline_from_tokens(const DynamicTokenList *tokens) {
    assert(tokens != NULL);

    if (tokens->cursor == 0) {
        fprintf(stderr, "shell: empty pipeline\n");
        return NULL;
    }

    /*
     * 先创建第一条空 Command，随后单次扫描 TokenList：Word 追加 argv，
     * pipe 切换到下一条 Command，重定向消费紧随其后的文件名 Token。
     */
    Pipeline *pipeline = new_Pipeline(4);
    if (pipeline == NULL) {
        return NULL;
    }

    Command *current_command = new_command();
    if (current_command == NULL || append_command(pipeline, current_command) < 0) {
        free(current_command);
        free_Pipeline(pipeline);
        return NULL;
    }

    for (size_t i = 0; i < tokens->cursor; i++) {
        const Token *token = tokens->tokens[i];
        assert(token != NULL);

        if (token->type == TOK_WORD) {
            if (append_argument(current_command, token->text) < 0) {
                free_Pipeline(pipeline);
                return NULL;
            }
            continue;
        }

        if (token->type == TOK_PIPE) {
            if (current_command->argc == 0) {
                fprintf(stderr, "shell: empty command before pipe\n");
                free_Pipeline(pipeline);
                return NULL;
            }

            current_command = new_command();
            if (current_command == NULL ||
                append_command(pipeline, current_command) < 0) {
                free(current_command);
                free_Pipeline(pipeline);
                return NULL;
            }
            continue;
        }

        if (token->type == TOK_AMP) {
            if (i != tokens->cursor - 1) {
                fprintf(stderr, "shell: & must appear at end\n");
                free_Pipeline(pipeline);
                return NULL;
            }

            pipeline->is_background = 1;
            continue;
        }

        if (i + 1 >= tokens->cursor ||
            tokens->tokens[i + 1]->type != TOK_WORD) {
            fprintf(stderr, "shell: expected filename after redirection\n");
            free_Pipeline(pipeline);
            return NULL;
        }

        const char *filename = tokens->tokens[++i]->text;

        if (token->type == TOK_REDIR_IN) {
            if (set_redirection(&current_command->input_file,
                                filename, "input") < 0) {
                free_Pipeline(pipeline);
                return NULL;
            }
        } else if (token->type == TOK_REDIR_OUT ||
                   token->type == TOK_REDIR_APP) {
            if (set_redirection(&current_command->output_file,
                                filename, "output") < 0) {
                free_Pipeline(pipeline);
                return NULL;
            }
            current_command->append_output = token->type == TOK_REDIR_APP;
        } else {
            fprintf(stderr, "shell: unknown token type\n");
            free_Pipeline(pipeline);
            return NULL;
        }
    }

    if (current_command->argc == 0) {
        fprintf(stderr, "shell: pipeline command is empty\n");
        free_Pipeline(pipeline);
        return NULL;
    }

    return pipeline;
}
