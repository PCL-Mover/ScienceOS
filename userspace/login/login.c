#include "tpos.h"

#define LOGIN_LINE_MAX 96
#define LOGIN_FILE_MAX 1024

static char passwd_db[LOGIN_FILE_MAX];
static char shadow_db[LOGIN_FILE_MAX];

static int str_eq(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static uint64_t read_whole_file(const char *path, char *buffer, uint64_t capacity) {
    uint64_t fd = tpos_open(path);
    uint64_t total = 0;

    if (fd == (uint64_t)-1) {
        return 0;
    }
    while (total + 1 < capacity) {
        uint64_t got = tpos_read(fd, buffer + total, capacity - 1 - total);
        if (got == 0 || got == (uint64_t)-1) {
            break;
        }
        total += got;
    }
    tpos_close(fd);
    buffer[total] = '\0';
    return total;
}

static void read_line(char *buffer, uint64_t capacity) {
    uint64_t length = 0;

    while (length + 1 < capacity) {
        char c = '\0';
        if (tpos_read(STDIN_FD, &c, 1) != 1) {
            break;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        buffer[length++] = c;
    }
    buffer[length] = '\0';
}

/* Copies one ':' (or newline) separated field into out. Returns advanced cursor. */
static const char *next_field(const char *cursor, char *out, uint64_t capacity) {
    uint64_t length = 0;

    while (*cursor != '\0' && *cursor != ':' && *cursor != '\n') {
        if (length + 1 < capacity) {
            out[length++] = *cursor;
        }
        cursor++;
    }
    out[length] = '\0';
    /* Only consume the ':' separator so callers can detect the record end. */
    if (*cursor == ':') {
        cursor++;
    }
    return cursor;
}

static uint64_t parse_u64(const char *text) {
    uint64_t value = 0;

    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (uint64_t)(*text - '0');
        text++;
    }
    return value;
}

static int lookup_user(const char *name, uint64_t *uid, uint64_t *gid) {
    const char *cursor = passwd_db;

    while (*cursor != '\0') {
        char field_name[LOGIN_LINE_MAX];
        char field_uid[24];
        char field_gid[24];

        cursor = next_field(cursor, field_name, sizeof(field_name));
        cursor = next_field(cursor, field_uid, sizeof(field_uid));
        cursor = next_field(cursor, field_gid, sizeof(field_gid));
        while (*cursor != '\0' && *cursor != '\n') {
            char skip[LOGIN_LINE_MAX];
            cursor = next_field(cursor, skip, sizeof(skip));
        }
        if (*cursor == '\n') {
            cursor++;
        }

        if (!str_eq(field_name, name)) {
            continue;
        }
        *uid = parse_u64(field_uid);
        *gid = parse_u64(field_gid);
        return 0;
    }

    return -1;
}

static int lookup_shadow(const char *name, char *salt, uint64_t salt_cap, char *hash) {
    const char *cursor = shadow_db;

    while (*cursor != '\0') {
        char field_name[LOGIN_LINE_MAX];

        cursor = next_field(cursor, field_name, sizeof(field_name));
        cursor = next_field(cursor, salt, salt_cap);
        cursor = next_field(cursor, hash, 65);
        while (*cursor != '\0' && *cursor != '\n') {
            char skip[LOGIN_LINE_MAX];
            cursor = next_field(cursor, skip, sizeof(skip));
        }
        if (*cursor == '\n') {
            cursor++;
        }

        if (str_eq(field_name, name)) {
            return 0;
        }
    }

    return -1;
}

__attribute__((noreturn))
void _start(void) {
    uint64_t tty = tpos_open("/dev/tty");

    if (tty == (uint64_t)-1 ||
        tpos_dup2(tty, STDIN_FD) != STDIN_FD ||
        tpos_dup2(tty, STDOUT_FD) != STDOUT_FD ||
        tpos_dup2(tty, STDERR_FD) != STDERR_FD) {
        tpos_exit(1);
    }

    read_whole_file("/etc/passwd", passwd_db, sizeof(passwd_db));
    read_whole_file("/etc/shadow", shadow_db, sizeof(shadow_db));

    for (;;) {
        char name[LOGIN_LINE_MAX];
        char password[LOGIN_LINE_MAX];
        char salt[32];
        char stored[65];
        char computed[65];
        uint64_t uid = 0;
        uint64_t gid = 0;

        tpos_write_cstr("\nx86 login: ");
        read_line(name, sizeof(name));
        if (name[0] == '\0') {
            continue;
        }

        if (lookup_user(name, &uid, &gid) != 0) {
            tpos_write_cstr("Login incorrect\n");
            continue;
        }

        tpos_write_cstr("Password: ");
        tpos_tty_set_echo(0);
        read_line(password, sizeof(password));
        tpos_tty_set_echo(1);
        tpos_write_cstr("\n");

        if (lookup_shadow(name, salt, sizeof(salt), stored) != 0) {
            tpos_write_cstr("Login incorrect\n");
            continue;
        }
        tpos_sha256_salted_hex(salt, password, computed);
        if (!str_eq(computed, stored)) {
            tpos_write_cstr("Login incorrect\n");
            continue;
        }

        if (tpos_setgid(gid) != 0 || tpos_setuid(uid) != 0) {
            tpos_write_cstr("login: cannot switch credentials\n");
            tpos_exit(2);
        }

        tpos_write_cstr("Welcome to TangPingOS\n");
        if (tpos_spawn("/bin/shell.elf", "") != 0) {
            tpos_write_cstr("login: failed to start shell\n");
            tpos_exit(3);
        }
        tpos_exit(0);
    }
}
