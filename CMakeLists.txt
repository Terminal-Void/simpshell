cmake_minimum_required(VERSION 3.20)

project(simpshell C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(simpshell
        src/main.c
        src/shell.c
        src/aliases.c
        src/history.c
        src/parser.c
        src/builtins.c
        src/executor.c
        src/jobs.c
        src/prompt.c
        src/terminal.c
        src/utils.c
)

target_include_directories(simpshell PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(simpshell PRIVATE
        # 公开 getline、setenv、utimensat、clock_gettime 等 POSIX.1-2008 接口。
        _POSIX_C_SOURCE=200809L
)

target_compile_options(simpshell PRIVATE
        -Wall
        -Wextra
        -Wpedantic
)
