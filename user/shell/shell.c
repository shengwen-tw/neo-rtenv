#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <common/list.h>

#include "shell.h"

void shell_path_init(void);

static void shell_reset_line(struct shell *shell);
static void shell_reset_autocomplete(struct shell *shell);
static void shell_reset_history_scrolling(struct shell *shell);

static int serial_fd = 0;

void shell_serial_init(void)
{
    serial_fd = open("/dev/console", O_RDWR);
}

char shell_getc(void)
{
    char c;
    read(serial_fd, &c, 1);
    return c;
}

void shell_puts(char *s)
{
    write(serial_fd, s, strlen(s));
}

static void shell_ctrl_c_handler(struct shell *shell)
{
    shell_reset_autocomplete(shell);
    shell_reset_history_scrolling(shell);

    shell_puts("^C\n\r");
    shell_puts(shell->prompt);
    shell_reset_line(shell);
}

static void shell_unknown_cmd_handler(int argc, char *argv[])
{
    char s[50 + LINE_MAX];
    snprintf(s, 50 + LINE_MAX, "unknown command: %s\n\r", argv[0]);
    shell_puts(s);
}

void shell_cls(void)
{
    shell_puts("\x1b[H\x1b[2J");
}

void shell_init(struct shell *shell,
                struct shell_cmd *shell_cmds,
                int cmd_cnt,
                struct shell_history *history,
                int history_max_cnt,
                struct shell_autocompl *autocompl)
{
    shell->prompt[0] = '\0';
    shell->prompt_len = 0;
    shell->cursor_pos = 0;
    shell->buf[0] = '\0';
    shell->char_cnt = 0;
    shell->shell_cmds = shell_cmds;
    shell->cmd_cnt = cmd_cnt;
    shell->history = history;
    shell->history_cnt = 0;
    shell->history_max_cnt = history_max_cnt;
    shell->show_history = false;
    shell->show_autocompl = false;
    shell->autocompl = autocompl;
    shell->autocompl_curr = 0;
    shell->autocompl_cnt = 0;
    INIT_LIST_HEAD(&shell->history_head);

    int i;
    for (i = 0; i < cmd_cnt; i++) {
        shell->autocompl[i].cmd[0] = '\0';
    }
}

/* Minimal configuration of which does not support command parsing,
 * history saving, and command completion */
void shell_init_minimal(struct shell *shell)
{
    shell->prompt[0] = '\0';
    shell->prompt_len = 0;
    shell->cursor_pos = 0;
    shell->buf[0] = '\0';
    shell->char_cnt = 0;
    shell->shell_cmds = NULL;
    shell->cmd_cnt = 0;
    shell->history = NULL;
    shell->history_cnt = 0;
    shell->history_max_cnt = 0;
    shell->show_history = false;
    shell->show_autocompl = false;
    shell->autocompl = NULL;
    shell->autocompl_curr = 0;
    shell->autocompl_cnt = 0;
    INIT_LIST_HEAD(&shell->history_head);
}

void shell_set_prompt(struct shell *shell, char *new_prompt)
{
    strncpy(shell->prompt, new_prompt, LINE_MAX - 1);
    shell->prompt_len = strlen(shell->prompt);
}

static void shell_reset_line(struct shell *shell)
{
    shell->cursor_pos = 0;
    shell->char_cnt = 0;
}

static void shell_remove_char(struct shell *shell, int remove_pos)
{
    int i;
    for (i = (remove_pos - 1); i < (shell->char_cnt); i++) {
        shell->buf[i] = shell->buf[i + 1];
    }

    shell->buf[shell->char_cnt] = '\0';
    shell->char_cnt--;
    shell->cursor_pos--;

    if (shell->cursor_pos > shell->char_cnt) {
        shell->cursor_pos = shell->char_cnt;
    }
}

