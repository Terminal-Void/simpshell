//
// Created by Terminal Void on 2026/7/7.
//

#include "prompt.h"

#include <assert.h>

#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int format_cwd_with_tilde(const char *cwd, char *buf, size_t buf_size) {
    assert(buf != NULL);
    assert(buf_size !=0 );

    const char *home = getenv("HOME");

    if (home == NULL || home[0] == '\0') {
        const int written = snprintf(buf, buf_size, "%s", cwd);
        return written < 0 || (size_t)written >= buf_size ? -1 : 0;
    }

    const size_t home_len = strlen(home);

    if (strcmp(cwd, home) == 0) {
        const int written = snprintf(buf, buf_size, "~");
        return written < 0 || (size_t)written >= buf_size ? -1 : 0;
    }

    if (strncmp(cwd, home, home_len) == 0 && cwd[home_len] == '/') {
        const int written = snprintf(buf, buf_size, "~%s", cwd + home_len);
        return written < 0 || (size_t)written >= buf_size ? -1 : 0;
    }

    const int written = snprintf(buf, buf_size, "%s", cwd);
    return written < 0 || (size_t)written >= buf_size ? -1 : 0;
}

void print_prompt(void) {
    char cwd[MAX_CMD_LEN];
    char display_cwd[MAX_CMD_LEN];

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("getcwd");
        printf("SimpShell %% ");
        fflush(stdout);
        return;
    }

    if (format_cwd_with_tilde(cwd, display_cwd, sizeof(display_cwd)) != 0) {
        printf("SimpShell:%s %% ", cwd);
    } else {
        printf("SimpShell:%s %% ", display_cwd);
    }

    fflush(stdout);
}