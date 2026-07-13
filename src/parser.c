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

static int is_double_quote_escape_target(const char c) {
    return c == '$' ||
           c == '`' ||
           c == '"' ||
           c == '\\' ||
           c == '\n';
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

DynamicTokenList *tokenize(const char *input) {
    assert(input != NULL);

    DynamicTokenList *tokens = new_DynamicTokenList(16);
    if (tokens == NULL) {
        return NULL;
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
        return NULL;
    }

    for (size_t i=0; input[i] != '\0'; i++) {
        const char c = input[i];

        // 单引号内反斜杠是普通字符；双引号内只转义 shell 规定的字符。
        if (c == '\\' && in_squote == 0) {
            const char next = input[i + 1];

            if (next != '\0' &&
                (!in_dquote || is_double_quote_escape_target(next))) {
                i++;
                if (append_char(word, input[i]) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens=NULL;
                    return NULL;
                }
            } else {
                if (append_char(word, c) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return NULL;
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
                    return NULL;
                }
                if(append_cstring(word, home) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return NULL;
                }
            }
            else {
                if (append_char(word, c) < 0) {
                    free_DynamicString(word);
                    word = NULL;
                    free_DynamicTokenList(tokens);
                    tokens = NULL;
                    return NULL;
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
                    return NULL;
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
                    return NULL;
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
                return NULL;
            }
            continue;
        }


        //处理普通字符
        if (append_char(word, c) < 0) {
            free_DynamicString(word);
            word = NULL;
            free_DynamicTokenList(tokens);
            tokens = NULL;
            return NULL;
        }
        word_started = 1;
    }

    if (in_squote || in_dquote) {
        fprintf(stderr, "shell: unclosed quote\n");
        free_DynamicString(word);
        word = NULL;
        free_DynamicTokenList(tokens);
        tokens = NULL;
        return NULL;
    }

    if (word_started) {
        if (emit_word(tokens, word) < 0) {
            free_DynamicString(word);
            word = NULL;
            free_DynamicTokenList(tokens);
            tokens = NULL;
            return NULL;
        }
    }

    free_DynamicString(word);
    word = NULL;
    return tokens;

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