static void shell_insert_char(struct shell *shell, char c)
{
    int i;
    for (i = shell->char_cnt; i > (shell->cursor_pos - 1); i--) {
        shell->buf[i] = shell->buf[i - 1];
    }
    shell->char_cnt++;
    shell->buf[shell->char_cnt] = '\0';

    shell->buf[shell->cursor_pos] = c;
    shell->cursor_pos++;
}

static void shell_refresh_line(struct shell *shell)
{
    char s[LINE_MAX * 5];
    snprintf(s, LINE_MAX * 5,
             "\33[2K\r"  /* Clear current line */
             "%s%s\r"    /* Show prompt message */
             "\033[%dC", /* Move cursor position */
             shell->prompt, shell->buf, shell->prompt_len + shell->cursor_pos);
    shell_puts(s);
}

static void shell_cursor_shift_one_left(struct shell *shell)
{
    if (shell->cursor_pos <= 0)
        return;

    shell->cursor_pos--;
    shell_refresh_line(shell);
}

static void shell_cursor_shift_one_right(struct shell *shell)
{
    if (shell->cursor_pos >= shell->char_cnt)
        return;

    shell->cursor_pos++;
    shell_refresh_line(shell);
}

static void shell_reset_history_scrolling(struct shell *shell)
{
    shell->show_history = false;
}

static void shell_push_new_history(struct shell *shell, char *cmd)
{
    if (shell->history == NULL) {
        return;
    }

    struct list_head *history_list;
    struct shell_history *history_new;

    /* Check the size of the history list */
    if (shell->history_cnt < shell->history_max_cnt) {
        /* History list is not full, allocate new memory space */
        history_list =
            &shell->history[shell->history_max_cnt - shell->history_cnt - 1]
                 .list;
        shell->history_cnt++;

        /* Push new history record into the list */
        history_new = list_entry(history_list, struct shell_history, list);
        strncpy(history_new->cmd, shell->buf, LINE_MAX);
        list_add(history_list, &shell->history_head);
    } else {
        /* History list is full, remove the oldest record */
        history_list = shell->history_head.next;
        list_del(history_list);

        /* Push new history record into the list */
        history_new = list_entry(history_list, struct shell_history, list);
        strncpy(history_new->cmd, shell->buf, LINE_MAX);
        list_add(history_list, &shell->history_head);
    }

    /* Initialize the history pointer */
    shell->history_curr = &shell->history_head;

    /* Reset the history reading */
    shell->show_history = false;
}

static void shell_reset_autocomplete(struct shell *shell)
{
    shell->show_autocompl = false;
}

static void shell_generate_suggest_words(struct shell *shell,
                                         int argv0_start,
                                         int argv0_end)
{
    /* Reset the recommendation candidate count */
    shell->autocompl_cnt = 0;

    /* Calculate the length of the first argument */
    int argv0_len = shell->cursor_pos - argv0_start;

    /* Calculate the length of the whole user input */
    int cmd_len = strlen(shell->buf);

    /* Get the first argument that get rid of the spaces */
    char *user_cmd = &shell->buf[argv0_start];

    int i;
    for (i = 0; i < shell->cmd_cnt; i++) {
        /* Load shell command from the command list for comparing */
        char *shell_cmd = shell->shell_cmds[i].name;

        /* Check if the user input matches the shell command */
        if (strncmp(user_cmd, shell_cmd, argv0_len) == 0) {
            char *suggest_candidate;

            /* Copy string from the beginning to the character before the first
             * argument */
            suggest_candidate = shell->autocompl[shell->autocompl_cnt].cmd;
            memcpy(suggest_candidate, &shell->buf[0], argv0_start);

            /* Insert the name of the matched shell command */
            suggest_candidate += argv0_start;
            memcpy(suggest_candidate, shell_cmd, strlen(shell_cmd));

            /* Copy the string from the insertion tail to the end of the first
             * argument */
            suggest_candidate += strlen(shell_cmd);
            int cnt = &shell->buf[argv0_end] - &shell->buf[shell->cursor_pos];
            memcpy(suggest_candidate, &shell->buf[shell->cursor_pos], cnt);

            /* Copy string from the tail of the first argument to the end */
            suggest_candidate += cnt;
            cnt = &shell->buf[cmd_len] - &shell->buf[argv0_end];
            memcpy(suggest_candidate, &shell->buf[argv0_end], cnt);

            /* Append end character */
            suggest_candidate += cnt;
            *suggest_candidate = '\0';

            shell->autocompl_cnt++;
        }
    }
}

