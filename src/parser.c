//
// Created by Terminal Void on 2026/7/7.
//

#include "parser.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include "utils.h"


static int append_token(DynamicTokenList *tokens, TokenType type, char *text) {
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

static int is_word_boundary(char c) {
    return c == '\0' ||
           c == '/' ||
           c == ' ' ||
           c == '\t' ||
           c == '|' ||
           c == '<' ||
           c == '>' ||
           c == '&';
}


static int emit_word(DynamicTokenList *tokens, const DynamicString *word) {
    char *text = spawn_cstring_from_DynamicString(word);
    if (text == NULL) {
        return -1;
    }

    return append_token(tokens, TOK_WORD, text);
}

static int emit_operator(DynamicTokenList *tokens, const TokenType type) {
    return append_token(tokens, type, NULL);
}

int tokenize(const char *input, DynamicTokenList **out_tokens) {
    assert(input != NULL);
    assert(out_tokens != NULL);
    assert(*out_tokens == NULL);

    DynamicTokenList *tokens = new_DynamicTokenList(16);
    if (tokens == NULL) {
        return -1;
    }

    int in_dquote = 0;
    int in_squote = 0;
    int word_started = 0;

    //处理 backslash dquote squote pipe redir_io amp
    //处理优先级 backslash > squote > dquote > 其他

    DynamicString *word = new_DynamicString(32);
    if (word == NULL) {
        free_DynamicTokenList(tokens);
        tokens=NULL;
        return -1;
    }

    for (size_t i=0; input[i] != '\0'; i++) {
        const char c = input[i];

        //处理反斜杠，
        //单引号内反斜杠为普通字符，双引号内反斜杠正常发挥作用
        if (c == '\\' && in_squote == 0) {
            if (input[i+1] != '\0') {
                i++;
                if (append_char(word, input[i]) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens=NULL;
                    return -1;
                }
            } else {
                if (append_char(word, c) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
            }
            word_started = 1;
            continue;
        }

        //处理出现单引号的情况
        //在双引号内，单引号可以直接当作普通字符，剩余部分按双引号内的字符逻辑处理
        if (c == '\'' && in_dquote == 0) {
            in_squote = !in_squote;
            word_started = 1;
            continue;
        }

        //处理出现双引号的情况
        //在单引号内，双引号只是普通字符
        if (c == '\"' && in_squote == 0) {
            in_dquote = !in_dquote;
            word_started = 1;
            continue;
        }

        //处理波浪号，出现在开头且后面是空或者/的展开，其他情况当作普通字符处理
        if (c == '~' && in_squote == 0 && in_dquote == 0) {
            if (!word_started && is_word_boundary(input[i+1])) {
                const char *home = getenv("HOME");
                if (home == NULL) {
                    fprintf(stderr,"error parsing tilde: HOME environment variable not set\n");
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
                if(append_cstring(word, home) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
            }
            else {
                if (append_char(word, c) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
            }
            word_started = 1;
            continue;
        }

        if (!in_squote && !in_dquote && (c == ' ' || c == '\t')) {
            // 这里应该 emit 当前 word
            if (word_started) {
                if (emit_word(tokens, word) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
                clear_DynamicString(word);
                word_started = 0;
            }
            continue;
        }


        // 普通状态下，识别特殊符号：| < > >> &
        if (!in_squote && !in_dquote && (c == '|' || c == '<' || c == '>' || c == '&')) {
            // 这里先 emit 当前 word，再 emit 特殊 token

            //emit 当前 word (如有)
            if (word_started) {
                if (emit_word(tokens, word) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return -1;
                }
                clear_DynamicString(word);
                word_started = 0;
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
                if (input[i+1] != '\0' && input[i+1] == '>') {
                    i++;
                    type = TOK_REDIR_APP;
                }
                else {
                    type = TOK_REDIR_OUT;
                }
            }
            else {
                type = TOK_AMP;
            }

            if (emit_operator(tokens, type) < 0) {
                free_DynamicString(word);
                word = NULL;
                free_DynamicTokenList(tokens);
                tokens = NULL;
                return -1;
            }
            continue;
        }


        //处理普通字符
        if (append_char(word, c) < 0) {
            free_DynamicString(word);
            word = NULL;
            free_DynamicTokenList(tokens);
            tokens = NULL;
            return -1;
        }
        word_started = 1;
    }

    if (in_squote || in_dquote) {
        fprintf(stderr, "shell: unclosed quote\n");
        free_DynamicString(word);
        word = NULL;
        free_DynamicTokenList(tokens);
        tokens = NULL;
        return -1;
    }

    if (word_started) {
        if (emit_word(tokens, word) < 0) {
            free_DynamicString(word);
            word = NULL;
            free_DynamicTokenList(tokens);
            tokens = NULL;
            return -1;
        }
    }

    free_DynamicString(word);
    word = NULL;
    *out_tokens = tokens;
    return 0;

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