#include "shell.h"
#include "keyboard.h"
#include "text.h"
#include "fs.h"
#include "process.h"
#include <stdint.h>
#include <stddef.h>

extern uint32_t *g_fb_ptr;
extern uint64_t  g_fb_pitch;
extern uint64_t  g_fb_width;
extern uint64_t  g_fb_height;
extern void test_process_1(void);
extern void test_process_2(void);

#define CMD_BUFFER_SIZE 128
#define MAX_OUTPUT_LINES 18
#define LINE_HEIGHT 10

#define OUTPUT_TOP_Y 30
#define PROMPT_Y (OUTPUT_TOP_Y + (MAX_OUTPUT_LINES * LINE_HEIGHT) + 10)
#define PROMPT_X 10

static char output_lines[MAX_OUTPUT_LINES][CMD_BUFFER_SIZE];
static int output_count = 0;
static char writing_file[32] = {0};
static int writing_mode = 0;

static void clear_screen_region(uint32_t y_start, uint32_t y_end) {
    for (uint32_t row = y_start; row < y_end; row++) {
        for (uint32_t col = 0; col < g_fb_width; col++) {
            g_fb_ptr[row * (g_fb_pitch / 4) + col] = 0x00000000;
        }
    }
}

static void str_copy(char *dest, const char *src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Adds a new line of output, scrolling older lines up if the buffer is full.
static void output_print(const char *line) {
    if (output_count < MAX_OUTPUT_LINES) {
        str_copy(output_lines[output_count], line, CMD_BUFFER_SIZE);
        output_count++;
    } else {
        // Scroll: drop the oldest line, shift everything up
        for (int i = 1; i < MAX_OUTPUT_LINES; i++) {
            str_copy(output_lines[i - 1], output_lines[i], CMD_BUFFER_SIZE);
        }
        str_copy(output_lines[MAX_OUTPUT_LINES - 1], line, CMD_BUFFER_SIZE);
    }
}

// Redraws the entire output region from the current buffer state.
static void redraw_output(void) {
    clear_screen_region(OUTPUT_TOP_Y, PROMPT_Y - 5);
    for (int i = 0; i < output_count; i++) {
        draw_string(output_lines[i], PROMPT_X, OUTPUT_TOP_Y + (i * LINE_HEIGHT), 0x00CCCCCC);
    }
}

static void redraw_prompt(const char *cmd_buffer) {
    clear_screen_region(PROMPT_Y, PROMPT_Y + LINE_HEIGHT);
    if (writing_mode) {
        draw_string("text: ", PROMPT_X, PROMPT_Y, 0x00FFFF00);
        draw_string(cmd_buffer, PROMPT_X + 48, PROMPT_Y, 0x00FFFFFF);
    } else {
        draw_string(": ", PROMPT_X, PROMPT_Y, 0x0000FF00);
        draw_string(cmd_buffer, PROMPT_X + 18, PROMPT_Y, 0x00FFFFFF);
    }
}

static int str_equal(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}
static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str != *prefix) return 0;
        str++; prefix++;
    }
    return 1;
}

static void execute_command(const char *cmd) {
    if (writing_mode) {
        if (cmd[0] == '\0') {
            output_print("cancelled");
        } else {
            uint32_t len = 0;
            while (cmd[len]) len++;

            fs_create_file(writing_file);
            int res = fs_write_file(writing_file, cmd, len);
            if (res == 0) {
                output_print("file saved");
            } else {
                output_print("write failed");
            }
        }
        writing_mode = 0;
        writing_file[0] = '\0';
        return;
    }

    if (str_starts_with(cmd, "write ")) {
        const char *p = cmd + 6;
        while (*p == ' ') p++;
        if (*p == '\0') {
            output_print("usage: write <filename>");
            return;
        }
        int fi = 0;
        while (*p && *p != ' ' && fi < 27) {
            writing_file[fi++] = *p++;
        }
        writing_file[fi] = '\0';
        writing_mode = 1;
        output_print("enter text and press Enter:");
    } else if (str_starts_with(cmd, "cat ")) {
        const char *filename = cmd + 4; // skip "cat "
        char file_buf[256] = {0};
        int bytes_read = fs_read_file(filename, file_buf, sizeof(file_buf) - 1);

        if (bytes_read < 0) {
            output_print("file not found");
            } else {
            file_buf[bytes_read] = '\0';
                output_print(file_buf); // NOTE: assumes content has no newlines for now
        }
    } else if (str_equal(cmd, "ls")) {
        struct file_entry entries[MAX_FILES];
        int count = fs_list_files(entries, MAX_FILES);
        if (count == 0) {
            output_print("(no files)");
        } else {
            for (int i = 0; i < count; i++) {
                output_print(entries[i].filename);
            }
        }
    } else if (str_equal(cmd, "ps")) {
    struct process *proc = process_get_list();
    while (proc != NULL) {
        char line[32] = "PID: ";
        char num[12];
        int n = 0;
        uint64_t pid = proc->pid;
        if (pid == 0) {
            num[n++] = '0';
        } else {
            char temp[12]; int t = 0;
            while (pid > 0) { temp[t++] = '0' + (pid % 10); pid /= 10; }
            while (t > 0) num[n++] = temp[--t];
        }
        num[n] = '\0';

        int i = 5;
        int j = 0;
        while (num[j]) line[i++] = num[j++];
        line[i] = '\0';

        output_print(line);
        proc = proc->next;
    }
    } else if (str_equal(cmd, "capdemo")) {
        struct process *p1 = process_create(test_process_1);
        process_create(test_process_2);
        process_grant_capability(p1, CAP_DRAW_REGION, 50, 70, 300, 320);
        output_print("Capability demo started: P1 drawing, P2 denied");
    } else if (str_equal(cmd, "clear")) {
            output_count = 0;
    } else if (str_equal(cmd, "help")) {
        output_print("commands: ls, cat <f>, write <f>, ps, clear, help, capdemo");
    } else if (cmd[0] == '\0') {
        // empty command, do nothing
    } else {
        output_print("unknown command");
    }
}

void shell_run(void) {
    char cmd_buffer[CMD_BUFFER_SIZE];
    int cmd_len = 0;
    cmd_buffer[0] = '\0';

    output_print("Infinity shell - type 'help' for commands");
    redraw_output();
    redraw_prompt(cmd_buffer);

    for (;;) {
        char c = keyboard_read_char();
        if (c == 0) {
            continue;
        }

        if (c == '\n') {
            cmd_buffer[cmd_len] = '\0';
            execute_command(cmd_buffer);
            redraw_output();

            cmd_len = 0;
            cmd_buffer[0] = '\0';
            redraw_prompt(cmd_buffer);
        } else if (c == '\b') {
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buffer[cmd_len] = '\0';
                redraw_prompt(cmd_buffer);
            }
        } else if (cmd_len < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_len] = c;
            cmd_len++;
            cmd_buffer[cmd_len] = '\0';
            redraw_prompt(cmd_buffer);
        }
    }
}