static void shell_autocomplete(struct shell *shell)
{
    if (shell->autocompl == NULL) {
        return;
    }

    /* Find the start position of the first argument */
    int argv0_start = 0;
    while ((shell->buf[argv0_start] == ' ') && (argv0_start < shell->char_cnt))
        argv0_start++;

    /* Find the end position of the first argument */
    int argv0_end = argv0_start;
    while ((shell->buf[argv0_end] != ' ') && (argv0_end < shell->char_cnt))
        argv0_end++;


    /* Autocomplete is disabled after the first argument */
    if (shell->cursor_pos > argv0_end)
        return;

    /* Autocomplete is triggered before the first argument (i.e., spaces) */
    if (shell->cursor_pos < argv0_start) {
        argv0_start = shell->cursor_pos;
        argv0_end = shell->cursor_pos;
    }

    /* Reset the autocomplete if the cursor position changed */
    if (shell->autocompl_cursor_pos != shell->cursor_pos) {
        shell_reset_autocomplete(shell);
    }

    /* Check if the autocomple has been initialized */
    if (shell->show_autocompl == false) {
        /* Backup the user input */
        strncpy(shell->input_backup, shell->buf, LINE_MAX);

        /* Generate the recommendation word dictionary */
        shell_generate_suggest_words(shell, argv0_start, argv0_end);

        /* Record the cursor positon */
        shell->autocompl_cursor_pos = shell->cursor_pos;

        shell->autocompl_curr = 0;
        shell->show_autocompl = true;
    }

    /* Check if there are more candidate words or not */
    if (shell->autocompl_curr == shell->autocompl_cnt) {
        /* Restore the user input */
        strncpy(shell->buf, shell->input_backup, LINE_MAX);
        shell_reset_autocomplete(shell);
    } else {
        /* Overwrite the user input with autocomplete result */
        char *suggestion = shell->autocompl[shell->autocompl_curr].cmd;
        strncpy(shell->buf, suggestion, LINE_MAX);
    }

    /* Prepare the next candidate word to show */
    shell->autocompl_curr++;

    /* Refresh the line */
    shell->char_cnt = strlen(shell->buf);
    shell_refresh_line(shell);
}

void shell_print_history(struct shell *shell)
{
    if (shell->history == NULL) {
        return;
    }

    char s[LINE_MAX * 3];

    struct list_head *list_curr;
    struct shell_history *history;

    int i = 0;
    list_for_each (list_curr, &shell->history_head) {
        history = list_entry(list_curr, struct shell_history, list);

        snprintf(s, LINE_MAX * 3, "%d %s\n\r", ++i, history->cmd);
        shell_puts(s);
    }
}

static void shell_ctrl_a_handler(struct shell *shell)
{
    shell->cursor_pos = 0;
    shell_refresh_line(shell);
}

static void shell_ctrl_e_handler(struct shell *shell)
{
    if (shell->char_cnt == 0)
        return;

    shell->cursor_pos = shell->char_cnt;
    shell_refresh_line(shell);
}

static void shell_ctrl_u_handler(struct shell *shell)
{
    shell->buf[0] = '\0';
    shell->char_cnt = 0;
    shell->cursor_pos = 0;
    shell_refresh_line(shell);
}

