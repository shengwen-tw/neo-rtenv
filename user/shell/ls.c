#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/limits.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kconfig.h"
#include "shell.h"

int ls(int argc, char *argv[])
{
    char str[PRINT_SIZE_MAX] = {0};

    /* Get current directory path */
    char path[PATH_MAX] = {0};
    getcwd(path, PATH_MAX);

    if (argc == 2) {
        if (argv[1][0] != '/') {
            /* Input filename using relative path */
            int pos = strlen(path);
            snprintf(&path[pos], PRINT_SIZE_MAX, "/%s", argv[1]);
        } else {
            /* Input filename using absolute path */
            strncpy(path, argv[1], PATH_MAX - 1);
            path[PATH_MAX - 1] = '\0';
        }
    } else if (argc > 2) {
        shell_puts("ls: too many arguments\n\r");
        return 1;
    }

    /* Open the directory */
    DIR dir;
    int retval = opendir(path, &dir);

    /* Check if the directory is open successfully */
    if (retval != 0) {
        snprintf(str, PRINT_SIZE_MAX,
                 "ls: cannot access '%s': No such file or directory\n\r",
                 argv[1]);
        shell_puts(str);
        return 1;
    }

    /* Enumerate the directory */
    int pos = 0;
    struct dirent dirent;
    while ((readdir(&dir, &dirent)) != -1) {
        if (dirent.d_type == S_IFDIR) {
            pos += snprintf(&str[pos], PRINT_SIZE_MAX, "%s/  ", dirent.d_name);
        } else {
            pos += snprintf(&str[pos], PRINT_SIZE_MAX, "%s  ", dirent.d_name);
        }
    }

    snprintf(&str[pos], PRINT_SIZE_MAX, "\n\r");

    shell_puts(str);

    return 0;
}

HOOK_SHELL_CMD("ls", ls);
