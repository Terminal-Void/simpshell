#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_CMD_LEN 1024

int main() {
    char cmd[MAX_CMD_LEN];

    while (1) {
        // 1. 打印Prompt
        printf("SimpShell %% ");
        fflush(stdout); // 强制刷新输出缓冲区。因为没有换行符 '\n'，不 fflush 的话提示符可能不会立刻显示在终端上

        // 2. 读取用户输入
        if (fgets(cmd, MAX_CMD_LEN, stdin) == NULL) {
            // 处理按下 Ctrl+D (EOF) 的情况：优雅退出
            printf("\n");
            break;
        }

        // 3. 去除字符串末尾的换行符
        // 用户按下回车时，fgets 会把 '\n' 也读进来，我们需要把它替换成字符串结束符 '\0'
        cmd[strcspn(cmd, "\n")] = '\0';

        // 如果用户什么都没输直接按了回车，跳过本次循环
        if (strlen(cmd) == 0) {
            continue;
        }

        // 4. 实现第一个内置命令：exit
        if (strcmp(cmd, "exit") == 0) {
            printf("Bye!\n");
            break;
        }

        // 临时测试：回显用户输入，证明框架跑通了
        printf("准备执行命令: %s\n", cmd);
    }

    return 0;
}