static void shell_up_arrow_handler(struct shell *shell)
{
    if (shell->history == NULL) {
        return;
    }

    /* Ignore the key pressing if the history list is empty */
    if (list_empty(&shell->history_head) == true)
        return;

    if (shell->show_history == false) {
        /* Backup the user input */
        strncpy(shell->input_backup, shell->buf, LINE_MAX);

        /* History reading mode is now on */
        shell->show_history = true;
    }

    /* Load the next history to show */
    shell->history_curr = shell->history_curr->prev;

    /* Check if there is more hisotry to show */
    if (shell->history_curr == &shell->history_head) {
        /* Restore user input if traversal of history list is done */
        strncpy(shell->buf, shell->input_backup, LINE_MAX);
        shell->show_history = false;
    } else {
        /* Display the history to the user */
        struct shell_history *history =
            list_entry(shell->history_curr, struct shell_history, list);
        strncpy(shell->buf, history->cmd, LINE_MAX);
    }

    /* Refresh the line */
    shell->char_cnt = strlen(shell->buf);
    shell->cursor_pos = shell->char_cnt;
    shell_refresh_line(shell);
}

static void shell_down_arrow_handler(struct shell *shell)
{
    if (shell->history == NULL) {
        return;
    }

    /* Return if user did not trigger history reading */
    if (shell->show_history == false)
        return;

    /* Load previos history */
    shell->history_curr = shell->history_curr->next;

    /* Check if the traversal of history list is done */
    if (shell->history_curr == &shell->history_head) {
        /* Restore user input */
        strncpy(shell->buf, shell->input_backup, LINE_MAX);
        shell->show_history = false;
    } else {
        /* Display history to the user */
        struct shell_history *history =
            list_entry(shell->history_curr, struct shell_history, list);
        strncpy(shell->buf, history->cmd, LINE_MAX);
    }

    /* Refresh the line */
    shell->char_cnt = strlen(shell->buf);
    shell->cursor_pos = shell->char_cnt;
    shell_refresh_line(shell);
}

static void shell_right_arrow_handler(struct shell *shell)
{
    shell_cursor_shift_one_right(shell);
}

static void shell_left_arrow_handler(struct shell *shell)
{
    shell_cursor_shift_one_left(shell);
}

static void shell_home_handler(struct shell *shell)
{
    shell->cursor_pos = 0;
    shell_refresh_line(shell);
}

static void shell_end_handler(struct shell *shell)
{
    if (shell->char_cnt == 0)
        return;

    shell->cursor_pos = shell->char_cnt;
    shell_refresh_line(shell);
}

static void shell_delete_handler(struct shell *shell)
{
    if (shell->char_cnt == 0 || shell->cursor_pos == shell->char_cnt)
        return;

    shell_reset_autocomplete(shell);
    shell_reset_history_scrolling(shell);
    shell_remove_char(shell, shell->cursor_pos + 1);
    shell->cursor_pos++;
    shell_refresh_line(shell);
}

static void shell_backspace_handler(struct shell *shell)
{
    if (shell->char_cnt == 0 || shell->cursor_pos == 0)
        return;

    shell_reset_autocomplete(shell);
    shell_reset_history_scrolling(shell);
    shell_remove_char(shell, shell->cursor_pos);
    shell_refresh_line(shell);
}

