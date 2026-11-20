#include <console.h>
#include <input.h>
#include <scheduler.h>
#include <stdint.h>
#include <tty.h>

static int strings_equal(const char *a, const char *b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
}

int tty_is_path(const char *path) {
    return strings_equal(path, "/dev/tty");
}

#define TTY_LINE_MAX 128

static char tty_line[TTY_LINE_MAX];
static uint64_t tty_line_len;
static int tty_echo_enabled = 1;

void tty_set_echo(int enabled) {
    tty_echo_enabled = enabled ? 1 : 0;
}

static void tty_echo_char(char c) {
    if (!tty_echo_enabled) {
        return;
    }
    char s[2] = {c, '\0'};
    console_write(s);
}

/*
 * Canonical line discipline: keystrokes are buffered and only handed to the
 * reader once Enter is pressed.  Backspace edits the buffered line instead of
 * blindly erasing console cells, so the shell prompt can no longer be deleted.
 */
void tty_on_key(char c) {
    if (c == '\r') {
        c = '\n';
    }

    if (c == '\b' || c == 0x7f) {
        if (tty_line_len == 0) {
            return;
        }
        tty_line_len--;
        if (tty_echo_enabled) {
            console_write("\b \b");
        }
        return;
    }

    if (c == '\n') {
        console_write("\n");
        for (uint64_t i = 0; i < tty_line_len; i++) {
            input_push_key(tty_line[i]);
        }
        input_push_key('\n');
        tty_line_len = 0;
        scheduler_wake_input_waiters();
        return;
    }

    if (tty_line_len >= TTY_LINE_MAX) {
        return;
    }

    tty_line[tty_line_len++] = c;
    tty_echo_char(c);
}

uint64_t tty_read(char *buffer, uint64_t len) {
    return input_read(buffer, len);
}

uint64_t tty_write(const char *buffer, uint64_t len) {
    console_write_n(buffer, len);
    return len;
}