void shell_listen(struct shell *shell)
{
    shell_puts(shell->prompt);

    int c;
    char seq[2];
    while (1) {
        c = shell_getc();

        switch (c) {
        case NULL_CH:
            break;
        case CTRL_A:
            shell_ctrl_a_handler(shell);
            break;
        case CTRL_B:
            shell_left_arrow_handler(shell);
            break;
        case CTRL_C:
            shell_ctrl_c_handler(shell);
            break;
        case CTRL_D:
            break;
        case CTRL_E:
            shell_ctrl_e_handler(shell);
            break;
        case CTRL_F:
            shell_right_arrow_handler(shell);
            break;
        case CTRL_G:
            break;
        case CTRL_H:
            break;
        case TAB:
            shell_autocomplete(shell);
            break;
        case CTRL_J:
            break;
        case ENTER:
            shell_reset_history_scrolling(shell);
            shell_reset_autocomplete(shell);
            if (shell->char_cnt > 0) {
                shell_puts("\n\r");
                shell_reset_line(shell);
                shell_push_new_history(shell, shell->buf);
                return;
            } else {
                shell_puts("\n\r");
                shell_puts(shell->prompt);
            }
            break;
        case CTRL_K:
            break;
        case CTRL_L:
            break;
        case CTRL_N:
            break;
        case CTRL_O:
            break;
        case CTRL_P:
            break;
        case CTRL_Q:
            break;
        case CTRL_R:
            break;
        case CTRL_S:
            break;
        case CTRL_T:
            break;
        case CTRL_U:
            shell_ctrl_u_handler(shell);
            break;
        case CTRL_W:
            break;
        case CTRL_X:
            break;
        case CTRL_Y:
            break;
        case CTRL_Z:
            break;
        case ESC_SEQ1:
            seq[0] = shell_getc();
            seq[1] = shell_getc();
            if (seq[0] == ESC_SEQ2) {
                if (seq[1] == UP_ARROW) {
                    shell_up_arrow_handler(shell);
                    break;
                } else if (seq[1] == DOWN_ARROW) {
                    shell_down_arrow_handler(shell);
                    break;
                } else if (seq[1] == RIGHT_ARROW) {
                    shell_right_arrow_handler(shell);
                    break;
                } else if (seq[1] == LEFT_ARROW) {
                    shell_left_arrow_handler(shell);
                    break;
                } else if (seq[1] == HOME_XTERM) {
                    shell_home_handler(shell);
                    break;
                } else if (seq[1] == HOME_VT100) {
                    shell_home_handler(shell);
                    shell_getc();
                    break;
                } else if (seq[1] == END_XTERM) {
                    shell_end_handler(shell);
                    break;
                } else if (seq[1] == END_VT100) {
                    shell_end_handler(shell);
                    shell_getc();
                    break;
                } else if (seq[1] == DELETE && shell_getc() == ESC_SEQ4) {
                    shell_delete_handler(shell);
                    break;
                }
            }
            break;
        case BACKSPACE:
            shell_backspace_handler(shell);
            break;
        case SPACE:
        default: {
            if (shell->char_cnt != (LINE_MAX - 1)) {
                shell_reset_autocomplete(shell);
                shell_reset_history_scrolling(shell);
                shell_insert_char(shell, c);
                shell_refresh_line(shell);
            }
            break;
        }
        }
    }
}

static int shell_split_cmd_token(char *cmd, char *argv[])
{
    int i = 0;
    int len = strlen(cmd);

    /* Skip spaces before the first argument */
    while (i < len && cmd[i] == ' ') {
        i++;
    }

    /* Get the first argument */
    argv[0] = &cmd[i];
    int argc = 1;

    for (; i < len; i++) {
        if (cmd[i] == ' ') {
            /* Split the token by inserting a null character */
            cmd[i] = '\0';
            i++;

            /* Skip repeated spaces */
            while (cmd[i] == ' ')
                i++;

            /* No more characters to read */
            if (i == len)
                break;

            /* Check the count of the argument */
            if (argc <= (SHELL_ARG_CNT - 1)) {
                argv[argc] = &cmd[i];  // record the start address for the token
                argc++;
            }
        }
    }

    return argc;
}

void shell_execute(struct shell *shell)
{
    if (shell->shell_cmds == NULL) {
        return;
    }

    char *argv[SHELL_ARG_CNT] = {0};
    int argc = shell_split_cmd_token(shell->buf, argv);

    int i;
    for (i = 0; i < shell->cmd_cnt; i++) {
        if (strcmp(argv[0], shell->shell_cmds[i].name) == 0) {
            shell->shell_cmds[i].handler(argc, argv);
            shell->buf[0] = '\0';
            shell_reset_line(shell);
            return;
        }
    }

    shell_unknown_cmd_handler(argc, argv);
    shell->buf[0] = '\0';
    shell_reset_line(shell);
}
