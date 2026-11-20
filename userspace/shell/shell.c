#include "tpos.h"

#define SHELL_LINE_MAX 128
#define SHELL_PATH_MAX 96
#define USB_TEST_PARTITION_BLOCKS 22528
#define USB_TEST_SCRATCH_LBA (USB_TEST_PARTITION_BLOCKS - 1)

#define sys_write tpos_write
#define sys_getpid tpos_getpid
#define sys_geteuid tpos_geteuid
#define sys_getegid tpos_getegid
#define sys_setuid tpos_setuid
#define sys_setgid tpos_setgid
#define sys_tty_set_echo tpos_tty_set_echo
#define sys_yield tpos_yield
#define sys_brk tpos_brk
#define sys_open_flags tpos_open_flags
#define sys_open tpos_open
#define sys_read tpos_read
#define sys_close tpos_close
#define sys_getdents tpos_getdents
#define sys_write_file tpos_write_file
#define sys_dup2 tpos_dup2
#define sys_lseek tpos_lseek
#define sys_unlink tpos_unlink
#define sys_spawn tpos_spawn
#define sys_task_info tpos_task_info
#define sys_meminfo tpos_meminfo
#define sys_system_info tpos_system_info
#define sys_uptime tpos_uptime
#define sys_block_info tpos_block_info
#define sys_block_read tpos_block_read
#define sys_block_write tpos_block_write
#define sys_mount_info tpos_mount_info
#define sys_blockfs_unlock tpos_blockfs_unlock
#define sys_sbrk tpos_sbrk
#define sys_exit tpos_exit
#define u64_to_decimal tpos_u64_to_decimal
#define u64_to_hex tpos_u64_to_hex
#define copy_string tpos_strcpy
#define write_literal tpos_write_literal
#define string_length tpos_strlen
#define write_cstr tpos_write_cstr
#define write_cstr_limited tpos_write_cstr_limited
#define write_u64_decimal tpos_write_u64_decimal
#define write_u64_hex tpos_write_u64_hex
#define write_hex_byte tpos_write_hex_byte

static void cmd_usbdiag(void);

static void write_dirent_name(const struct dirent *dirent) {
    uint64_t len = 0;
    while (len < DIRENT_NAME_MAX && dirent->name[len] != '\0') {
        len++;
    }
    sys_write(STDOUT_FD, dirent->name, len);
}

static int cmd_ls(const char *path) {
    static const char prefix[] = "$ ls ";
    static const char newline[] = "\n";
    static const char entry_prefix[] = "  ";
    static const char fail[] = "ls: failed\n";
    struct dirent dirent;

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents(path, i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            return -1;
        }

        write_literal(entry_prefix, sizeof(entry_prefix) - 1);
        write_dirent_name(&dirent);
        write_literal(newline, sizeof(newline) - 1);
    }

    return 0;
}

static int cmd_cat(const char *path, char *buffer, uint64_t buffer_len) {
    static const char prefix[] = "$ cat ";
    static const char newline[] = "\n";
    static const char open_fail[] = "cat: open failed\n";
    static const char read_fail[] = "cat: read failed\n";

    write_literal(prefix, sizeof(prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);

    uint64_t fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(open_fail, sizeof(open_fail) - 1);
        return -1;
    }

    for (;;) {
        uint64_t read_len = sys_read(fd, buffer, buffer_len);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            write_literal(read_fail, sizeof(read_fail) - 1);
            if (path[0] == '/' && path[1] == 'u' && path[2] == 's' &&
                path[3] == 'b' && path[4] == 'd' && path[5] == 'i' &&
                path[6] == 's' && path[7] == 'k' &&
                (path[8] == '/' || path[8] == '\0')) {
                cmd_usbdiag();
            }
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        sys_write(STDOUT_FD, buffer, read_len);
        if (read_len < buffer_len) {
            break;
        }
    }

    sys_close(fd);
    write_literal(newline, sizeof(newline) - 1);
    return 0;
}

static void run_startup_self_test(char *heap) {
    static const char heap_message[] = "shell.elf: heap buffer works\n";
    static const char usercopy_ok[] = "shell.elf: bad user pointer rejected\n";
    static const char usercopy_fail[] = "shell.elf: bad user pointer was accepted\n";

    uint64_t heap_len = copy_string(heap, heap_message);
    sys_write(STDOUT_FD, heap, heap_len);

    uint64_t fd = sys_open("/hello.txt");
    if (fd == (uint64_t)-1) {
        sys_exit(10);
    }

    uint64_t bad_read = sys_read(fd, (void *)0xffff800000000000ULL, 1);
    if (bad_read == (uint64_t)-1) {
        write_literal(usercopy_ok, sizeof(usercopy_ok) - 1);
    } else {
        write_literal(usercopy_fail, sizeof(usercopy_fail) - 1);
        sys_exit(9);
    }

    sys_close(fd);
}

static int strings_equal(const char *a, const char *b) {
    uint64_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *prefix) {
    uint64_t i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static int contains_text(const char *s, const char *needle) {
    uint64_t needle_len = string_length(needle);
    uint64_t i = 0;

    if (needle_len == 0) {
        return 1;
    }
    while (s[i] != '\0') {
        uint64_t j = 0;
        while (needle[j] != '\0' && s[i + j] == needle[j]) {
            j++;
        }
        if (j == needle_len) {
            return 1;
        }
        i++;
    }
    return 0;
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ') {
        s++;
    }
    return s;
}

static const char *find_space(const char *s) {
    while (*s != '\0' && *s != ' ') {
        s++;
    }
    return s;
}

static int parse_u64(const char *s, uint64_t *out) {
    uint64_t value = 0;
    uint64_t i = 0;

    if (s[0] == '\0') {
        return -1;
    }

    while (s[i] != '\0') {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        value = value * 10 + (uint64_t)(s[i] - '0');
        i++;
    }

    *out = value;
    return 0;
}

static uint64_t copy_token(char *dst, uint64_t dst_len, const char *start, const char *end) {
    uint64_t len = 0;
    while (start + len < end && len + 1 < dst_len) {
        dst[len] = start[len];
        len++;
    }
    dst[len] = '\0';
    return len;
}

static uint64_t read_token(char *dst, uint64_t dst_len, const char *src, const char **rest) {
    uint64_t len = 0;
    char quote = 0;
    int closed = 0;

    if (dst_len == 0) {
        return 0;
    }
    src = skip_spaces(src);
    if (src[0] == '"') {
        quote = '"';
        src++;
    }
    while (src[0] != '\0') {
        if (quote != 0) {
            if (src[0] == quote) {
                src++;
                closed = 1;
                break;
            }
        } else if (src[0] == ' ') {
            break;
        }
        if (len + 1 < dst_len) {
            dst[len++] = src[0];
        }
        src++;
    }
    if (quote != 0 && !closed) {
        dst[0] = '\0';
        return 0;
    }
    dst[len] = '\0';
    if (rest != 0) {
        *rest = skip_spaces(src);
    }
    return len;
}

static int copy_cstr_limited(char *dst, uint64_t dst_len, const char *src) {
    uint64_t i = 0;
    if (dst_len == 0) {
        return -1;
    }
    while (src[i] != '\0' && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    if (src[i] != '\0') {
        dst[0] = '\0';
        return -1;
    }
    dst[i] = '\0';
    return 0;
}

static uint64_t path_length(const char *path) {
    return string_length(path);
}

static void path_pop_component(char *path) {
    uint64_t len = path_length(path);
    if (len <= 1) {
        path[0] = '/';
        path[1] = '\0';
        return;
    }

    while (len > 1 && path[len - 1] == '/') {
        path[--len] = '\0';
    }
    while (len > 1 && path[len - 1] != '/') {
        path[--len] = '\0';
    }
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
}

static int path_append_component(char *path, uint64_t path_len,
                                 const char *component, uint64_t component_len) {
    uint64_t len = path_length(path);
    uint64_t need_slash = len > 1 ? 1 : 0;

    if (component_len == 0 || len + need_slash + component_len >= path_len) {
        return -1;
    }
    if (need_slash) {
        path[len++] = '/';
    }
    for (uint64_t i = 0; i < component_len; i++) {
        path[len++] = component[i];
    }
    path[len] = '\0';
    return 0;
}

static int resolve_path(const char *cwd, const char *input, char *out, uint64_t out_len) {
    const char *p = input;

    if (out_len < 2) {
        return -1;
    }

    if (p[0] == '/') {
        out[0] = '/';
        out[1] = '\0';
        while (*p == '/') {
            p++;
        }
    } else {
        if (copy_cstr_limited(out, out_len, cwd) != 0) {
            return -1;
        }
    }

    while (*p != '\0') {
        const char *start;
        uint64_t len = 0;

        while (*p == '/') {
            p++;
        }
        start = p;
        while (p[len] != '\0' && p[len] != '/') {
            len++;
        }

        if (len == 0) {
            break;
        }
        if (len == 1 && start[0] == '.') {
            /* no-op */
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            path_pop_component(out);
        } else if (path_append_component(out, out_len, start, len) != 0) {
            return -1;
        }

        p = start + len;
    }

    return 0;
}

static int split_parent_base(const char *path, char *parent, uint64_t parent_len,
                             char *base, uint64_t base_len) {
    uint64_t len = path_length(path);
    uint64_t slash = 0;

    if (len <= 1) {
        return -1;
    }

    for (uint64_t i = 1; i < len; i++) {
        if (path[i] == '/') {
            slash = i;
        }
    }

    if (slash == 0) {
        if (copy_cstr_limited(parent, parent_len, "/") != 0 ||
            copy_cstr_limited(base, base_len, path + 1) != 0) {
            return -1;
        }
        return 0;
    }

    if (slash + 1 >= len || slash + 1 >= parent_len) {
        return -1;
    }
    for (uint64_t i = 0; i < slash; i++) {
        parent[i] = path[i];
    }
    parent[slash] = '\0';
    return copy_cstr_limited(base, base_len, path + slash + 1);
}

static int find_dirent_for_path(const char *path, struct dirent *out) {
    char parent[SHELL_PATH_MAX];
    char base[DIRENT_NAME_MAX];
    struct dirent dirent;

    if (strings_equal(path, "/")) {
        copy_cstr_limited(out->name, sizeof(out->name), "/");
        out->type = DIRENT_TYPE_DIR;
        out->size = 0;
        return 0;
    }

    if (split_parent_base(path, parent, sizeof(parent), base, sizeof(base)) != 0) {
        return -1;
    }

    for (uint64_t i = 0; i < 32; i++) {
        uint64_t result = sys_getdents(parent, i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            return -1;
        }
        if (strings_equal(dirent.name, base)) {
            *out = dirent;
            return 0;
        }
    }

    return -1;
}

static int path_is_dir(const char *path) {
    struct dirent dirent;
    if (strings_equal(path, "/")) {
        return 1;
    }
    if (find_dirent_for_path(path, &dirent) != 0) {
        return 0;
    }
    return dirent.type == DIRENT_TYPE_DIR;
}

static int write_file_text(const char *path, const char *text, uint64_t flags);

static void run_dup2_stdin_self_test(char *buffer, uint64_t buffer_len) {
    static const char dup2_ok[] = "shell.elf: dup2(file, stdin) ok\n";
    static const char dup2_offset_ok[] = "shell.elf: dup2 shared offset ok\n";
    static const char dup2_fail[] = "shell.elf: dup2(file, stdin) failed\n";

    if (buffer_len < 6) {
        sys_exit(14);
    }

    uint64_t fd = sys_open("/hello.txt");
    if (fd == (uint64_t)-1) {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_exit(14);
    }
    if (sys_dup2(fd, STDIN_FD) != STDIN_FD) {
        sys_close(fd);
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_exit(14);
    }

    uint64_t read_len = sys_read(STDIN_FD, buffer, 5);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == 5 && starts_with(buffer, "Hello")) {
        write_literal(dup2_ok, sizeof(dup2_ok) - 1);
    } else {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_close(fd);
        sys_exit(14);
    }

    read_len = sys_read(fd, buffer, 1);
    if (read_len == 1 && buffer[0] == ' ') {
        write_literal(dup2_offset_ok, sizeof(dup2_offset_ok) - 1);
    } else {
        write_literal(dup2_fail, sizeof(dup2_fail) - 1);
        sys_close(fd);
        sys_exit(14);
    }

    sys_close(fd);
}

static void run_ramfs_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: ramfs open/write/read ok\n";
    static const char fd_ok[] = "shell.elf: ramfs append ok\n";
    static const char fail[] = "shell.elf: ramfs write/read failed\n";
    static const char path[] = "/ram-note.txt";
    static const char content[] = "TangPingOS ramfs is writable.\n";
    static const char append[] = "fd write works.\n";

    uint64_t len = sizeof(content) - 1;
    uint64_t append_len = sizeof(append) - 1;
    if (buffer_len < len + append_len + 1) {
        sys_exit(16);
    }
    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    if (sys_write(fd, content, len) != len) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    sys_close(fd);

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == len && starts_with(buffer, content)) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }

    fd = sys_open_flags(path, OPEN_APPEND);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    if (sys_write(fd, append, append_len) != append_len) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    sys_close(fd);

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(16);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len == len + append_len && starts_with(buffer, content)) {
        write_literal(fd_ok, sizeof(fd_ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(16);
}

static void run_lseek_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: lseek ok\n";
    static const char fail[] = "shell.elf: lseek failed\n";
    static const char path[] = "/seek-note.txt";
    static const char initial[] = "abcdef";
    static const char patch[] = "XY";
    static const char expected[] = "abXYef";
    static const char tail[] = "ef";

    if (buffer_len < sizeof(expected)) {
        sys_exit(17);
    }

    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_write(fd, initial, sizeof(initial) - 1) != sizeof(initial) - 1) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_lseek(fd, 2, SEEK_SET) != 2) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_write(fd, patch, sizeof(patch) - 1) != sizeof(patch) - 1) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    if (sys_lseek(fd, 0, SEEK_SET) != 0) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len != sizeof(expected) - 1 || !starts_with(buffer, expected)) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }

    if (sys_lseek(fd, -2, SEEK_END) != sizeof(expected) - 1 - 2) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(17);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    sys_close(fd);
    if (read_len == sizeof(tail) - 1 && starts_with(buffer, tail)) {
        write_literal(ok, sizeof(ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(17);
}

static void run_unlink_self_test(void) {
    static const char ok[] = "shell.elf: unlink ok\n";
    static const char fail[] = "shell.elf: unlink failed\n";
    static const char path[] = "/delete-me.txt";
    static const char content[] = "temporary";

    if (write_file_text(path, content, OPEN_CREATE | OPEN_TRUNC) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(19);
    }
    if (sys_unlink(path) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(19);
    }
    if (sys_open(path) == (uint64_t)-1) {
        write_literal(ok, sizeof(ok) - 1);
        return;
    }

    write_literal(fail, sizeof(fail) - 1);
    sys_exit(19);
}

static int read_file_equals(const char *path, char *buffer, uint64_t buffer_len,
                            const char *expected) {
    uint64_t fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        return 0;
    }

    uint64_t read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len == (uint64_t)-1 || read_len + 1 > buffer_len) {
        return 0;
    }
    buffer[read_len] = '\0';

    return read_len == string_length(expected) && strings_equal(buffer, expected);
}

static int read_file_matches_segments(const char *path, char *buffer, uint64_t buffer_len,
                                      const char *prefix, char first_fill,
                                      uint64_t first_fill_len, const char *middle,
                                      char second_fill, uint64_t second_fill_len,
                                      const char *suffix) {
    uint64_t fd = sys_open(path);
    uint64_t read_len = 0;
    uint64_t pos = 0;
    uint64_t prefix_len = string_length(prefix);
    uint64_t middle_len = string_length(middle);
    uint64_t suffix_len = string_length(suffix);
    uint64_t expected_len = prefix_len + first_fill_len + middle_len +
                            second_fill_len + suffix_len;

    if (fd == (uint64_t)-1 || buffer_len < expected_len) {
        if (fd != (uint64_t)-1) {
            sys_close(fd);
        }
        return 0;
    }

    while (read_len < expected_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, expected_len - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            return 0;
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);
    if (read_len != expected_len) {
        return 0;
    }

    for (uint64_t i = 0; i < prefix_len; i++) {
        if (buffer[pos++] != prefix[i]) {
            return 0;
        }
    }
    for (uint64_t i = 0; i < first_fill_len; i++) {
        if (buffer[pos++] != first_fill) {
            return 0;
        }
    }
    for (uint64_t i = 0; i < middle_len; i++) {
        if (buffer[pos++] != middle[i]) {
            return 0;
        }
    }
    for (uint64_t i = 0; i < second_fill_len; i++) {
        if (buffer[pos++] != second_fill) {
            return 0;
        }
    }
    for (uint64_t i = 0; i < suffix_len; i++) {
        if (buffer[pos++] != suffix[i]) {
            return 0;
        }
    }

    return 1;
}

static void read_line(uint64_t input_fd, char *line, uint64_t max_len) {
    uint64_t len = 0;

    for (;;) {
        char c;
        uint64_t read = sys_read(input_fd, &c, 1);
        if (read == 0) {
            sys_yield();
            continue;
        }
        if (read == (uint64_t)-1) {
            line[0] = '\0';
            return;
        }

        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            line[len] = '\0';
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
            }
            continue;
        }
        if (len + 1 < max_len) {
            line[len++] = c;
        }
    }
}

static int write_file_text(const char *path, const char *text, uint64_t flags) {
    uint64_t len = string_length(text);
    uint64_t fd = sys_open_flags(path, flags);
    if (fd == (uint64_t)-1) {
        return -1;
    }
    if (sys_write(fd, text, len) != len) {
        sys_close(fd);
        return -1;
    }
    sys_close(fd);
    return 0;
}

static int copy_file_data(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    uint64_t in_fd = sys_open(src);
    if (in_fd == (uint64_t)-1) {
        return -1;
    }

    uint64_t out_fd = sys_open_flags(dst, OPEN_CREATE | OPEN_TRUNC);
    if (out_fd == (uint64_t)-1) {
        sys_close(in_fd);
        return -1;
    }

    for (;;) {
        uint64_t read_len = sys_read(in_fd, buffer, buffer_len);
        if (read_len == (uint64_t)-1) {
            sys_close(in_fd);
            sys_close(out_fd);
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        if (sys_write(out_fd, buffer, read_len) != read_len) {
            sys_close(in_fd);
            sys_close(out_fd);
            return -1;
        }
    }

    sys_close(in_fd);
    sys_close(out_fd);
    return 0;
}

static void write_dirent_type(uint64_t type) {
    static const char file[] = "file";
    static const char dir[] = "dir";
    static const char device[] = "device";
    static const char unknown[] = "unknown";

    if (type == DIRENT_TYPE_FILE) {
        write_literal(file, sizeof(file) - 1);
    } else if (type == DIRENT_TYPE_DIR) {
        write_literal(dir, sizeof(dir) - 1);
    } else if (type == DIRENT_TYPE_DEVICE) {
        write_literal(device, sizeof(device) - 1);
    } else {
        write_literal(unknown, sizeof(unknown) - 1);
    }
}

static void cmd_stat(const char *path) {
    static const char fail[] = "stat: failed\n";
    static const char path_prefix[] = "path: ";
    static const char type_prefix[] = "type: ";
    static const char size_prefix[] = "size: ";
    static const char newline[] = "\n";
    struct dirent dirent;

    if (find_dirent_for_path(path, &dirent) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal(path_prefix, sizeof(path_prefix) - 1);
    write_cstr(path);
    write_literal(newline, sizeof(newline) - 1);
    write_literal(type_prefix, sizeof(type_prefix) - 1);
    write_dirent_type(dirent.type);
    write_literal(newline, sizeof(newline) - 1);
    write_literal(size_prefix, sizeof(size_prefix) - 1);
    write_u64_decimal(dirent.size);
    write_literal(newline, sizeof(newline) - 1);
}

static void cmd_cp(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    static const char ok[] = "cp: ok\n";
    static const char fail[] = "cp: failed\n";

    if (copy_file_data(src, dst, buffer, buffer_len) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_mv(const char *src, const char *dst, char *buffer, uint64_t buffer_len) {
    static const char ok[] = "mv: ok\n";
    static const char fail[] = "mv: failed\n";

    if (copy_file_data(src, dst, buffer, buffer_len) == 0 && sys_unlink(src) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_hexdump(const char *path, char *buffer, uint64_t buffer_len) {
    static const char fail[] = "hexdump: failed\n";
    uint64_t fd = sys_open(path);
    uint64_t offset = 0;

    if (fd == (uint64_t)-1 || buffer_len < 16) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    for (;;) {
        uint64_t read_len = sys_read(fd, buffer, 16);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            return;
        }
        if (read_len == 0) {
            break;
        }

        write_u64_hex(offset);
        write_literal(": ", 2);
        for (uint64_t i = 0; i < read_len; i++) {
            write_hex_byte((uint8_t)buffer[i]);
            write_literal(" ", 1);
        }
        write_literal("\n", 1);
        offset += read_len;
    }

    sys_close(fd);
}

static void cmd_edit(const char *path, char *buffer, uint64_t buffer_len) {
    static const char start[] = "edit: enter lines, .save to finish\n";
    static const char prompt[] = "> ";
    static const char ok[] = "edit: saved\n";
    static const char fail[] = "edit: failed\n";

    if (buffer_len < SHELL_LINE_MAX) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal(start, sizeof(start) - 1);
    for (;;) {
        write_literal(prompt, sizeof(prompt) - 1);
        read_line(STDIN_FD, buffer, SHELL_LINE_MAX);
        if (strings_equal(buffer, ".save")) {
            sys_close(fd);
            write_literal(ok, sizeof(ok) - 1);
            return;
        }

        uint64_t len = string_length(buffer);
        if (sys_write(fd, buffer, len) != len || sys_write(fd, "\n", 1) != 1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            return;
        }
    }
}

static const char *task_state_name(uint64_t state) {
    switch (state) {
        case 0:
            return "runnable";
        case 1:
            return "sleeping";
        case 2:
            return "waiting-input";
        case 3:
            return "stopped";
        default:
            return "unknown";
    }
}

static void write_pages_as_kib(uint64_t pages, uint64_t page_size) {
    write_u64_decimal((pages * page_size) / 1024);
    write_literal(" KiB", 4);
}

static void cmd_ps(void) {
    static const char header[] = "pid state switches exit name\n";
    static const char fail[] = "ps: failed\n";
    struct task_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_task_info(i, &info);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            return;
        }

        write_u64_decimal(info.pid);
        write_literal(" ", 1);
        write_cstr(task_state_name(info.state));
        write_literal(" ", 1);
        write_u64_decimal(info.switches);
        write_literal(" ", 1);
        write_u64_decimal(info.exit_status);
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal("\n", 1);
    }
}

static void cmd_mem(void) {
    static const char fail[] = "mem: failed\n";
    struct meminfo info;

    if (sys_meminfo(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal("page size: ", 11);
    write_u64_decimal(info.page_size);
    write_literal(" bytes\n", 7);
    write_literal("total: ", 7);
    write_u64_decimal(info.total_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.total_pages, info.page_size);
    write_literal("\nused: ", 7);
    write_u64_decimal(info.used_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.used_pages, info.page_size);
    write_literal("\nfree: ", 7);
    write_u64_decimal(info.free_pages);
    write_literal(" pages, ", 8);
    write_pages_as_kib(info.free_pages, info.page_size);
    write_literal("\n", 1);
}

static void cmd_uptime(void) {
    static struct system_info info;
    uint64_t ticks = sys_uptime();
    uint64_t hz = 100;

    if (sys_system_info(&info) == 0 && info.timer_hz != 0) {
        hz = info.timer_hz;
    }

    write_literal("ticks: ", 7);
    write_u64_decimal(ticks);
    write_literal("\nseconds: ", 10);
    write_u64_decimal(ticks / hz);
    write_literal("\n", 1);
}

static void cmd_sysinfo(void) {
    static const char fail[] = "sysinfo: failed\n";
    static struct system_info info;

    if (sys_system_info(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal("cpu: ", 5);
    write_cstr_limited(info.cpu_name, sizeof(info.cpu_name));
    write_literal("\nframebuffer: ", 14);
    write_u64_decimal(info.framebuffer_width);
    write_literal("x", 1);
    write_u64_decimal(info.framebuffer_height);
    write_literal(" pitch ", 7);
    write_u64_decimal(info.framebuffer_pitch);
    write_literal(" bpp ", 5);
    write_u64_decimal(info.framebuffer_bpp);
    write_literal("\nmemmap entries: ", 17);
    write_u64_decimal(info.memmap_entries);
    write_literal("\ntimer hz: ", 11);
    write_u64_decimal(info.timer_hz);
    write_literal("\nticks: ", 8);
    write_u64_decimal(info.ticks);
    write_literal("\nmemory total/free/used pages: ", 31);
    write_u64_decimal(info.total_pages);
    write_literal("/", 1);
    write_u64_decimal(info.free_pages);
    write_literal("/", 1);
    write_u64_decimal(info.used_pages);
    write_literal("\nxhci controllers: ", 19);
    write_u64_decimal(info.xhci_count);
    if (info.xhci_found) {
        write_literal(" first=", 7);
        write_u64_decimal(info.xhci_bus);
        write_literal(":", 1);
        write_u64_decimal(info.xhci_slot);
        write_literal(".", 1);
        write_u64_decimal(info.xhci_function);
    }
    write_literal("\n", 1);
}

static void cmd_usb(void) {
    static const char fail[] = "usb: failed\n";
    static struct system_info info;

    if (sys_system_info(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }
    write_literal("xhci found: ", 12);
    write_u64_decimal(info.xhci_found);
    write_literal("\ncontrollers: ", 14);
    write_u64_decimal(info.xhci_count);
    if (!info.xhci_found) {
        write_literal("\n", 1);
        return;
    }
    write_literal("\npci: ", 6);
    write_u64_decimal(info.xhci_bus);
    write_literal(":", 1);
    write_u64_decimal(info.xhci_slot);
    write_literal(".", 1);
    write_u64_decimal(info.xhci_function);
    write_literal("\nvendor: ", 9);
    write_u64_hex(info.xhci_vendor_id);
    write_literal("\ndevice: ", 9);
    write_u64_hex(info.xhci_device_id);
    write_literal("\nrevision: ", 11);
    write_u64_hex(info.xhci_revision);
    write_literal("\nirq line: ", 11);
    write_u64_decimal(info.xhci_irq_line);
    write_literal("\nbar0 raw: ", 11);
    write_u64_hex(info.xhci_bar0_raw);
    write_literal("\nmmio base: ", 12);
    write_u64_hex(info.xhci_mmio_base);
    write_literal("\nbar0 64-bit: ", 14);
    write_u64_decimal(info.xhci_bar0_is_64bit);
    write_literal("\nmmio mapped: ", 14);
    write_u64_decimal(info.xhci_mmio_mapped);
    if (info.xhci_mmio_mapped) {
        write_literal("\ncap length: ", 13);
        write_u64_decimal(info.xhci_cap_length);
        write_literal("\nhci version: ", 14);
        write_u64_hex(info.xhci_hci_version);
        write_cstr("\ncontext size: ");
        write_u64_decimal(info.xhci_context_size);
        write_literal("\nmax slots: ", 12);
        write_u64_decimal(info.xhci_max_slots);
        write_literal("\nmax interrupters: ", 19);
        write_u64_decimal(info.xhci_max_interrupters);
        write_literal("\nmax ports: ", 12);
        write_u64_decimal(info.xhci_max_ports);
        write_literal("\ndoorbell offset: ", 18);
        write_u64_hex(info.xhci_doorbell_offset);
        write_literal("\nruntime offset: ", 17);
        write_u64_hex(info.xhci_runtime_offset);
        write_cstr("\nop regs ready: ");
        write_u64_decimal(info.xhci_op_regs_ready);
        write_cstr("\nop usbcmd before: ");
        write_u64_hex(info.xhci_op_usbcmd_before);
        write_cstr("\nop usbsts before: ");
        write_u64_hex(info.xhci_op_usbsts_before);
        write_cstr("\nop pagesize: ");
        write_u64_hex(info.xhci_op_pagesize);
        write_cstr("\nreset allowed: ");
        write_u64_decimal(info.xhci_reset_allowed);
        write_cstr("\nreset_policy: ");
        write_u64_decimal(info.xhci_reset_policy);
        write_cstr("\nreal_hw_attempted: ");
        write_u64_decimal(info.xhci_real_hw_attempted);
        write_cstr("\nhandoff_required: ");
        write_u64_decimal(info.xhci_handoff_required);
        write_cstr("\ncontroller_start_result: ");
        write_u64_decimal(info.xhci_controller_start_result);
        write_cstr("\nreset attempted: ");
        write_u64_decimal(info.xhci_reset_attempted);
        write_cstr("\nreset ok: ");
        write_u64_decimal(info.xhci_reset_ok);
        write_cstr("\nhalt ok: ");
        write_u64_decimal(info.xhci_halt_ok);
        write_cstr("\nready ok: ");
        write_u64_decimal(info.xhci_ready_ok);
        write_cstr("\nop usbcmd after: ");
        write_u64_hex(info.xhci_op_usbcmd_after);
        write_cstr("\nop usbsts after: ");
        write_u64_hex(info.xhci_op_usbsts_after);
        write_cstr("\next cap offset: ");
        write_u64_hex(info.xhci_ext_cap_offset);
        write_cstr("\next cap count: ");
        write_u64_decimal(info.xhci_ext_cap_count);
        write_cstr("\nlegacy cap offset: ");
        write_u64_hex(info.xhci_legacy_cap_offset);
        write_cstr("\nlegacy bios owned: ");
        write_u64_decimal(info.xhci_legacy_bios_owned_before);
        write_cstr(" -> ");
        write_u64_decimal(info.xhci_legacy_bios_owned_after);
        write_cstr("\nlegacy os owned: ");
        write_u64_decimal(info.xhci_legacy_os_owned_before);
        write_cstr(" -> ");
        write_u64_decimal(info.xhci_legacy_os_owned_after);
        write_cstr("\nhandoff attempted: ");
        write_u64_decimal(info.xhci_handoff_attempted);
        write_cstr("\nhandoff ok: ");
        write_u64_decimal(info.xhci_handoff_ok);
        write_cstr("\nrings ready: ");
        write_u64_decimal(info.xhci_rings_ready);
        write_cstr("\ncommand ring: ");
        write_u64_hex(info.xhci_command_ring_phys);
        write_cstr("\nevent ring: ");
        write_u64_hex(info.xhci_event_ring_phys);
        write_cstr("\nerst: ");
        write_u64_hex(info.xhci_erst_phys);
        write_cstr("\ndcbaa: ");
        write_u64_hex(info.xhci_dcbaa_phys);
        write_cstr("\nscratchpads: ");
        write_u64_decimal(info.xhci_scratchpad_count);
        write_cstr("\nscratchpad array: ");
        write_u64_hex(info.xhci_scratchpad_array_phys);
        write_cstr("\ncrcr: ");
        write_u64_hex(info.xhci_crcr_value);
        write_cstr("\ndcbaap: ");
        write_u64_hex(info.xhci_dcbaap_value);
        write_cstr("\nerstsz: ");
        write_u64_decimal(info.xhci_erstsz_value);
        write_cstr("\nerstba: ");
        write_u64_hex(info.xhci_erstba_value);
        write_cstr("\nerdp: ");
        write_u64_hex(info.xhci_erdp_value);
        write_cstr("\ncontroller started: ");
        write_u64_decimal(info.xhci_controller_started);
        write_cstr("\nconfig: ");
        write_u64_hex(info.xhci_config_value);
        write_cstr("\nusbcmd run: ");
        write_u64_hex(info.xhci_usbcmd_run);
        write_cstr("\nusbsts run: ");
        write_u64_hex(info.xhci_usbsts_run);
        write_cstr("\nenable slot attempted: ");
        write_u64_decimal(info.xhci_enable_slot_attempted);
        write_cstr("\nenable slot ok: ");
        write_u64_decimal(info.xhci_enable_slot_ok);
        write_cstr("\nenable slot id: ");
        write_u64_decimal(info.xhci_enable_slot_id);
        write_cstr("\nenable slot cc: ");
        write_u64_decimal(info.xhci_enable_slot_completion_code);
        write_cstr("\nenable slot event type: ");
        write_u64_decimal(info.xhci_enable_slot_event_type);
        write_cstr("\nevent trb: ");
        write_u64_hex(info.xhci_enable_slot_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_enable_slot_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_enable_slot_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_enable_slot_event_trb3);
        write_cstr("\ncommand doorbell: ");
        write_u64_hex(info.xhci_command_doorbell_value);
        write_cstr("\nerdp after command: ");
        write_u64_hex(info.xhci_erdp_after_command);
        write_cstr("\nport scan done: ");
        write_u64_decimal(info.xhci_port_scan_done);
        write_cstr("\nports scanned: ");
        write_u64_decimal(info.xhci_port_scan_limit);
        write_cstr("\nport power attempted: ");
        write_u64_decimal(info.xhci_port_power_attempted);
        write_cstr("\nport power changed: ");
        write_u64_decimal(info.xhci_port_power_changed_count);
        write_cstr("\nport connect rescans: ");
        write_u64_decimal(info.xhci_port_connect_rescan_count);
        write_cstr("\nconnected ports: ");
        write_u64_decimal(info.xhci_connected_port_count);
        write_cstr("\nenabled ports: ");
        write_u64_decimal(info.xhci_enabled_port_count);
        write_cstr("\npowered ports: ");
        write_u64_decimal(info.xhci_powered_port_count);
        write_cstr("\nfirst connected port: ");
        write_u64_decimal(info.xhci_first_connected_port);
        write_cstr("\nfirst portsc: ");
        write_u64_hex(info.xhci_first_connected_portsc);
        write_cstr("\nfirst speed: ");
        write_u64_decimal(info.xhci_first_connected_speed);
        write_cstr("\nfirst enabled: ");
        write_u64_decimal(info.xhci_first_connected_enabled);
        write_cstr("\nfirst powered: ");
        write_u64_decimal(info.xhci_first_connected_powered);
        write_cstr("\nfirst link state: ");
        write_u64_decimal(info.xhci_first_connected_link_state);
        write_cstr("\nsecond connected port: ");
        write_u64_decimal(info.xhci_second_connected_port);
        write_cstr("\nsecond portsc: ");
        write_u64_hex(info.xhci_second_connected_portsc);
        write_cstr("\nsecond speed: ");
        write_u64_decimal(info.xhci_second_connected_speed);
        write_cstr("\nsecond enabled: ");
        write_u64_decimal(info.xhci_second_connected_enabled);
        write_cstr("\nsecond powered: ");
        write_u64_decimal(info.xhci_second_connected_powered);
        write_cstr("\nsecond link state: ");
        write_u64_decimal(info.xhci_second_connected_link_state);
        write_cstr("\nselected connected port: ");
        write_u64_decimal(info.xhci_selected_connected_port);
        write_cstr("\nselected portsc: ");
        write_u64_hex(info.xhci_selected_connected_portsc);
        write_cstr("\nselected speed: ");
        write_u64_decimal(info.xhci_selected_connected_speed);
        write_cstr("\nselected enabled: ");
        write_u64_decimal(info.xhci_selected_connected_enabled);
        write_cstr("\nselected powered: ");
        write_u64_decimal(info.xhci_selected_connected_powered);
        write_cstr("\nselected link state: ");
        write_u64_decimal(info.xhci_selected_connected_link_state);
        write_cstr("\nselected score: ");
        write_u64_decimal(info.xhci_selected_connected_score);
        write_cstr("\nport1 portsc: ");
        write_u64_hex(info.xhci_port1_portsc);
        write_cstr("\nport reset attempted: ");
        write_u64_decimal(info.xhci_port_reset_attempted);
        write_cstr("\nport reset ok: ");
        write_u64_decimal(info.xhci_port_reset_ok);
        write_cstr("\nport reset port: ");
        write_u64_decimal(info.xhci_port_reset_port);
        write_cstr("\nport reset before: ");
        write_u64_hex(info.xhci_port_reset_portsc_before);
        write_cstr("\nport reset after: ");
        write_u64_hex(info.xhci_port_reset_portsc_after);
        write_cstr("\nport reset enabled: ");
        write_u64_decimal(info.xhci_port_reset_enabled);
        write_cstr("\nport reset speed: ");
        write_u64_decimal(info.xhci_port_reset_speed);
        write_cstr("\nport reset link state: ");
        write_u64_decimal(info.xhci_port_reset_link_state);
        write_cstr("\naddress device attempted: ");
        write_u64_decimal(info.xhci_address_device_attempted);
        write_cstr("\naddress device ok: ");
        write_u64_decimal(info.xhci_address_device_ok);
        write_cstr("\naddress device slot: ");
        write_u64_decimal(info.xhci_address_device_slot_id);
        write_cstr("\naddress device cc: ");
        write_u64_decimal(info.xhci_address_device_completion_code);
        write_cstr("\naddress device event type: ");
        write_u64_decimal(info.xhci_address_device_event_type);
        write_cstr("\naddress event trb: ");
        write_u64_hex(info.xhci_address_device_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_address_device_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_address_device_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_address_device_event_trb3);
        write_cstr("\ninput context: ");
        write_u64_hex(info.xhci_input_context_phys);
        write_cstr("\noutput context: ");
        write_u64_hex(info.xhci_output_context_phys);
        write_cstr("\nep0 ring: ");
        write_u64_hex(info.xhci_ep0_ring_phys);
        write_cstr("\nep0 max packet: ");
        write_u64_decimal(info.xhci_ep0_max_packet_size);
        write_cstr("\nerdp after address: ");
        write_u64_hex(info.xhci_erdp_after_address);
        write_cstr("\nusb address: ");
        write_u64_decimal(info.xhci_addressed_device_address);
        write_cstr("\nslot state: ");
        write_u64_decimal(info.xhci_addressed_slot_state);
        write_cstr("\noutput slot ctx: ");
        write_u64_hex(info.xhci_output_slot_context0);
        write_cstr(" ");
        write_u64_hex(info.xhci_output_slot_context1);
        write_cstr(" ");
        write_u64_hex(info.xhci_output_slot_context2);
        write_cstr(" ");
        write_u64_hex(info.xhci_output_slot_context3);
        write_cstr("\ndevice descriptor attempted: ");
        write_u64_decimal(info.xhci_device_descriptor_attempted);
        write_cstr("\ndevice descriptor ok: ");
        write_u64_decimal(info.xhci_device_descriptor_ok);
        write_cstr("\ndevice descriptor cc: ");
        write_u64_decimal(info.xhci_device_descriptor_completion_code);
        write_cstr("\ndevice descriptor event type: ");
        write_u64_decimal(info.xhci_device_descriptor_event_type);
        write_cstr("\ndevice descriptor trb: ");
        write_u64_hex(info.xhci_device_descriptor_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_device_descriptor_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_device_descriptor_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_device_descriptor_event_trb3);
        write_cstr("\ndevice descriptor buffer: ");
        write_u64_hex(info.xhci_device_descriptor_phys);
        write_cstr("\ndevice descriptor raw: ");
        write_u64_hex(info.xhci_device_descriptor_first8);
        write_cstr(" ");
        write_u64_hex(info.xhci_device_descriptor_second8);
        write_cstr(" ");
        write_u64_hex(info.xhci_device_descriptor_tail);
        write_cstr("\ndesc len/type: ");
        write_u64_decimal(info.xhci_device_descriptor_length);
        write_cstr("/");
        write_u64_decimal(info.xhci_device_descriptor_type);
        write_cstr("\nusb version: ");
        write_u64_hex(info.xhci_device_usb_version);
        write_cstr("\ndevice class/subclass/protocol: ");
        write_u64_decimal(info.xhci_device_class);
        write_cstr("/");
        write_u64_decimal(info.xhci_device_subclass);
        write_cstr("/");
        write_u64_decimal(info.xhci_device_protocol);
        write_cstr("\ndevice max packet raw: ");
        write_u64_decimal(info.xhci_device_max_packet_raw);
        write_cstr("\nvendor/product: ");
        write_u64_hex(info.xhci_device_vendor_id);
        write_cstr("/");
        write_u64_hex(info.xhci_device_product_id);
        write_cstr("\ndevice version: ");
        write_u64_hex(info.xhci_device_version);
        write_cstr("\nconfiguration descriptor attempted: ");
        write_u64_decimal(info.xhci_configuration_descriptor_attempted);
        write_cstr("\nconfiguration descriptor ok: ");
        write_u64_decimal(info.xhci_configuration_descriptor_ok);
        write_cstr("\nconfiguration descriptor cc: ");
        write_u64_decimal(info.xhci_configuration_descriptor_completion_code);
        write_cstr("\nconfiguration descriptor event type: ");
        write_u64_decimal(info.xhci_configuration_descriptor_event_type);
        write_cstr("\nconfiguration descriptor trb: ");
        write_u64_hex(info.xhci_configuration_descriptor_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_configuration_descriptor_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_configuration_descriptor_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_configuration_descriptor_event_trb3);
        write_cstr("\nconfiguration descriptor buffer: ");
        write_u64_hex(info.xhci_configuration_descriptor_phys);
        write_cstr("\nconfiguration descriptor raw: ");
        write_u64_hex(info.xhci_configuration_descriptor_first8);
        write_cstr(" ");
        write_u64_hex(info.xhci_configuration_descriptor_second8);
        write_cstr("\nconfiguration len/type/total: ");
        write_u64_decimal(info.xhci_configuration_descriptor_length);
        write_cstr("/");
        write_u64_decimal(info.xhci_configuration_descriptor_type);
        write_cstr("/");
        write_u64_decimal(info.xhci_configuration_total_length);
        write_cstr("\nconfiguration interfaces/value: ");
        write_u64_decimal(info.xhci_configuration_num_interfaces);
        write_cstr("/");
        write_u64_decimal(info.xhci_configuration_value);
        write_cstr("\nconfiguration attributes/max power: ");
        write_u64_hex(info.xhci_configuration_attributes);
        write_cstr("/");
        write_u64_decimal(info.xhci_configuration_max_power);
        write_cstr("\nfirst interface seen: ");
        write_u64_decimal(info.xhci_first_interface_seen);
        write_cstr("\nfirst interface number/alt/endpoints: ");
        write_u64_decimal(info.xhci_first_interface_number);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_interface_alternate);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_interface_endpoint_count);
        write_cstr("\nfirst interface class/subclass/protocol: ");
        write_u64_decimal(info.xhci_first_interface_class);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_interface_subclass);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_interface_protocol);
        write_cstr("\nendpoint descriptors: ");
        write_u64_decimal(info.xhci_endpoint_descriptor_count);
        write_cstr("\nbulk-in seen/address/attrs/mps/interval: ");
        write_u64_decimal(info.xhci_first_bulk_in_seen);
        write_cstr("/");
        write_u64_hex(info.xhci_first_bulk_in_address);
        write_cstr(" dci=");
        write_u64_decimal(info.xhci_first_bulk_in_dci);
        write_cstr("/");
        write_u64_hex(info.xhci_first_bulk_in_attributes);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_bulk_in_max_packet_size);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_bulk_in_interval);
        write_cstr("\nbulk-out seen/address/attrs/mps/interval: ");
        write_u64_decimal(info.xhci_first_bulk_out_seen);
        write_cstr("/");
        write_u64_hex(info.xhci_first_bulk_out_address);
        write_cstr(" dci=");
        write_u64_decimal(info.xhci_first_bulk_out_dci);
        write_cstr("/");
        write_u64_hex(info.xhci_first_bulk_out_attributes);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_bulk_out_max_packet_size);
        write_cstr("/");
        write_u64_decimal(info.xhci_first_bulk_out_interval);
        write_cstr("\nconfigure endpoint attempted: ");
        write_u64_decimal(info.xhci_configure_endpoint_attempted);
        write_cstr("\nconfigure endpoint ok: ");
        write_u64_decimal(info.xhci_configure_endpoint_ok);
        write_cstr("\nconfigure endpoint slot/cc/type: ");
        write_u64_decimal(info.xhci_configure_endpoint_slot_id);
        write_cstr("/");
        write_u64_decimal(info.xhci_configure_endpoint_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_configure_endpoint_event_type);
        write_cstr("\nconfigure endpoint event trb: ");
        write_u64_hex(info.xhci_configure_endpoint_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_configure_endpoint_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_configure_endpoint_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_configure_endpoint_event_trb3);
        write_cstr("\nconfigure endpoint input context: ");
        write_u64_hex(info.xhci_configure_endpoint_input_context_phys);
        write_cstr("\nbulk ring in/out: ");
        write_u64_hex(info.xhci_bulk_in_ring_phys);
        write_cstr("/");
        write_u64_hex(info.xhci_bulk_out_ring_phys);
        write_cstr("\nbulk-in ctx: ");
        write_u64_hex(info.xhci_bulk_in_endpoint_context0);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_in_endpoint_context1);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_in_endpoint_context2);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_in_endpoint_context3);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_in_endpoint_context4);
        write_cstr("\nbulk-out ctx: ");
        write_u64_hex(info.xhci_bulk_out_endpoint_context0);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_out_endpoint_context1);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_out_endpoint_context2);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_out_endpoint_context3);
        write_cstr(" ");
        write_u64_hex(info.xhci_bulk_out_endpoint_context4);
        write_cstr("\nset configuration attempted: ");
        write_u64_decimal(info.xhci_set_configuration_attempted);
        write_cstr("\nset configuration ok/value/cc/type: ");
        write_u64_decimal(info.xhci_set_configuration_ok);
        write_cstr("/");
        write_u64_decimal(info.xhci_set_configuration_value);
        write_cstr("/");
        write_u64_decimal(info.xhci_set_configuration_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_set_configuration_event_type);
        write_cstr("\nset configuration event trb: ");
        write_u64_hex(info.xhci_set_configuration_event_trb0);
        write_cstr(" ");
        write_u64_hex(info.xhci_set_configuration_event_trb1);
        write_cstr(" ");
        write_u64_hex(info.xhci_set_configuration_event_trb2);
        write_cstr(" ");
        write_u64_hex(info.xhci_set_configuration_event_trb3);
        write_cstr("\nbot inquiry attempted: ");
        write_u64_decimal(info.xhci_bot_inquiry_attempted);
        write_cstr("\nbot inquiry ok: ");
        write_u64_decimal(info.xhci_bot_inquiry_ok);
        write_cstr("\nbot inquiry tag: ");
        write_u64_hex(info.xhci_bot_inquiry_tag);
        write_cstr("\nbot buffers cbw/data/csw: ");
        write_u64_hex(info.xhci_bot_cbw_phys);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_inquiry_data_phys);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_csw_phys);
        write_cstr("\nbot cc cbw/data/csw: ");
        write_u64_decimal(info.xhci_bot_cbw_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_data_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_csw_completion_code);
        write_cstr("\nbot event types cbw/data/csw: ");
        write_u64_decimal(info.xhci_bot_cbw_event_type);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_data_event_type);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_csw_event_type);
        write_cstr("\nbot csw signature/tag/residue/status: ");
        write_u64_hex(info.xhci_bot_csw_signature);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_csw_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_csw_residue);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_csw_status);
        write_cstr("\nbot_last_error_stage: ");
        write_u64_decimal(info.xhci_bot_last_error_stage);
        write_cstr("\nusb_storage_readonly_policy: ");
        write_u64_decimal(info.xhci_usb_storage_readonly_policy);
        write_cstr("\ninquiry peripheral/removable/version/format/addlen: ");
        write_u64_hex(info.xhci_inquiry_peripheral);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_removable);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_version);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_response_format);
        write_cstr("/");
        write_u64_decimal(info.xhci_inquiry_additional_length);
        write_cstr("\ninquiry vendor/product/revision raw: ");
        write_u64_hex(info.xhci_inquiry_vendor_first8);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_product_first8);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_product_second8);
        write_cstr("/");
        write_u64_hex(info.xhci_inquiry_revision);
        write_cstr("\nbot read capacity attempted: ");
        write_u64_decimal(info.xhci_bot_capacity_attempted);
        write_cstr("\nbot read capacity ok: ");
        write_u64_decimal(info.xhci_bot_capacity_ok);
        write_cstr("\nbot read capacity tag/data: ");
        write_u64_hex(info.xhci_bot_capacity_tag);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_capacity_data_phys);
        write_cstr("\nbot read capacity cc cbw/data/csw: ");
        write_u64_decimal(info.xhci_bot_capacity_cbw_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_data_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_csw_completion_code);
        write_cstr("\nbot read capacity csw signature/tag/residue/status: ");
        write_u64_hex(info.xhci_bot_capacity_csw_signature);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_capacity_csw_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_csw_residue);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_csw_status);
        write_cstr("\nbot read capacity last_lba/block_size/block_count: ");
        write_u64_decimal(info.xhci_bot_capacity_last_lba);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_block_size);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_capacity_block_count);
        write_cstr("\nbot read10 attempted: ");
        write_u64_decimal(info.xhci_bot_read10_attempted);
        write_cstr("\nbot read10 ok: ");
        write_u64_decimal(info.xhci_bot_read10_ok);
        write_cstr("\nbot read10 tag/lba/bytes/data: ");
        write_u64_hex(info.xhci_bot_read10_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_lba);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_bytes);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_read10_data_phys);
        write_cstr("\nbot read10 cc cbw/data/csw: ");
        write_u64_decimal(info.xhci_bot_read10_cbw_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_data_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_csw_completion_code);
        write_cstr("\nbot read10 csw signature/tag/residue/status: ");
        write_u64_hex(info.xhci_bot_read10_csw_signature);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_read10_csw_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_csw_residue);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_read10_csw_status);
        write_cstr("\nbot read10 lba0 first16: ");
        write_u64_hex(info.xhci_bot_read10_first8);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_read10_second8);
        write_cstr("\nbot write10 attempted: ");
        write_u64_decimal(info.xhci_bot_write10_attempted);
        write_cstr("\nbot write10 ok: ");
        write_u64_decimal(info.xhci_bot_write10_ok);
        write_cstr("\nbot write10 verified: ");
        write_u64_decimal(info.xhci_bot_write10_verified);
        write_cstr("\nbot write10 tag/lba/bytes/data: ");
        write_u64_hex(info.xhci_bot_write10_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_lba);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_bytes);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_write10_data_phys);
        write_cstr("\nbot write10 cc cbw/data/csw: ");
        write_u64_decimal(info.xhci_bot_write10_cbw_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_data_completion_code);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_csw_completion_code);
        write_cstr("\nbot write10 csw signature/tag/residue/status: ");
        write_u64_hex(info.xhci_bot_write10_csw_signature);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_write10_csw_tag);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_csw_residue);
        write_cstr("/");
        write_u64_decimal(info.xhci_bot_write10_csw_status);
        write_cstr("\nbot write10 readback first16: ");
        write_u64_hex(info.xhci_bot_write10_first8);
        write_cstr("/");
        write_u64_hex(info.xhci_bot_write10_second8);
    }
    write_literal("\n", 1);
}

static void write_buffer_hex(const char *buffer, uint64_t len) {
    uint64_t offset = 0;

    while (offset < len) {
        uint64_t line_len = len - offset;
        if (line_len > 16) {
            line_len = 16;
        }

        write_u64_hex(offset);
        write_literal(": ", 2);
        for (uint64_t i = 0; i < line_len; i++) {
            write_hex_byte((uint8_t)buffer[offset + i]);
            write_literal(" ", 1);
        }
        write_literal("\n", 1);
        offset += line_len;
    }
}

static void clear_buffer(char *buffer, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        buffer[i] = 0;
    }
}

static int find_block_device_by_name(const char *name, uint64_t *index_out,
                                     struct block_device_info *info_out) {
    struct block_device_info info;

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_block_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }
        if (strings_equal(info.name, name)) {
            if (index_out != 0) {
                *index_out = i;
            }
            if (info_out != 0) {
                *info_out = info;
            }
            return 0;
        }
    }

    return -1;
}

static void cmd_lsblk(void) {
    static const char header[] = "id name block_size blocks writable\n";
    struct block_device_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_block_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }

        write_u64_decimal(i);
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal(" ", 1);
        write_u64_decimal(info.block_size);
        write_literal(" ", 1);
        write_u64_decimal(info.block_count);
        write_literal(" ", 1);
        write_u64_decimal(info.writable);
        write_literal("\n", 1);
    }
}

static int block_device_exists(const char *name) {
    struct block_device_info info;

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_block_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }
        if (strings_equal(info.name, name)) {
            return 1;
        }
    }

    return 0;
}

static void cmd_usbdiag(void) {
    static const char fail[] = "usbdiag: failed\n";
    static struct system_info info;
    int usb0_present;

    if (sys_system_info(&info) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    usb0_present = block_device_exists("usb0");

    write_cstr("xhci_found: ");
    write_u64_decimal(info.xhci_found);
    write_cstr("\ncontroller_started: ");
    write_u64_decimal(info.xhci_controller_started);
    write_cstr("\ncontroller_start_result: ");
    write_u64_decimal(info.xhci_controller_start_result);
    write_cstr("\nreset_policy: ");
    write_u64_decimal(info.xhci_reset_policy);
    write_cstr("\nroot_power_attempted/changed/rescans: ");
    write_u64_decimal(info.xhci_port_power_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_port_power_changed_count);
    write_cstr("/");
    write_u64_decimal(info.xhci_port_connect_rescan_count);
    write_cstr("\nconnected_ports: ");
    write_u64_decimal(info.xhci_connected_port_count);
    write_cstr("\nselected_port/speed/score: ");
    write_u64_decimal(info.xhci_selected_connected_port);
    write_cstr("/");
    write_u64_decimal(info.xhci_selected_connected_speed);
    write_cstr("/");
    write_u64_decimal(info.xhci_selected_connected_score);
    write_cstr("\nport_reset_attempted/ok/port: ");
    write_u64_decimal(info.xhci_port_reset_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_port_reset_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_port_reset_port);
    write_cstr("\nwarm_reset_attempted/ok/after: ");
    write_u64_decimal(info.xhci_port_warm_reset_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_port_warm_reset_ok);
    write_cstr("/");
    write_u64_hex(info.xhci_port_warm_reset_portsc_after);
    write_cstr("\naddress_attempted/ok/cc: ");
    write_u64_decimal(info.xhci_address_device_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_address_device_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_address_device_completion_code);
    write_cstr("\ndevice_class/subclass/protocol: ");
    write_u64_decimal(info.xhci_device_class);
    write_cstr("/");
    write_u64_decimal(info.xhci_device_subclass);
    write_cstr("/");
    write_u64_decimal(info.xhci_device_protocol);
    write_cstr("\ninterface_class/subclass/protocol: ");
    write_u64_decimal(info.xhci_first_interface_class);
    write_cstr("/");
    write_u64_decimal(info.xhci_first_interface_subclass);
    write_cstr("/");
    write_u64_decimal(info.xhci_first_interface_protocol);
    write_cstr("\nconfigure endpoint attempted/ok/cc: ");
    write_u64_decimal(info.xhci_configure_endpoint_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_configure_endpoint_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_configure_endpoint_completion_code);
    write_cstr("\nset configuration attempted/ok/cc: ");
    write_u64_decimal(info.xhci_set_configuration_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_set_configuration_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_set_configuration_completion_code);
    write_cstr("\nhub detected/config/desc/ports: ");
    write_u64_decimal(info.xhci_hub_detected);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_configuration_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_descriptor_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_port_count);
    write_cstr("\nhub config attempted/cc: ");
    write_u64_decimal(info.xhci_hub_configuration_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_configuration_completion_code);
    write_cstr("\nhub desc cc/len/type: ");
    write_u64_decimal(info.xhci_hub_descriptor_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_descriptor_length);
    write_cstr("/");
    write_u64_hex(info.xhci_hub_descriptor_type);
    write_cstr("\nhub power_ok/status_port/reset_ok: ");
    write_u64_decimal(info.xhci_hub_port_power_ok_count);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_first_connected_port);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_port_reset_ok);
    write_cstr("\nhub port status/change: ");
    write_u64_hex(info.xhci_hub_port_reset_status ?
                  info.xhci_hub_port_reset_status :
                  info.xhci_hub_first_port_status);
    write_cstr("/");
    write_u64_hex(info.xhci_hub_port_reset_change ?
                  info.xhci_hub_port_reset_change :
                  info.xhci_hub_first_port_change);
    write_cstr("\nhub slot update attempted/ok/cc: ");
    write_u64_decimal(info.xhci_hub_slot_update_attempted);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_slot_update_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_hub_slot_update_completion_code);
    write_cstr("\nhub slot id/ctx0/ctx1: ");
    write_u64_decimal(info.xhci_hub_slot_update_slot_id);
    write_cstr("/");
    write_u64_hex(info.xhci_hub_slot_update_context0);
    write_cstr("/");
    write_u64_hex(info.xhci_hub_slot_update_context1);
    write_cstr("\nbot_inquiry/read_capacity/read10: ");
    write_u64_decimal(info.xhci_bot_inquiry_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_capacity_ok);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_read10_ok);
    write_cstr("\nbot cc cbw/data/csw/stage: ");
    write_u64_decimal(info.xhci_bot_cbw_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_data_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_csw_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_last_error_stage);
    write_cstr("\nread10 cc cbw/data/csw/status: ");
    write_u64_decimal(info.xhci_bot_read10_cbw_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_read10_data_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_read10_csw_completion_code);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_read10_csw_status);
    write_cstr("\nread10 lba/tag/csw_tag/residue: ");
    write_u64_decimal(info.xhci_bot_read10_lba);
    write_cstr("/");
    write_u64_hex(info.xhci_bot_read10_tag);
    write_cstr("/");
    write_u64_hex(info.xhci_bot_read10_csw_tag);
    write_cstr("/");
    write_u64_decimal(info.xhci_bot_read10_csw_residue);
    write_cstr("\nbot csw event trb: ");
    write_u64_hex(info.xhci_bot_csw_event_trb0);
    write_cstr(" ");
    write_u64_hex(info.xhci_bot_csw_event_trb1);
    write_cstr(" ");
    write_u64_hex(info.xhci_bot_csw_event_trb2);
    write_cstr(" ");
    write_u64_hex(info.xhci_bot_csw_event_trb3);
    write_cstr("\nusb0_present: ");
    write_u64_decimal(usb0_present);
    write_cstr("\nreason: ");

    if (!info.xhci_found) {
        write_cstr("no_xhci_controller");
    } else if (!info.xhci_mmio_mapped) {
        write_cstr("xhci_mmio_not_mapped");
    } else if (!info.xhci_reset_allowed) {
        write_cstr("xhci_reset_blocked");
    } else if (!info.xhci_controller_started) {
        write_cstr("xhci_controller_not_started");
    } else if (info.xhci_connected_port_count == 0 ||
               info.xhci_selected_connected_port == 0) {
        write_cstr("no_connected_root_port");
    } else if (!info.xhci_port_reset_attempted) {
        write_cstr("root_port_reset_not_attempted");
    } else if (!info.xhci_port_reset_ok) {
        write_cstr("root_port_reset_failed");
    } else if (info.xhci_hub_detected &&
               !info.xhci_hub_configuration_ok) {
        write_cstr("usb_hub_configuration_failed");
    } else if (info.xhci_hub_detected &&
               !info.xhci_hub_descriptor_ok) {
        write_cstr("usb_hub_descriptor_failed");
    } else if (info.xhci_hub_detected &&
               info.xhci_hub_first_connected_port == 0) {
        write_cstr("usb_hub_no_downstream_device");
    } else if (info.xhci_hub_detected &&
               !info.xhci_hub_port_reset_ok) {
        write_cstr("usb_hub_downstream_reset_failed");
    } else if (info.xhci_hub_detected &&
               info.xhci_hub_descriptor_ok &&
               !info.xhci_hub_slot_update_attempted) {
        write_cstr("usb_hub_slot_context_update_not_attempted");
    } else if (info.xhci_hub_detected &&
               info.xhci_hub_slot_update_attempted &&
               !info.xhci_hub_slot_update_ok) {
        write_cstr("usb_hub_slot_context_update_failed");
    } else if (!info.xhci_address_device_attempted) {
        write_cstr(info.xhci_hub_detected ?
                   "usb_hub_second_slot_not_attempted" :
                   "address_device_not_attempted");
    } else if (!info.xhci_address_device_ok) {
        write_cstr(info.xhci_hub_detected ?
                   "usb_hub_second_slot_address_failed" :
                   "address_device_failed");
    } else if (info.xhci_first_interface_seen &&
               (info.xhci_first_interface_class != 8 ||
                info.xhci_first_interface_subclass != 6 ||
                info.xhci_first_interface_protocol != 80)) {
        write_cstr(info.xhci_hub_detected ?
                   "usb_hub_downstream_not_mass_storage_bot" :
                   "first_device_is_not_mass_storage_bot");
    } else if (info.xhci_set_configuration_attempted &&
               !info.xhci_set_configuration_ok) {
        write_cstr("mass_storage_set_configuration_failed");
    } else if (!info.xhci_bot_inquiry_ok) {
        write_cstr("mass_storage_inquiry_failed_or_not_attempted");
    } else if (!info.xhci_bot_capacity_ok) {
        write_cstr("mass_storage_capacity_failed");
    } else if (!usb0_present) {
        write_cstr("mass_storage_ready_but_usb0_not_registered");
    } else {
        write_cstr("usb0_ready");
    }
    write_cstr("\nhint: ");
    if (info.xhci_hub_detected ||
        info.xhci_device_class == 9 || info.xhci_first_interface_class == 9) {
        if (info.xhci_hub_slot_update_attempted &&
            !info.xhci_hub_slot_update_ok) {
            write_cstr("Hub slot context update failed; capture hub slot fields.");
        } else if (info.xhci_hub_port_reset_ok) {
            write_cstr("Hub downstream reset works; inspect second-slot address/config/BOT fields.");
        } else {
            write_cstr("Hub support is partial; capture these hub fields for the next driver step.");
        }
    } else if (!usb0_present) {
        write_cstr("Try a directly attached USB mass-storage device, then capture usbdiag.");
    } else {
        write_cstr("Run lsblk and mounts.");
    }
    write_cstr("\n");
}

static void cmd_mounts(void) {
    static const char header[] = "id path fs source writable\n";
    struct mount_info info;

    write_literal(header, sizeof(header) - 1);
    for (uint64_t i = 0; i < 8; i++) {
        uint64_t result = sys_mount_info(i, &info);
        if (result == (uint64_t)-1) {
            break;
        }

        write_u64_decimal(i);
        write_literal(" ", 1);
        write_cstr_limited(info.path, sizeof(info.path));
        write_literal(" ", 1);
        write_cstr_limited(info.name, sizeof(info.name));
        write_literal(" ", 1);
        write_cstr_limited(info.source, sizeof(info.source));
        write_literal(" ", 1);
        write_u64_decimal(info.writable);
        write_literal("\n", 1);
    }
}

static int build_mount_info_path(const char *mount_path, char *out, uint64_t out_len) {
    static const char info_name[] = "info.txt";
    uint64_t len = string_length(mount_path);
    uint64_t pos = 0;

    if (len == 0 || out_len == 0) {
        return -1;
    }
    while (len > 1 && mount_path[len - 1] == '/') {
        len--;
    }
    if (len + sizeof(info_name) + 1 >= out_len) {
        return -1;
    }
    for (uint64_t i = 0; i < len; i++) {
        out[pos++] = mount_path[i];
    }
    if (pos == 0 || out[pos - 1] != '/') {
        out[pos++] = '/';
    }
    for (uint64_t i = 0; i < sizeof(info_name) - 1; i++) {
        out[pos++] = info_name[i];
    }
    out[pos] = '\0';
    return 0;
}

static int read_mount_info_text(const char *mount_path, char *info_path,
                                uint64_t info_path_len, char *buffer,
                                uint64_t buffer_len) {
    if (buffer_len < 1024 ||
        build_mount_info_path(mount_path, info_path, info_path_len) != 0) {
        return -1;
    }

    uint64_t fd = sys_open(info_path);
    if (fd == (uint64_t)-1) {
        return -1;
    }

    uint64_t read_len = 0;
    while (read_len + 1 < buffer_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, buffer_len - 1 - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            return -1;
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);

    if (read_len == 0) {
        return -1;
    }
    buffer[read_len] = '\0';
    return 0;
}

static int info_field_value(const char *info, const char *field,
                            const char **value_out, uint64_t *len_out) {
    uint64_t field_len = string_length(field);

    for (uint64_t i = 0; info[i] != '\0'; i++) {
        uint64_t j = 0;
        if (i != 0 && info[i - 1] != '\n') {
            continue;
        }
        while (j < field_len && info[i + j] == field[j]) {
            j++;
        }
        if (j == field_len) {
            uint64_t len = 0;
            const char *value = info + i + field_len;
            while (value[len] != '\0' && value[len] != '\n') {
                len++;
            }
            *value_out = value;
            *len_out = len;
            return 0;
        }
    }

    return -1;
}

static int info_field_equals(const char *info, const char *field,
                             const char *expected) {
    const char *value;
    uint64_t len;
    uint64_t expected_len = string_length(expected);

    if (info_field_value(info, field, &value, &len) != 0 ||
        len != expected_len) {
        return 0;
    }
    for (uint64_t i = 0; i < len; i++) {
        if (value[i] != expected[i]) {
            return 0;
        }
    }
    return 1;
}

static void writeaudit_field(const char *info, const char *label,
                             const char *field) {
    static const char missing[] = "missing";
    const char *value;
    uint64_t len;

    write_cstr(label);
    if (info_field_value(info, field, &value, &len) == 0) {
        sys_write(STDOUT_FD, value, len);
    } else {
        write_literal(missing, sizeof(missing) - 1);
    }
    write_literal("\n", 1);
}

static int writeaudit_info_is_usable(const char *info) {
    return contains_text(info, "detected_fs: exfat") &&
           contains_text(info, "write_candidate_ready: ") &&
           contains_text(info, "write_policy_allows_commit: ") &&
           contains_text(info, "manual_unlock_attempted: ") &&
           contains_text(info, "manual_unlock_ready: ") &&
           contains_text(info, "manual_unlock_blocker: ") &&
           contains_text(info, "real_write_prereq_ready: ") &&
           contains_text(info, "real_write_dry_run_lbas: ") &&
           contains_text(info, "real_write_dry_run_clusters: ");
}

static void cmd_writeaudit(const char *mount_path, char *buffer,
                           uint64_t buffer_len) {
    static const char fail[] = "writeaudit: failed\n";
    static const char header[] = "writeaudit: ";
    static const char info_path_prefix[] = "info: ";
    static const char missing_exfat[] = "result: no_exfat_write_audit\n";
    static const char writable[] = "result: policy_allows_write\n";
    static const char readonly[] = "result: readonly_or_locked\n";
    char info_path[SHELL_PATH_MAX];

    if (read_mount_info_text(mount_path, info_path, sizeof(info_path),
                             buffer, buffer_len) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_literal(header, sizeof(header) - 1);
    write_cstr(mount_path);
    write_literal("\n", 1);
    write_literal(info_path_prefix, sizeof(info_path_prefix) - 1);
    write_cstr(info_path);
    write_literal("\n", 1);

    if (!writeaudit_info_is_usable(buffer)) {
        write_literal(missing_exfat, sizeof(missing_exfat) - 1);
        return;
    }

    writeaudit_field(buffer, "fs: ", "detected_fs: ");
    writeaudit_field(buffer, "source: ", "source: ");
    writeaudit_field(buffer, "write_enabled: ", "write_enabled: ");
    writeaudit_field(buffer, "write_policy: ", "write_policy: ");
    writeaudit_field(buffer, "write_blocker: ", "write_blocker: ");
    writeaudit_field(buffer, "manual_unlock_attempted: ",
                     "manual_unlock_attempted: ");
    writeaudit_field(buffer, "manual_unlock_ready: ",
                     "manual_unlock_ready: ");
    writeaudit_field(buffer, "manual_unlock_blocker: ",
                     "manual_unlock_blocker: ");
    writeaudit_field(buffer, "candidate_ready: ", "write_candidate_ready: ");
    writeaudit_field(buffer, "policy_allows_commit: ",
                     "write_policy_allows_commit: ");
    writeaudit_field(buffer, "real_prereq_ready: ",
                     "real_write_prereq_ready: ");
    writeaudit_field(buffer, "real_blocker: ", "real_write_blocker: ");
    writeaudit_field(buffer, "dry_run_action: ",
                     "real_write_dry_run_action: ");
    writeaudit_field(buffer, "dry_run_path: ", "real_write_dry_run_path: ");
    writeaudit_field(buffer, "dry_run_ready: ",
                     "real_write_dry_run_ready: ");
    writeaudit_field(buffer, "dry_run_policy_allows: ",
                     "real_write_dry_run_policy_allows: ");
    writeaudit_field(buffer, "dry_run_write_count: ",
                     "real_write_dry_run_write_count: ");
    writeaudit_field(buffer, "dry_run_lbas: ", "real_write_dry_run_lbas: ");
    writeaudit_field(buffer, "dry_run_clusters: ",
                     "real_write_dry_run_clusters: ");
    writeaudit_field(buffer, "dry_run_note: ",
                     "real_write_dry_run_note: ");

    if (contains_text(buffer, "write_policy_allows_commit: 1")) {
        write_literal(writable, sizeof(writable) - 1);
    } else {
        write_literal(readonly, sizeof(readonly) - 1);
    }
}

static void cmd_writeunlock(const char *mount_path,
                            uint64_t bitmap_lba,
                            uint64_t fat_lba,
                            uint64_t directory_lba) {
    static const char ok[] = "writeunlock: ok\n";
    static const char fail[] = "writeunlock: failed\n";
    struct blockfs_unlock_request request;

    request.magic = BLOCKFS_UNLOCK_MAGIC;
    request.expected_bitmap_lba = bitmap_lba;
    request.expected_fat_lba = fat_lba;
    request.expected_directory_lba = directory_lba;
    request.flags = 0;

    if (sys_blockfs_unlock(mount_path, &request) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void cmd_blkread(uint64_t device, uint64_t lba, char *buffer, uint64_t buffer_len) {
    static const char fail[] = "blkread: failed\n";

    if (buffer_len < 512 || sys_block_read(device, lba, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    write_buffer_hex(buffer, 512);
}

static void cmd_blkwrite(uint64_t device, uint64_t lba, const char *text,
                         char *buffer, uint64_t buffer_len) {
    static const char ok[] = "blkwrite: ok\n";
    static const char fail[] = "blkwrite: failed\n";
    uint64_t text_len = string_length(text);

    if (buffer_len < 512 || text_len >= 512) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    clear_buffer(buffer, 512);
    for (uint64_t i = 0; i < text_len; i++) {
        buffer[i] = text[i];
    }

    if (sys_block_write(device, lba, buffer, 1) == 1) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

static void fill_usbtestwrite_pattern(char *buffer, uint64_t len) {
    static const char marker[] = "TangPingOS usbtestwrite scratch sector\n";

    clear_buffer(buffer, len);
    for (uint64_t i = 0; i < sizeof(marker) - 1 && i < len; i++) {
        buffer[i] = marker[i];
    }
    if (len >= 24) {
        buffer[16] = (char)(USB_TEST_SCRATCH_LBA & 0xff);
        buffer[17] = (char)((USB_TEST_SCRATCH_LBA >> 8) & 0xff);
        buffer[18] = (char)((USB_TEST_SCRATCH_LBA >> 16) & 0xff);
        buffer[19] = (char)((USB_TEST_SCRATCH_LBA >> 24) & 0xff);
        buffer[20] = 'T';
        buffer[21] = 'P';
        buffer[22] = 'O';
        buffer[23] = 'S';
    }
}

static int usbtestwrite_run(char *buffer, uint64_t buffer_len, int verbose) {
    static const char missing[] = "usbtestwrite: usb0p1 test partition not found\n";
    static const char unsafe[] = "usbtestwrite: refusing non-test USB partition\n";
    static const char readonly[] = "usbtestwrite: usb0p1 is readonly\n";
    static const char failed[] = "usbtestwrite: failed\n";
    static const char mismatch[] = "usbtestwrite: readback mismatch\n";
    static const char ok[] = "usbtestwrite: ok device=";
    static const char lba_text[] = " lba=";
    uint64_t device = 0;
    struct block_device_info info;

    if (buffer_len < 1024) {
        if (verbose) {
            write_literal(failed, sizeof(failed) - 1);
        }
        return -1;
    }
    if (find_block_device_by_name("usb0p1", &device, &info) != 0) {
        if (verbose) {
            write_literal(missing, sizeof(missing) - 1);
        }
        return -1;
    }
    if (info.block_size != 512 || info.block_count != USB_TEST_PARTITION_BLOCKS) {
        if (verbose) {
            write_literal(unsafe, sizeof(unsafe) - 1);
        }
        return -1;
    }
    if (info.writable == 0) {
        if (verbose) {
            write_literal(readonly, sizeof(readonly) - 1);
        }
        return -1;
    }

    fill_usbtestwrite_pattern(buffer, 512);
    if (sys_block_write(device, USB_TEST_SCRATCH_LBA, buffer, 1) != 1) {
        if (verbose) {
            write_literal(failed, sizeof(failed) - 1);
        }
        return -1;
    }

    clear_buffer(buffer + 512, 512);
    if (sys_block_read(device, USB_TEST_SCRATCH_LBA, buffer + 512, 1) != 1) {
        if (verbose) {
            write_literal(failed, sizeof(failed) - 1);
        }
        return -1;
    }
    for (uint64_t i = 0; i < 512; i++) {
        if (buffer[i] != buffer[512 + i]) {
            if (verbose) {
                write_literal(mismatch, sizeof(mismatch) - 1);
            }
            return -1;
        }
    }

    if (verbose) {
        write_literal(ok, sizeof(ok) - 1);
        write_u64_decimal(device);
        write_literal(lba_text, sizeof(lba_text) - 1);
        write_u64_decimal(USB_TEST_SCRATCH_LBA);
        write_literal("\n", 1);
    }
    return 0;
}

static void cmd_run(const char *path, const char *args) {
    static const char ok[] = "run: started\n";
    static const char fail[] = "run: failed\n";

    if (sys_spawn(path, args) == 0) {
        write_literal(ok, sizeof(ok) - 1);
    } else {
        write_literal(fail, sizeof(fail) - 1);
    }
}

/*
 * Minimal account database, modelled on the POSIX layout:
 *   /etc/passwd : name:uid:gid
 *   /etc/shadow : name:salt:sha256(salt + password)
 *   /etc/group  : name:x:gid:
 */
#define ACCOUNT_NAME_MAX 32
#define ACCOUNT_DB_MAX 1024

static uint64_t account_read_file(const char *path, char *buffer, uint64_t capacity) {
    uint64_t fd = sys_open(path);
    uint64_t total = 0;

    if (fd == (uint64_t)-1) {
        return 0;
    }
    while (total + 1 < capacity) {
        uint64_t got = sys_read(fd, buffer + total, capacity - 1 - total);
        if (got == 0 || got == (uint64_t)-1) {
            break;
        }
        total += got;
    }
    sys_close(fd);
    buffer[total] = '\0';
    return total;
}

static int account_write_file(const char *path, const char *content) {
    uint64_t fd = sys_open_flags(path, OPEN_CREATE | OPEN_TRUNC);
    uint64_t len = string_length(content);

    if (fd == (uint64_t)-1) {
        return -1;
    }
    if (sys_write(fd, content, len) != len) {
        sys_close(fd);
        return -1;
    }
    sys_close(fd);
    return 0;
}

static const char *account_field(const char *cursor, char *out, uint64_t capacity) {
    uint64_t len = 0;

    while (*cursor != '\0' && *cursor != ':' && *cursor != '\n') {
        if (len + 1 < capacity) {
            out[len++] = *cursor;
        }
        cursor++;
    }
    out[len] = '\0';
    /* Only consume the ':' separator: the newline stays so the caller can
       detect the end of the record after skipping trailing fields. */
    if (*cursor == ':') {
        cursor++;
    }
    return cursor;
}

static int account_lookup(const char *name, uint64_t *uid, uint64_t *gid) {
    static char db[ACCOUNT_DB_MAX];
    const char *cursor = db;

    if (account_read_file("/etc/passwd", db, sizeof(db)) == 0) {
        return -1;
    }
    while (*cursor != '\0') {
        char field_name[ACCOUNT_NAME_MAX];
        char field_uid[24];
        char field_gid[24];
        char skip[ACCOUNT_NAME_MAX];

        cursor = account_field(cursor, field_name, sizeof(field_name));
        cursor = account_field(cursor, field_uid, sizeof(field_uid));
        cursor = account_field(cursor, field_gid, sizeof(field_gid));
        while (*cursor != '\0' && *cursor != '\n') {
            cursor = account_field(cursor, skip, sizeof(skip));
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (strings_equal(field_name, name)) {
            if (parse_u64(field_uid, uid) != 0 || parse_u64(field_gid, gid) != 0) {
                return -1;
            }
            return 0;
        }
    }
    return -1;
}

static int account_name_by_uid(uint64_t uid, char *out, uint64_t capacity) {
    static char db[ACCOUNT_DB_MAX];
    const char *cursor = db;

    if (account_read_file("/etc/passwd", db, sizeof(db)) == 0) {
        return -1;
    }
    while (*cursor != '\0') {
        char field_name[ACCOUNT_NAME_MAX];
        char field_uid[24];
        char field_gid[24];
        char skip[ACCOUNT_NAME_MAX];
        uint64_t value = 0;

        cursor = account_field(cursor, field_name, sizeof(field_name));
        cursor = account_field(cursor, field_uid, sizeof(field_uid));
        cursor = account_field(cursor, field_gid, sizeof(field_gid));
        while (*cursor != '\0' && *cursor != '\n') {
            cursor = account_field(cursor, skip, sizeof(skip));
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (parse_u64(field_uid, &value) == 0 && value == uid) {
            copy_cstr_limited(out, capacity, field_name);
            return 0;
        }
    }
    return -1;
}

static int account_shadow(const char *name, char *salt, uint64_t salt_cap, char *hash) {
    static char db[ACCOUNT_DB_MAX];
    const char *cursor = db;

    if (account_read_file("/etc/shadow", db, sizeof(db)) == 0) {
        return -1;
    }
    while (*cursor != '\0') {
        char field_name[ACCOUNT_NAME_MAX];
        char field_salt[32];
        char field_hash[65];
        char skip[ACCOUNT_NAME_MAX];

        cursor = account_field(cursor, field_name, sizeof(field_name));
        cursor = account_field(cursor, field_salt, sizeof(field_salt));
        cursor = account_field(cursor, field_hash, sizeof(field_hash));
        while (*cursor != '\0' && *cursor != '\n') {
            cursor = account_field(cursor, skip, sizeof(skip));
        }
        if (*cursor == '\n') {
            cursor++;
        }
        if (strings_equal(field_name, name)) {
            copy_cstr_limited(salt, salt_cap, field_salt);
            copy_cstr_limited(hash, 65, field_hash);
            return 0;
        }
    }
    return -1;
}

/* Appends one line to a database file. */
static int account_append_line(const char *path, const char *line) {
    static char db[ACCOUNT_DB_MAX];
    uint64_t used = account_read_file(path, db, sizeof(db));

    if (used + string_length(line) + 1 >= sizeof(db)) {
        return -1;
    }
    if (used > 0 && db[used - 1] != '\n') {
        db[used++] = '\n';
    }
    copy_cstr_limited(db + used, sizeof(db) - used, line);
    used += string_length(line);
    db[used++] = '\n';
    db[used] = '\0';
    return account_write_file(path, db);
}

static void read_password(const char *prompt, char *buffer, uint64_t capacity) {
    uint64_t len = 0;

    write_cstr(prompt);
    sys_tty_set_echo(0);
    while (len + 1 < capacity) {
        char c = '\0';
        if (sys_read(STDIN_FD, &c, 1) != 1 || c == '\n' || c == '\r') {
            break;
        }
        buffer[len++] = c;
    }
    buffer[len] = '\0';
    sys_tty_set_echo(1);
    write_literal("\n", 1);
}

static void cmd_whoami(void) {
    char name[ACCOUNT_NAME_MAX];

    if (account_name_by_uid(sys_geteuid(), name, sizeof(name)) != 0) {
        copy_cstr_limited(name, sizeof(name), "unknown");
    }
    write_cstr(name);
    write_literal("\n", 1);
}

static void cmd_id(void) {
    char name[ACCOUNT_NAME_MAX];
    char group[ACCOUNT_NAME_MAX];

    if (account_name_by_uid(sys_geteuid(), name, sizeof(name)) != 0) {
        copy_cstr_limited(name, sizeof(name), "unknown");
    }
    if (account_name_by_uid(sys_getegid(), group, sizeof(group)) != 0) {
        copy_cstr_limited(group, sizeof(group), "unknown");
    }
    write_literal("uid=", 4);
    write_u64_decimal(sys_geteuid());
    write_literal("(", 1);
    write_cstr(name);
    write_literal(") gid=", 6);
    write_u64_decimal(sys_getegid());
    write_literal("(", 1);
    write_cstr(group);
    write_literal(")\n", 2);
}

static void cmd_groups(void) {
    char name[ACCOUNT_NAME_MAX];

    if (account_name_by_uid(sys_getegid(), name, sizeof(name)) != 0) {
        copy_cstr_limited(name, sizeof(name), "unknown");
    }
    write_cstr(name);
    write_literal("\n", 1);
}

static void cmd_su(const char *args) {
    static const char usage[] = "usage: su [user]\n";
    static const char fail[] = "su: authentication failure\n";
    char target[ACCOUNT_NAME_MAX];
    char salt[32];
    char stored[65];
    char computed[65];
    char password[64];
    uint64_t uid = 0;
    uint64_t gid = 0;

    if (args[0] == '\0') {
        copy_cstr_limited(target, sizeof(target), "root");
    } else {
        copy_cstr_limited(target, sizeof(target), args);
        if (target[0] == '\0') {
            write_literal(usage, sizeof(usage) - 1);
            return;
        }
    }

    if (account_lookup(target, &uid, &gid) != 0 || account_shadow(target, salt, sizeof(salt), stored) != 0) {
        write_cstr("su: user ");
        write_cstr(target);
        write_literal(" does not exist\n", 16);
        return;
    }

    read_password("Password: ", password, sizeof(password));
    tpos_sha256_salted_hex(salt, password, computed);
    if (!strings_equal(computed, stored)) {
        write_literal(fail, sizeof(fail) - 1);
        return;
    }

    if (sys_setgid(gid) != 0 || sys_setuid(uid) != 0) {
        write_literal("su: cannot change credentials\n", 29);
        return;
    }
    if (sys_spawn("/bin/shell.elf", "") != 0) {
        write_literal("su: failed to start shell\n", 26);
        return;
    }
    sys_exit(0);
}

static void cmd_useradd(const char *args) {
    static const char denied[] = "useradd: permission denied\n";
    static const char usage[] = "usage: useradd name\n";
    char name[ACCOUNT_NAME_MAX];
    char line[ACCOUNT_DB_MAX];
    char salt[24];
    char salt_seed[24];
    char hash[65];
    char uid_text[24];
    uint64_t uid = 0;
    uint64_t gid = 0;
    uint64_t next_uid = 1000;

    if (sys_geteuid() != 0) {
        write_literal(denied, sizeof(denied) - 1);
        return;
    }
    copy_cstr_limited(name, sizeof(name), args);
    if (name[0] == '\0') {
        write_literal(usage, sizeof(usage) - 1);
        return;
    }
    if (account_lookup(name, &uid, &gid) == 0) {
        write_cstr("useradd: user ");
        write_cstr(name);
        write_literal(" already exists\n", 16);
        return;
    }

    /* Pick the next free uid above the existing ones. */
    {
        static char db[ACCOUNT_DB_MAX];
        const char *cursor = db;
        if (account_read_file("/etc/passwd", db, sizeof(db)) > 0) {
            while (*cursor != '\0') {
                char field_name[ACCOUNT_NAME_MAX];
                char field_uid[24];
                char field_gid[24];
                char skip[ACCOUNT_NAME_MAX];
                uint64_t value = 0;

                cursor = account_field(cursor, field_name, sizeof(field_name));
                cursor = account_field(cursor, field_uid, sizeof(field_uid));
                cursor = account_field(cursor, field_gid, sizeof(field_gid));
                while (*cursor != '\0' && *cursor != '\n') {
                    cursor = account_field(cursor, skip, sizeof(skip));
                }
                if (*cursor == '\n') {
                    cursor++;
                }
                if (parse_u64(field_uid, &value) == 0 && value >= next_uid) {
                    next_uid = value + 1;
                }
            }
        }
    }

    u64_to_decimal(next_uid, uid_text);
    copy_cstr_limited(salt_seed, sizeof(salt_seed), "tp");
    {
        uint64_t seed_len = string_length(salt_seed);
        copy_cstr_limited(salt_seed + seed_len, sizeof(salt_seed) - seed_len, uid_text);
    }
    copy_cstr_limited(salt, sizeof(salt), salt_seed);
    tpos_sha256_salted_hex(salt, "changeme", hash);

    line[0] = '\0';
    copy_cstr_limited(line, sizeof(line), name);
    {
        uint64_t len = string_length(line);
        write_literal("", 0);
        line[len++] = ':';
        copy_cstr_limited(line + len, sizeof(line) - len, uid_text);
        len = string_length(line);
        line[len++] = ':';
        copy_cstr_limited(line + len, sizeof(line) - len, uid_text);
        line[string_length(line)] = '\0';
    }
    if (account_append_line("/etc/passwd", line) != 0) {
        write_literal("useradd: cannot update /etc/passwd\n", 35);
        return;
    }

    line[0] = '\0';
    copy_cstr_limited(line, sizeof(line), name);
    {
        uint64_t len = string_length(line);
        line[len++] = ':';
        copy_cstr_limited(line + len, sizeof(line) - len, salt);
        len = string_length(line);
        line[len++] = ':';
        copy_cstr_limited(line + len, sizeof(line) - len, hash);
    }
    account_append_line("/etc/shadow", line);

    line[0] = '\0';
    copy_cstr_limited(line, sizeof(line), name);
    {
        uint64_t len = string_length(line);
        copy_cstr_limited(line + len, sizeof(line) - len, ":x:");
        len = string_length(line);
        copy_cstr_limited(line + len, sizeof(line) - len, uid_text);
        len = string_length(line);
        line[len++] = ':';
        line[len] = '\0';
    }
    account_append_line("/etc/group", line);

    write_cstr("useradd: created ");
    write_cstr(name);
    write_literal(" uid=", 5);
    write_u64_decimal(next_uid);
    write_literal(" (initial password: changeme)\n", 30);
}

static void cmd_passwd(const char *args) {
    static const char denied[] = "passwd: permission denied\n";
    char target[ACCOUNT_NAME_MAX];
    char self[ACCOUNT_NAME_MAX];
    char old_password[64];
    char new_password[64];
    char confirm[64];
    char salt[32];
    char stored[65];
    char computed[65];

    if (account_name_by_uid(sys_geteuid(), self, sizeof(self)) != 0) {
        copy_cstr_limited(self, sizeof(self), "unknown");
    }
    if (args[0] == '\0') {
        copy_cstr_limited(target, sizeof(target), self);
    } else {
        copy_cstr_limited(target, sizeof(target), args);
    }
    if (!strings_equal(target, self) && sys_geteuid() != 0) {
        write_literal(denied, sizeof(denied) - 1);
        return;
    }
    if (account_shadow(target, salt, sizeof(salt), stored) != 0) {
        write_cstr("passwd: unknown user ");
        write_cstr(target);
        write_literal("\n", 1);
        return;
    }

    if (strings_equal(target, self)) {
        read_password("Current password: ", old_password, sizeof(old_password));
        tpos_sha256_salted_hex(salt, old_password, computed);
        if (!strings_equal(computed, stored)) {
            write_literal("passwd: authentication failure\n", 31);
            return;
        }
    }

    read_password("New password: ", new_password, sizeof(new_password));
    read_password("Retype new password: ", confirm, sizeof(confirm));
    if (!strings_equal(new_password, confirm)) {
        write_literal("passwd: passwords do not match\n", 31);
        return;
    }

    /* Rewrite /etc/shadow with the replaced entry. */
    {
        static char db[ACCOUNT_DB_MAX];
        static char updated[ACCOUNT_DB_MAX];
        const char *cursor = db;
        uint64_t used = 0;

        if (account_read_file("/etc/shadow", db, sizeof(db)) == 0) {
            write_literal("passwd: cannot read /etc/shadow\n", 31);
            return;
        }
        tpos_sha256_salted_hex(salt, new_password, computed);
        updated[0] = '\0';
        while (*cursor != '\0') {
            char field_name[ACCOUNT_NAME_MAX];
            const char *line_start = cursor;

            cursor = account_field(cursor, field_name, sizeof(field_name));
            while (*cursor != '\0' && *cursor != '\n') {
                cursor++;
            }
            if (*cursor == '\n') {
                cursor++;
            }
            if (strings_equal(field_name, target)) {
                copy_cstr_limited(updated + used, sizeof(updated) - used, target);
                used = string_length(updated);
                updated[used++] = ':';
                updated[used] = '\0';
                copy_cstr_limited(updated + used, sizeof(updated) - used, salt);
                used = string_length(updated);
                updated[used++] = ':';
                updated[used] = '\0';
                copy_cstr_limited(updated + used, sizeof(updated) - used, computed);
                used = string_length(updated);
                updated[used++] = '\n';
                updated[used] = '\0';
            } else {
                uint64_t line_len = (uint64_t)(cursor - line_start);
                if (used + line_len + 1 < sizeof(updated)) {
                    for (uint64_t i = 0; i < line_len; i++) {
                        updated[used++] = line_start[i];
                    }
                    updated[used] = '\0';
                }
            }
        }
        if (account_write_file("/etc/shadow", updated) != 0) {
            write_literal("passwd: cannot update /etc/shadow\n", 33);
            return;
        }
    }

    write_literal("passwd: password updated\n", 25);
}

static void run_shell_command(const char *line, char *cwd, char *buffer, uint64_t buffer_len) {
    static const char help[] =
        "commands: help, pwd, cd, ls, cat, echo, clear, touch, write, append, rm, stat, cp, mv, hexdump, edit, run, ps, mem, uptime, sysinfo, usb, usbdiag, mounts, lsblk, blkread, blkwrite, usbtestwrite, writeaudit, writeunlock, whoami, id, groups, su, passwd, useradd\n";
    static const char usage_blkread[] = "usage: blkread device lba\n";
    static const char usage_blkwrite[] = "usage: blkwrite device lba text\n";
    static const char usage_cat[] = "usage: cat /path\n";
    static const char usage_cp[] = "usage: cp src dst\n";
    static const char usage_edit[] = "usage: edit /path\n";
    static const char usage_hexdump[] = "usage: hexdump /path\n";
    static const char usage_ls[] = "usage: ls [/path]\n";
    static const char usage_mv[] = "usage: mv src dst\n";
    static const char usage_rm[] = "usage: rm /path\n";
    static const char usage_run[] = "usage: run /path.elf [args...]\n";
    static const char usage_stat[] = "usage: stat /path\n";
    static const char usage_touch[] = "usage: touch /path\n";
    static const char usage_writeaudit[] = "usage: writeaudit [/mount]\n";
    static const char usage_writeunlock[] =
        "usage: writeunlock /mount bitmap_lba fat_lba directory_lba TPUSBWRITE\n";
    static const char usage_write[] = "usage: write /path text\n";
    static const char usage_append[] = "usage: append /path text\n";
    static const char cd_fail[] = "cd: failed\n";
    static const char rm_ok[] = "rm: ok\n";
    static const char rm_fail[] = "rm: failed\n";
    static const char touch_ok[] = "touch: ok\n";
    static const char touch_fail[] = "touch: failed\n";
    static const char write_ok[] = "write: ok\n";
    static const char write_fail[] = "write: failed\n";
    static const char append_ok[] = "append: ok\n";
    static const char append_fail[] = "append: failed\n";
    char path[SHELL_PATH_MAX];

    line = skip_spaces(line);
    if (line[0] == '\0') {
        return;
    }
    if (strings_equal(line, "help")) {
        write_literal(help, sizeof(help) - 1);
        return;
    }
    if (strings_equal(line, "pwd")) {
        write_cstr(cwd);
        write_literal("\n", 1);
        return;
    }
    if (strings_equal(line, "clear")) {
        for (uint64_t i = 0; i < 40; i++) {
            write_literal("\n", 1);
        }
        return;
    }
    if (strings_equal(line, "ps")) {
        cmd_ps();
        return;
    }
    if (strings_equal(line, "mem")) {
        cmd_mem();
        return;
    }
    if (strings_equal(line, "uptime")) {
        cmd_uptime();
        return;
    }
    if (strings_equal(line, "sysinfo")) {
        cmd_sysinfo();
        return;
    }
    if (strings_equal(line, "whoami")) {
        cmd_whoami();
        return;
    }
    if (strings_equal(line, "id")) {
        cmd_id();
        return;
    }
    if (strings_equal(line, "groups")) {
        cmd_groups();
        return;
    }
    if (strings_equal(line, "su")) {
        cmd_su("");
        return;
    }
    if (starts_with(line, "su ")) {
        cmd_su(skip_spaces(line + 3));
        return;
    }
    if (strings_equal(line, "passwd")) {
        cmd_passwd("");
        return;
    }
    if (starts_with(line, "passwd ")) {
        cmd_passwd(skip_spaces(line + 7));
        return;
    }
    if (starts_with(line, "useradd ")) {
        cmd_useradd(skip_spaces(line + 8));
        return;
    }
    if (strings_equal(line, "usb")) {
        cmd_usb();
        return;
    }
    if (strings_equal(line, "usbdiag")) {
        cmd_usbdiag();
        return;
    }
    if (strings_equal(line, "lsblk")) {
        cmd_lsblk();
        return;
    }
    if (strings_equal(line, "mounts")) {
        cmd_mounts();
        return;
    }
    if (strings_equal(line, "usbtestwrite")) {
        usbtestwrite_run(buffer, buffer_len, 1);
        return;
    }
    if (strings_equal(line, "writeaudit")) {
        cmd_writeaudit("/usbdisk", buffer, buffer_len);
        return;
    }
    if (starts_with(line, "writeaudit ")) {
        const char *arg = skip_spaces(line + 11);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_writeaudit, sizeof(usage_writeaudit) - 1);
            return;
        }
        cmd_writeaudit(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "writeunlock ")) {
        const char *rest = skip_spaces(line + 12);
        char mount_token[SHELL_PATH_MAX];
        char bitmap_token[24];
        char fat_token[24];
        char dir_token[24];
        char confirm_token[24];
        uint64_t bitmap_lba;
        uint64_t fat_lba;
        uint64_t directory_lba;

        if (read_token(mount_token, sizeof(mount_token), rest, &rest) == 0 ||
            read_token(bitmap_token, sizeof(bitmap_token), rest, &rest) == 0 ||
            read_token(fat_token, sizeof(fat_token), rest, &rest) == 0 ||
            read_token(dir_token, sizeof(dir_token), rest, &rest) == 0 ||
            read_token(confirm_token, sizeof(confirm_token), rest, &rest) == 0 ||
            rest[0] != '\0' ||
            resolve_path(cwd, mount_token, path, sizeof(path)) != 0 ||
            parse_u64(bitmap_token, &bitmap_lba) != 0 ||
            parse_u64(fat_token, &fat_lba) != 0 ||
            parse_u64(dir_token, &directory_lba) != 0 ||
            !strings_equal(confirm_token, "TPUSBWRITE")) {
            write_literal(usage_writeunlock, sizeof(usage_writeunlock) - 1);
            return;
        }
        cmd_writeunlock(path, bitmap_lba, fat_lba, directory_lba);
        return;
    }
    if (starts_with(line, "blkread ")) {
        const char *dev_start = skip_spaces(line + 8);
        const char *dev_end = find_space(dev_start);
        const char *lba_start = skip_spaces(dev_end);
        const char *lba_end = find_space(lba_start);
        char dev_token[24];
        char lba_token[24];
        uint64_t device;
        uint64_t lba;

        if (dev_start[0] == '\0' || lba_start[0] == '\0' ||
            copy_token(dev_token, sizeof(dev_token), dev_start, dev_end) == 0 ||
            copy_token(lba_token, sizeof(lba_token), lba_start, lba_end) == 0 ||
            parse_u64(dev_token, &device) != 0 || parse_u64(lba_token, &lba) != 0) {
            write_literal(usage_blkread, sizeof(usage_blkread) - 1);
            return;
        }
        cmd_blkread(device, lba, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "blkwrite ")) {
        const char *dev_start = skip_spaces(line + 9);
        const char *dev_end = find_space(dev_start);
        const char *lba_start = skip_spaces(dev_end);
        const char *lba_end = find_space(lba_start);
        const char *text = skip_spaces(lba_end);
        char dev_token[24];
        char lba_token[24];
        uint64_t device;
        uint64_t lba;

        if (dev_start[0] == '\0' || lba_start[0] == '\0' || text[0] == '\0' ||
            copy_token(dev_token, sizeof(dev_token), dev_start, dev_end) == 0 ||
            copy_token(lba_token, sizeof(lba_token), lba_start, lba_end) == 0 ||
            parse_u64(dev_token, &device) != 0 || parse_u64(lba_token, &lba) != 0) {
            write_literal(usage_blkwrite, sizeof(usage_blkwrite) - 1);
            return;
        }
        cmd_blkwrite(device, lba, text, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "echo")) {
        const char *text = skip_spaces(line + 4);
        write_cstr(text);
        write_literal("\n", 1);
        return;
    }
    if (strings_equal(line, "cd")) {
        if (copy_cstr_limited(cwd, SHELL_PATH_MAX, "/") != 0) {
            write_literal(cd_fail, sizeof(cd_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "cd ")) {
        const char *arg = skip_spaces(line + 3);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0 ||
            !path_is_dir(path) || copy_cstr_limited(cwd, SHELL_PATH_MAX, path) != 0) {
            write_literal(cd_fail, sizeof(cd_fail) - 1);
        }
        return;
    }
    if (strings_equal(line, "ls")) {
        cmd_ls(cwd);
        return;
    }
    if (starts_with(line, "ls ")) {
        const char *arg = skip_spaces(line + 3);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_ls, sizeof(usage_ls) - 1);
            return;
        }
        cmd_ls(path);
        return;
    }
    if (starts_with(line, "cat ")) {
        const char *arg = skip_spaces(line + 4);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_cat, sizeof(usage_cat) - 1);
            return;
        }
        cmd_cat(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "hexdump ")) {
        const char *arg = skip_spaces(line + 8);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_hexdump, sizeof(usage_hexdump) - 1);
            return;
        }
        cmd_hexdump(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "edit ")) {
        const char *arg = skip_spaces(line + 5);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_edit, sizeof(usage_edit) - 1);
            return;
        }
        cmd_edit(path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "touch ")) {
        const char *arg = skip_spaces(line + 6);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_touch, sizeof(usage_touch) - 1);
            return;
        }
        if (write_file_text(path, "", OPEN_CREATE | OPEN_APPEND) == 0) {
            write_literal(touch_ok, sizeof(touch_ok) - 1);
        } else {
            write_literal(touch_fail, sizeof(touch_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "write ")) {
        const char *path_start = skip_spaces(line + 6);
        const char *path_end = find_space(path_start);
        const char *text = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' || text[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_write, sizeof(usage_write) - 1);
            return;
        }
        if (write_file_text(path, text, OPEN_CREATE | OPEN_TRUNC) == 0) {
            write_literal(write_ok, sizeof(write_ok) - 1);
        } else {
            write_literal(write_fail, sizeof(write_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "append ")) {
        const char *path_start = skip_spaces(line + 7);
        const char *path_end = find_space(path_start);
        const char *text = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' || text[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_append, sizeof(usage_append) - 1);
            return;
        }
        if (write_file_text(path, text, OPEN_CREATE | OPEN_APPEND) == 0) {
            write_literal(append_ok, sizeof(append_ok) - 1);
        } else {
            write_literal(append_fail, sizeof(append_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "cp ")) {
        const char *src_start = skip_spaces(line + 3);
        const char *src_end = find_space(src_start);
        const char *dst_start = skip_spaces(src_end);
        const char *dst_end = find_space(dst_start);
        char src_token[SHELL_PATH_MAX];
        char dst_token[SHELL_PATH_MAX];
        char dst_path[SHELL_PATH_MAX];

        if (src_start[0] == '\0' || dst_start[0] == '\0' ||
            copy_token(src_token, sizeof(src_token), src_start, src_end) == 0 ||
            copy_token(dst_token, sizeof(dst_token), dst_start, dst_end) == 0 ||
            resolve_path(cwd, src_token, path, sizeof(path)) != 0 ||
            resolve_path(cwd, dst_token, dst_path, sizeof(dst_path)) != 0) {
            write_literal(usage_cp, sizeof(usage_cp) - 1);
            return;
        }
        cmd_cp(path, dst_path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "mv ")) {
        const char *src_start = skip_spaces(line + 3);
        const char *src_end = find_space(src_start);
        const char *dst_start = skip_spaces(src_end);
        const char *dst_end = find_space(dst_start);
        char src_token[SHELL_PATH_MAX];
        char dst_token[SHELL_PATH_MAX];
        char dst_path[SHELL_PATH_MAX];

        if (src_start[0] == '\0' || dst_start[0] == '\0' ||
            copy_token(src_token, sizeof(src_token), src_start, src_end) == 0 ||
            copy_token(dst_token, sizeof(dst_token), dst_start, dst_end) == 0 ||
            resolve_path(cwd, src_token, path, sizeof(path)) != 0 ||
            resolve_path(cwd, dst_token, dst_path, sizeof(dst_path)) != 0) {
            write_literal(usage_mv, sizeof(usage_mv) - 1);
            return;
        }
        cmd_mv(path, dst_path, buffer, buffer_len);
        return;
    }
    if (starts_with(line, "rm ")) {
        const char *arg = skip_spaces(line + 3);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_rm, sizeof(usage_rm) - 1);
            return;
        }
        if (sys_unlink(path) == 0) {
            write_literal(rm_ok, sizeof(rm_ok) - 1);
        } else {
            write_literal(rm_fail, sizeof(rm_fail) - 1);
        }
        return;
    }
    if (starts_with(line, "stat ")) {
        const char *arg = skip_spaces(line + 5);
        char token[SHELL_PATH_MAX];
        if (read_token(token, sizeof(token), arg, 0) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_stat, sizeof(usage_stat) - 1);
            return;
        }
        cmd_stat(path);
        return;
    }
    if (starts_with(line, "run ")) {
        const char *path_start = skip_spaces(line + 4);
        const char *path_end = find_space(path_start);
        const char *args = skip_spaces(path_end);
        char token[SHELL_PATH_MAX];
        if (path_start[0] == '\0' ||
            copy_token(token, sizeof(token), path_start, path_end) == 0 ||
            resolve_path(cwd, token, path, sizeof(path)) != 0) {
            write_literal(usage_run, sizeof(usage_run) - 1);
            return;
        }
        cmd_run(path, args);
        return;
    }

    {
        static const char not_found[] = "sh: command not found: ";
        const char *end = find_space(line);
        write_literal(not_found, sizeof(not_found) - 1);
        write_literal(line, (uint64_t)(end - line));
        write_literal("\n", 1);
    }
}

static void run_shell_script_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: shell script ok\n";
    static const char fail[] = "shell.elf: shell script failed\n";
    char cwd[SHELL_PATH_MAX];

    if (copy_cstr_limited(cwd, sizeof(cwd), "/") != 0) {
        sys_exit(20);
    }

    run_shell_command("pwd", cwd, buffer, buffer_len);
    run_shell_command("write script.txt alpha", cwd, buffer, buffer_len);
    run_shell_command("append script.txt beta", cwd, buffer, buffer_len);
    run_shell_command("stat script.txt", cwd, buffer, buffer_len);
    run_shell_command("cat script.txt", cwd, buffer, buffer_len);
    run_shell_command("cp script.txt copy.txt", cwd, buffer, buffer_len);

    if (!read_file_equals("/script.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    if (!read_file_equals("/copy.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    run_shell_command("mv copy.txt moved.txt", cwd, buffer, buffer_len);
    if (sys_open("/copy.txt") != (uint64_t)-1 ||
        !read_file_equals("/moved.txt", buffer, buffer_len, "alphabeta")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("hexdump moved.txt", cwd, buffer, buffer_len);

    run_shell_command("touch empty.txt", cwd, buffer, buffer_len);
    if (!read_file_equals("/empty.txt", buffer, buffer_len, "")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    run_shell_command("cd dev", cwd, buffer, buffer_len);
    if (!strings_equal(cwd, "/dev")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("ls", cwd, buffer, buffer_len);
    run_shell_command("cd ..", cwd, buffer, buffer_len);
    if (!strings_equal(cwd, "/")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }
    run_shell_command("ls /bin", cwd, buffer, buffer_len);
    run_shell_command("run /bin/hello.elf one two", cwd, buffer, buffer_len);
#ifdef TANGPINGOS_TEST_USER_PROGRAMS
    run_shell_command("run /bin/ls.elf /bin", cwd, buffer, buffer_len);
    run_shell_command("run /bin/cat.elf /hello.txt", cwd, buffer, buffer_len);
#endif
    for (uint64_t i = 0; i < 8; i++) {
        sys_yield();
    }
    run_shell_command("ps", cwd, buffer, buffer_len);
    run_shell_command("mem", cwd, buffer, buffer_len);
    run_shell_command("uptime", cwd, buffer, buffer_len);
    run_shell_command("sysinfo", cwd, buffer, buffer_len);
    run_shell_command("usb", cwd, buffer, buffer_len);
    run_shell_command("mounts", cwd, buffer, buffer_len);
    run_shell_command("cat \"/boot/very long filename.txt\"", cwd, buffer, buffer_len);

    run_shell_command("rm script.txt", cwd, buffer, buffer_len);
    run_shell_command("rm empty.txt", cwd, buffer, buffer_len);
    run_shell_command("rm moved.txt", cwd, buffer, buffer_len);
    if (sys_open("/script.txt") != (uint64_t)-1 ||
        sys_open("/empty.txt") != (uint64_t)-1 ||
        sys_open("/moved.txt") != (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(20);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_block_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: block device read/write ok\n";
    static const char fail[] = "shell.elf: block device test failed\n";
    static const char message[] = "TangPingOS block layer works";
    struct block_device_info info;

    if (buffer_len < 512 || sys_block_info(0, &info) != 0 ||
        info.block_size != 512 || info.block_count < 4 || info.writable == 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }

    clear_buffer(buffer, 512);
    for (uint64_t i = 0; i < sizeof(message) - 1; i++) {
        buffer[i] = message[i];
    }
    if (sys_block_write(0, 3, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }

    clear_buffer(buffer, 512);
    if (sys_block_read(0, 3, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(21);
    }
    for (uint64_t i = 0; i < sizeof(message) - 1; i++) {
        if (buffer[i] != message[i]) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(21);
        }
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_virtio_block_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: exFAT boot sector read ok\n";
    static const char fail[] = "shell.elf: virtio block device test failed\n";
    static const char expected[] = "EXFAT   ";
    struct block_device_info info;

    if (sys_block_info(1, &info) != 0) {
        return;
    }
    if (sys_block_info(2, &info) != 0 || buffer_len < 512 ||
        sys_block_read(2, 0, buffer, 1) != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(22);
    }

    for (uint64_t i = 0; i < sizeof(expected) - 1; i++) {
        if (buffer[3 + i] != expected[i]) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(22);
        }
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_mount_info_self_test(void) {
    static const char ok[] = "shell.elf: mount table ok\n";
    static const char fail[] = "shell.elf: mount table failed\n";
    struct mount_info info;
    struct block_device_info block;

    if (sys_mount_info(0, &info) != 0 || !strings_equal(info.name, "devfs") ||
        !strings_equal(info.path, "/dev") || info.writable != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }
    if (sys_mount_info(1, &info) != 0 || !strings_equal(info.name, "ramfs") ||
        !strings_equal(info.path, "/") || info.writable != 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }
    if (sys_block_info(2, &block) == 0 &&
        (sys_mount_info(3, &info) != 0 || !strings_equal(info.name, "blkfs") ||
         !strings_equal(info.path, "/usb") || !strings_equal(info.source, block.name))) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }
    if (sys_block_info(3, &block) == 0 &&
        (sys_mount_info(4, &info) != 0 || !strings_equal(info.name, "blkfs") ||
         !strings_equal(info.path, "/boot") || !strings_equal(info.source, block.name))) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(23);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_block_mount_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: block mount read ok\n";
    static const char fail[] = "shell.elf: block mount read failed\n";
    static const char info_prefix[] = "TangPingOS block-backed mount";
    static const char info_fs[] = "detected_fs: exfat";
    static const char info_bitmap[] = "allocation_bitmap: present";
    static const char info_bitmap_scanned[] = "bitmap_scanned_clusters: 1024";
    static const char info_bitmap_used[] = "bitmap_used_clusters: 10";
    static const char info_bitmap_free[] = "bitmap_free_clusters: 1014";
    static const char info_bitmap_first_free[] = "bitmap_first_free_cluster: 12";
    static const char info_plan_ready[] = "allocation_plan_ready: 1";
    static const char info_plan_clusters[] = "allocation_plan_clusters: 12 13 14";
    static const char info_plan_chain[] = "allocation_plan_fat_chain: 12->13->14->EOF";
    static const char info_dir_plan_path[] = "directory_plan_path: /NEW.TXT";
    static const char info_dir_plan_entries[] = "directory_plan_required_entries: 3";
    static const char info_dir_plan_ready[] = "directory_plan_ready: 1";
    static const char info_dir_plan_cluster[] = "directory_plan_cluster: 2";
    static const char info_dir_plan_slot[] = "directory_plan_first_slot: 6";
    static const char info_dir_plan_lba[] = "directory_plan_entry_lba: 192";
    static const char info_dir_entry_ready[] = "directory_entry_plan_ready: 1";
    static const char info_dir_entry_cluster[] = "directory_entry_plan_first_cluster: 12";
    static const char info_dir_entry_size[] = "directory_entry_plan_file_size: 12288";
    static const char info_dir_entry0[] = "directory_entry0_hex: 8502";
    static const char info_dir_entry1[] = "directory_entry1_hex: c0000007";
    static const char info_dir_entry2[] = "directory_entry2_hex: c1004e00450057002e00540058005400";
    static const char info_tx_ready[] = "transaction_plan_ready: 1";
    static const char info_tx_writes[] = "transaction_plan_write_count: 3";
    static const char info_tx_bitmap_lba[] = "transaction_bitmap_lba: 256";
    static const char info_tx_bitmap_offset[] = "transaction_bitmap_byte_offset: 1";
    static const char info_tx_bitmap_old[] = "transaction_bitmap_old_hex: 03";
    static const char info_tx_bitmap_new[] = "transaction_bitmap_new_hex: 1f";
    static const char info_tx_fat_lba[] = "transaction_fat_lba: 128";
    static const char info_tx_fat_plan[] = "transaction_fat_plan: 12@48:0->13 13@52:0->14 14@56:0->EOF";
    static const char info_tx_dir_lba[] = "transaction_directory_lba: 192";
    static const char info_tx_dir_offset[] = "transaction_directory_byte_offset: 192";
    static const char info_tx_dir_count[] = "transaction_directory_byte_count: 96";
    static const char info_patch_ready[] = "patched_transaction_ready: 1";
    static const char info_patch_bitmap[] = "patched_bitmap_value_hex: 1f";
    static const char info_patch_fat[] = "patched_fat_chain: 12->13->14->EOF";
    static const char info_patch_checksum[] = "patched_directory_checksum: 58755";
    static const char info_patch_cluster[] = "patched_directory_first_cluster: 12";
    static const char info_patch_size[] = "patched_directory_file_size: 12288";
    static const char info_patch_name_len[] = "patched_directory_name_length: 7";
    static const char info_patch_name_matches[] = "patched_directory_name_matches: 1";
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    static const char info_commit_supported[] = "commit_supported: 1";
    static const char info_commit_attempted[] = "commit_attempted: 1";
    static const char info_commit_ready[] = "commit_ready: 1";
    static const char info_commit_writes[] = "commit_write_count: 3";
    static const char info_commit_verified[] = "commit_verified: 1";
    static const char info_write_enabled[] = "write_enabled: 1";
    static const char info_write_policy[] = "write_policy: writable_test_build";
    static const char info_write_policy_allows[] = "write_policy_allows_commit: 1";
    static const char info_write_blocker[] = "write_blocker: none";
#else
    static const char info_commit_supported[] = "commit_supported: 0";
    static const char info_commit_attempted[] = "commit_attempted: 0";
    static const char info_commit_ready[] = "commit_ready: 0";
    static const char info_commit_writes[] = "commit_write_count: 0";
    static const char info_commit_verified[] = "commit_verified: 0";
    static const char info_write_enabled[] = "write_enabled: 0";
    static const char info_write_policy[] = "write_policy: readonly_non_test_media";
    static const char info_write_policy_allows[] = "write_policy_allows_commit: 0";
    static const char info_write_blocker[] = "write_blocker: not_write_whitelisted";
#endif
    static const char info_write_candidate[] = "write_candidate_ready: 1";
    static const char info_audit_volume[] = "write_audit_volume_bounds: 1";
    static const char info_audit_fat[] = "write_audit_fat_bounds: 1";
    static const char info_audit_heap[] = "write_audit_heap_bounds: 1";
    static const char info_audit_root[] = "write_audit_root_bounds: 1";
    static const char info_audit_bitmap[] = "write_audit_bitmap_bounds: 1";
    static const char info_audit_upcase[] = "write_audit_upcase_bounds: 1";
    static const char info_active_fat[] = "active_fat: 0";
    static const char info_volume_dirty[] = "volume_dirty: 0";
    static const char info_media_failure[] = "media_failure: 0";
    static const char info_boot_checksum_present[] = "boot_checksum_present: 1";
    static const char info_boot_checksum_valid[] = "boot_checksum_valid: 1";
    static const char info_backup_boot_present[] = "backup_boot_sector_present: 1";
    static const char info_backup_boot_matches[] = "backup_boot_sector_matches: 1";
    static const char info_backup_checksum_present[] = "backup_boot_checksum_present: 1";
    static const char info_backup_checksum_valid[] = "backup_boot_checksum_valid: 1";
    static const char info_real_write_ready[] = "real_write_prereq_ready: 1";
    static const char info_real_write_blocker[] = "real_write_blocker: none";
    static const char info_real_dry_action[] = "real_write_dry_run_action: create_root_file";
    static const char info_real_dry_path[] = "real_write_dry_run_path: /NEW.TXT";
    static const char info_real_dry_ready[] = "real_write_dry_run_ready: 1";
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    static const char info_real_dry_policy[] = "real_write_dry_run_policy_allows: 1";
#else
    static const char info_real_dry_policy[] = "real_write_dry_run_policy_allows: 0";
#endif
    static const char info_real_dry_blocker[] = "real_write_dry_run_blocker: none";
    static const char info_real_dry_writes[] = "real_write_dry_run_write_count: 3";
    static const char info_real_dry_lbas[] = "real_write_dry_run_lbas: 256 128 192";
    static const char info_real_dry_clusters[] = "real_write_dry_run_clusters: 12 13 14";
    static const char info_real_dry_note[] =
        "real_write_dry_run_note: dry_run_only_until_manual_real_media_unlock";
    static const char sector_signature[] = "EXFAT   ";
    static const char exfat_file_name[] = "HELLO.TXT";
    static const char exfat_file_content[] = "Hello from exFAT root file.\n";
    static const char exfat_chain_name[] = "CHAIN.TXT";
    static const char exfat_chain_tail[] = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    static const char exfat_late_name[] = "LATE.TXT";
    static const char exfat_late_content[] = "Late root chain file.\n";
    static const char exfat_dir_name[] = "DIR";
    static const char exfat_inner_name[] = "INNER.TXT";
    static const char exfat_inner_content[] = "Hello from an exFAT subdir.\n";
    struct block_device_info block;
    struct dirent dirent;

    if (sys_block_info(2, &block) != 0) {
        return;
    }
    if (buffer_len < 512) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    uint64_t fd = sys_open("/usb/info.txt");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    uint64_t read_len = 0;
    while (read_len + 1 < buffer_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, buffer_len - 1 - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(24);
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);
    if (read_len == 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    buffer[read_len] = '\0';
    if (!starts_with(buffer, info_prefix)) {
        write_literal("block mount step: info prefix\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_fs)) {
        write_literal("block mount step: info fs\n", 26);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_bitmap) ||
        !contains_text(buffer, info_bitmap_scanned) ||
        !contains_text(buffer, info_bitmap_used) ||
        !contains_text(buffer, info_bitmap_free) ||
        !contains_text(buffer, info_bitmap_first_free)) {
        write_literal("block mount step: bitmap info\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_plan_ready)) {
        write_literal("block mount step: allocation plan ready\n", 40);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_plan_clusters)) {
        write_literal("block mount step: allocation plan clusters\n", 43);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_plan_chain)) {
        write_literal("block mount step: allocation plan chain\n", 40);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_dir_plan_path) ||
        !contains_text(buffer, info_dir_plan_entries) ||
        !contains_text(buffer, info_dir_plan_ready) ||
        !contains_text(buffer, info_dir_plan_cluster) ||
        !contains_text(buffer, info_dir_plan_slot) ||
        !contains_text(buffer, info_dir_plan_lba)) {
        write_literal("block mount step: directory plan\n", 33);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_dir_entry_ready) ||
        !contains_text(buffer, info_dir_entry_cluster) ||
        !contains_text(buffer, info_dir_entry_size) ||
        !contains_text(buffer, info_dir_entry0) ||
        !contains_text(buffer, info_dir_entry1) ||
        !contains_text(buffer, info_dir_entry2)) {
        write_literal("block mount step: directory entry plan\n", 39);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_tx_ready) ||
        !contains_text(buffer, info_tx_writes) ||
        !contains_text(buffer, info_tx_bitmap_lba) ||
        !contains_text(buffer, info_tx_bitmap_offset) ||
        !contains_text(buffer, info_tx_bitmap_old) ||
        !contains_text(buffer, info_tx_bitmap_new) ||
        !contains_text(buffer, info_tx_fat_lba) ||
        !contains_text(buffer, info_tx_fat_plan) ||
        !contains_text(buffer, info_tx_dir_lba) ||
        !contains_text(buffer, info_tx_dir_offset) ||
        !contains_text(buffer, info_tx_dir_count)) {
        write_literal("block mount step: transaction plan\n", 36);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_patch_ready) ||
        !contains_text(buffer, info_patch_bitmap) ||
        !contains_text(buffer, info_patch_fat) ||
        !contains_text(buffer, info_patch_checksum) ||
        !contains_text(buffer, info_patch_cluster) ||
        !contains_text(buffer, info_patch_size) ||
        !contains_text(buffer, info_patch_name_len) ||
        !contains_text(buffer, info_patch_name_matches)) {
        write_literal("block mount step: transaction dry-run\n", 38);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (!contains_text(buffer, info_commit_supported) ||
        !contains_text(buffer, info_commit_attempted) ||
        !contains_text(buffer, info_commit_ready) ||
        !contains_text(buffer, info_commit_writes) ||
        !contains_text(buffer, info_commit_verified) ||
        !contains_text(buffer, info_write_enabled) ||
        !contains_text(buffer, info_write_policy) ||
        !contains_text(buffer, info_write_candidate) ||
        !contains_text(buffer, info_write_policy_allows) ||
        !contains_text(buffer, info_write_blocker) ||
        !contains_text(buffer, info_audit_volume) ||
        !contains_text(buffer, info_audit_fat) ||
        !contains_text(buffer, info_audit_heap) ||
        !contains_text(buffer, info_audit_root) ||
        !contains_text(buffer, info_audit_bitmap) ||
        !contains_text(buffer, info_audit_upcase) ||
        !contains_text(buffer, info_active_fat) ||
        !contains_text(buffer, info_volume_dirty) ||
        !contains_text(buffer, info_media_failure) ||
        !contains_text(buffer, info_boot_checksum_present) ||
        !contains_text(buffer, info_boot_checksum_valid) ||
        !contains_text(buffer, info_backup_boot_present) ||
        !contains_text(buffer, info_backup_boot_matches) ||
        !contains_text(buffer, info_backup_checksum_present) ||
        !contains_text(buffer, info_backup_checksum_valid) ||
        !contains_text(buffer, info_real_write_ready) ||
        !contains_text(buffer, info_real_write_blocker) ||
        !contains_text(buffer, info_real_dry_action) ||
        !contains_text(buffer, info_real_dry_path) ||
        !contains_text(buffer, info_real_dry_ready) ||
        !contains_text(buffer, info_real_dry_policy) ||
        !contains_text(buffer, info_real_dry_blocker) ||
        !contains_text(buffer, info_real_dry_writes) ||
        !contains_text(buffer, info_real_dry_lbas) ||
        !contains_text(buffer, info_real_dry_clusters) ||
        !contains_text(buffer, info_real_dry_note)) {
        write_literal("block mount step: transaction commit gate\n", 41);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    fd = sys_open("/usb/sector0.bin");
    if (fd == (uint64_t)-1) {
        write_literal("block mount step: sector open\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, 512);
    sys_close(fd);
    if (read_len != 512) {
        write_literal("block mount step: sector read\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    for (uint64_t i = 0; i < sizeof(sector_signature) - 1; i++) {
        if (buffer[3 + i] != sector_signature[i]) {
            write_literal("block mount step: sector signature\n", 35);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(24);
        }
    }

    if ((uint8_t)buffer[510] != 0x55 || (uint8_t)buffer[511] != 0xaa) {
        write_literal("block mount step: sector magic\n", 31);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    int found_exfat_file = 0;
    int found_chain_file = 0;
    int found_late_file = 0;
    int found_dir = 0;
    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents("/usb", i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal("block mount step: root list\n", 29);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(24);
        }
        if (strings_equal(dirent.name, exfat_file_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(exfat_file_content) - 1) {
            found_exfat_file = 1;
        }
        if (strings_equal(dirent.name, exfat_chain_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == 4096 + sizeof(exfat_chain_tail) - 1) {
            found_chain_file = 1;
        }
        if (strings_equal(dirent.name, exfat_late_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(exfat_late_content) - 1) {
            found_late_file = 1;
        }
        if (strings_equal(dirent.name, exfat_dir_name) &&
            dirent.type == DIRENT_TYPE_DIR) {
            found_dir = 1;
        }
    }
    if (!found_exfat_file || !found_chain_file || !found_late_file || !found_dir) {
        write_literal("block mount step: root entries\n", 32);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    fd = sys_open("/usb/HELLO.TXT");
    if (fd == (uint64_t)-1) {
        write_literal("block mount step: hello open\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(exfat_file_content) - 1) {
        write_literal("block mount step: hello read\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, exfat_file_content)) {
        write_literal("block mount step: hello content\n", 32);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    fd = sys_open("/usb/CHAIN.TXT");
    if (fd == (uint64_t)-1) {
        write_literal("block mount step: chain open\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, 1);
    if (read_len != 1 || buffer[0] != 'A') {
        sys_close(fd);
        write_literal("block mount step: chain head\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    if (sys_lseek(fd, 4096, SEEK_SET) != 4096) {
        sys_close(fd);
        write_literal("block mount step: chain seek\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, sizeof(exfat_chain_tail) - 1);
    sys_close(fd);
    if (read_len != sizeof(exfat_chain_tail) - 1) {
        write_literal("block mount step: chain tail read\n", 34);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, exfat_chain_tail)) {
        write_literal("block mount step: chain tail content\n", 37);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    fd = sys_open("/usb/LATE.TXT");
    if (fd == (uint64_t)-1) {
        write_literal("block mount step: late open\n", 28);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(exfat_late_content) - 1) {
        write_literal("block mount step: late read\n", 28);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, exfat_late_content)) {
        write_literal("block mount step: late content\n", 31);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    int found_inner_file = 0;
    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents("/usb/DIR", i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal("block mount step: inner list\n", 30);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(24);
        }
        if (strings_equal(dirent.name, exfat_inner_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(exfat_inner_content) - 1) {
            found_inner_file = 1;
        }
    }
    if (!found_inner_file) {
        write_literal("block mount step: inner entry\n", 31);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    fd = sys_open("/usb/DIR/INNER.TXT");
    if (fd == (uint64_t)-1) {
        write_literal("block mount step: inner open\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(exfat_inner_content) - 1) {
        write_literal("block mount step: inner read\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, exfat_inner_content)) {
        write_literal("block mount step: inner content\n", 33);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(24);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_fat32_mount_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: FAT32 mount read ok\n";
    static const char fail[] = "shell.elf: FAT32 mount read failed\n";
    static const char info_prefix[] = "TangPingOS block-backed mount";
    static const char info_fs[] = "detected_fs: fat32";
    static const char info_bps[] = "bytes_per_sector: 512";
    static const char info_spc[] = "sectors_per_cluster: 1";
    static const char info_root[] = "root_dir_cluster: 2";
    static const char info_label[] = "volume_label: TANGBOOT";
    static const char readme_name[] = "README.TXT";
    static const char kernel_name[] = "KERNEL.TXT";
    static const char efi_name[] = "EFI";
    static const char long_name_file[] = "very long filename.txt";
    static const char chain_name[] = "CHAIN.TXT";
    static const char late_name[] = "LATE.TXT";
    static const char boot_name[] = "BOOT";
    static const char bootx64_name[] = "BOOTX64.EFI";
    static const char readme_content[] = "Hello from FAT32 boot partition.\n";
    static const char kernel_content[] = "This FAT32 sample stands in for a UEFI ESP.\n";
    static const char long_name_content[] = "FAT32 multi-entry long file names are visible now.\n";
    static const char bootx64_content[] = "TangPingOS can now walk FAT32 EFI/BOOT.\n";
    static const char chain_prefix[] = "FAT32 chained file cluster 10 begins here.\n";
    static const char chain_middle[] = "FAT32 chained file cluster 11 continues here.\n";
    static const char chain_suffix[] = "FAT32 chained file end.\n";
    static const char late_content[] = "FAT32 late root entry from the second directory cluster.\n";
    struct block_device_info block;
    struct dirent dirent;
    uint64_t fd;
    uint64_t read_len;
    int found_readme = 0;
    int found_kernel = 0;
    int found_efi = 0;
    int found_long_name = 0;
    int found_chain = 0;
    int found_late = 0;
    int found_boot = 0;
    int found_bootx64 = 0;

    if (sys_block_info(3, &block) != 0) {
        return;
    }
    if (buffer_len < 512) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    fd = sys_open("/boot/info.txt");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    read_len = 0;
    while (read_len + 1 < buffer_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, buffer_len - 1 - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(27);
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);
    if (read_len == 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    buffer[read_len] = '\0';
    if (!starts_with(buffer, info_prefix) ||
        !contains_text(buffer, info_fs) ||
        !contains_text(buffer, info_bps) ||
        !contains_text(buffer, info_spc) ||
        !contains_text(buffer, info_root) ||
        !contains_text(buffer, info_label)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents("/boot", i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(27);
        }
        if (strings_equal(dirent.name, readme_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(readme_content) - 1) {
            found_readme = 1;
        }
        if (strings_equal(dirent.name, kernel_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(kernel_content) - 1) {
            found_kernel = 1;
        }
        if (strings_equal(dirent.name, efi_name) &&
            dirent.type == DIRENT_TYPE_DIR) {
            found_efi = 1;
        }
        if (strings_equal(dirent.name, long_name_file) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(long_name_content) - 1) {
            found_long_name = 1;
        }
        if (strings_equal(dirent.name, chain_name) &&
            dirent.type == DIRENT_TYPE_FILE) {
            found_chain = 1;
        }
        if (strings_equal(dirent.name, late_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(late_content) - 1) {
            found_late = 1;
        }
    }
    if (!found_readme || !found_kernel || !found_efi || !found_long_name ||
        !found_chain || !found_late) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents("/boot/EFI", i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(27);
        }
        if (strings_equal(dirent.name, boot_name) &&
            dirent.type == DIRENT_TYPE_DIR) {
            found_boot = 1;
        }
    }
    if (!found_boot) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    for (uint64_t i = 0; i < 16; i++) {
        uint64_t result = sys_getdents("/boot/EFI/BOOT", i, &dirent);
        if (result == 0) {
            break;
        }
        if (result == (uint64_t)-1) {
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(27);
        }
        if (strings_equal(dirent.name, bootx64_name) &&
            dirent.type == DIRENT_TYPE_FILE &&
            dirent.size == sizeof(bootx64_content) - 1) {
            found_bootx64 = 1;
        }
    }
    if (!found_bootx64) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    fd = sys_open("/boot/README.TXT");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(readme_content) - 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, readme_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    fd = sys_open("/boot/KERNEL.TXT");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(kernel_content) - 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, kernel_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    fd = sys_open("/boot/very long filename.txt");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(long_name_content) - 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, long_name_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    fd = sys_open("/boot/EFI/BOOT/BOOTX64.EFI");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    if (read_len != sizeof(bootx64_content) - 1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }
    buffer[read_len] = '\0';
    if (!strings_equal(buffer, bootx64_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    if (!read_file_matches_segments("/boot/CHAIN.TXT", buffer, buffer_len,
                                    chain_prefix, 'A', 470, chain_middle,
                                    'B', 180, chain_suffix) ||
        !read_file_equals("/boot/LATE.TXT", buffer, buffer_len, late_content) ||
        !read_file_equals("/boot/readme.txt", buffer, buffer_len, readme_content) ||
        !read_file_matches_segments("/boot/chain.txt", buffer, buffer_len,
                                    chain_prefix, 'A', 470, chain_middle,
                                    'B', 180, chain_suffix) ||
        !read_file_equals("/boot/late.txt", buffer, buffer_len, late_content) ||
        !read_file_equals("/boot/efi/boot/bootx64.efi", buffer, buffer_len,
                          bootx64_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(27);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_usb_mount_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: USB BOT block mount read ok\n";
    static const char fail[] = "shell.elf: USB BOT block mount failed\n";
    static const char path[] = "/usbdisk/HELLO.TXT";
    static const char expected[] = "Hello from USB BOT exFAT.\n";
    static const char info_prefix[] = "TangPingOS block-backed mount";
    static const char info_fs[] = "detected_fs: exfat";
    static const char info_write_enabled[] = "write_enabled: 1";
    static const char info_write_policy[] = "write_policy: writable_test_usb";
    static const char info_write_candidate[] = "write_candidate_ready: 1";
    static const char info_write_policy_allows[] = "write_policy_allows_commit: 1";
    static const char info_write_blocker[] = "write_blocker: none";
    static const char info_manual_attempted[] = "manual_unlock_attempted: 0";
    static const char info_manual_ready[] = "manual_unlock_ready: 0";
    static const char info_manual_blocker[] = "manual_unlock_blocker: none";
    static const char info_real_write_ready[] = "real_write_prereq_ready: 1";
    static const char info_real_write_blocker[] = "real_write_blocker: none";
    static const char info_real_dry_ready[] = "real_write_dry_run_ready: 1";
    static const char info_real_dry_policy[] = "real_write_dry_run_policy_allows: 1";
    static const char info_real_dry_lbas[] = "real_write_dry_run_lbas: 256 128 192";
    static const char info_real_dry_note[] =
        "real_write_dry_run_note: dry_run_only_until_manual_real_media_unlock";
    static const char info_commit_ready[] = "commit_ready: 1";
    uint64_t fd = sys_open(path);
    uint64_t read_len = 0;

    if (fd == (uint64_t)-1) {
        return;
    }
    sys_close(fd);

    if (buffer_len < sizeof(expected) || !read_file_equals(path, buffer, buffer_len, expected)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(29);
    }

    fd = sys_open("/usbdisk/info.txt");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(29);
    }
    while (read_len + 1 < buffer_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, buffer_len - 1 - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(29);
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (!starts_with(buffer, info_prefix) ||
        !contains_text(buffer, info_fs) ||
        !contains_text(buffer, info_write_enabled) ||
        !contains_text(buffer, info_write_policy) ||
        !contains_text(buffer, info_write_candidate) ||
        !contains_text(buffer, info_write_policy_allows) ||
        !contains_text(buffer, info_write_blocker) ||
        !contains_text(buffer, info_manual_attempted) ||
        !contains_text(buffer, info_manual_ready) ||
        !contains_text(buffer, info_manual_blocker) ||
        !contains_text(buffer, info_real_write_ready) ||
        !contains_text(buffer, info_real_write_blocker) ||
        !contains_text(buffer, info_real_dry_ready) ||
        !contains_text(buffer, info_real_dry_policy) ||
        !contains_text(buffer, info_real_dry_lbas) ||
        !contains_text(buffer, info_real_dry_note) ||
        !contains_text(buffer, info_commit_ready)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(29);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_writeaudit_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: writeaudit self-test ok\n";
    static const char fail[] = "shell.elf: writeaudit self-test failed\n";
    char info_path[SHELL_PATH_MAX];

    if (read_mount_info_text("/usbdisk", info_path, sizeof(info_path),
                             buffer, buffer_len) != 0) {
        return;
    }
    if (!writeaudit_info_is_usable(buffer) ||
        !info_field_equals(buffer, "detected_fs: ", "exfat") ||
        !info_field_equals(buffer, "write_candidate_ready: ", "1") ||
        !info_field_equals(buffer, "write_policy_allows_commit: ", "1") ||
        !info_field_equals(buffer, "real_write_prereq_ready: ", "1") ||
        !contains_text(buffer, "real_write_dry_run_lbas: ") ||
        !contains_text(buffer, "real_write_dry_run_clusters: ")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(33);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_writeunlock_reject_self_test(void) {
    static const char ok[] = "shell.elf: writeunlock reject self-test ok\n";
    static const char fail[] = "shell.elf: writeunlock reject self-test failed\n";
    struct blockfs_unlock_request request;

    request.magic = BLOCKFS_UNLOCK_MAGIC;
    request.expected_bitmap_lba = 256;
    request.expected_fat_lba = 128;
    request.expected_directory_lba = 192;
    request.flags = 0;

    if (sys_blockfs_unlock("/missing", &request) != (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(35);
    }

    write_literal(ok, sizeof(ok) - 1);
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static void run_writeunlock_success_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: writeunlock success self-test ok\n";
    static const char fail[] = "shell.elf: writeunlock success self-test failed\n";
    struct blockfs_unlock_request request;
    char info_path[SHELL_PATH_MAX];

    request.magic = BLOCKFS_UNLOCK_MAGIC;
    request.expected_bitmap_lba = 256;
    request.expected_fat_lba = 128;
    request.expected_directory_lba = 192;
    request.flags = 0;

    if (sys_blockfs_unlock("/usbdisk", &request) != 0 ||
        read_mount_info_text("/usbdisk", info_path, sizeof(info_path),
                             buffer, buffer_len) != 0 ||
        !info_field_equals(buffer, "manual_unlock_attempted: ", "1") ||
        !info_field_equals(buffer, "manual_unlock_ready: ", "1") ||
        !info_field_equals(buffer, "manual_unlock_blocker: ", "none")) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(36);
    }

    write_literal(ok, sizeof(ok) - 1);
}
#endif

static void run_usb_fat32_mount_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: USB FAT32 mount read ok\n";
    static const char fail[] = "shell.elf: USB FAT32 mount read failed\n";
    static const char info_prefix[] = "TangPingOS block-backed mount";
    static const char info_fs[] = "detected_fs: fat32";
    static const char info_label[] = "volume_label: USBBOOT";
    static const char readme_path[] = "/usbboot/README.TXT";
    static const char bootx64_path[] = "/usbboot/EFI/BOOT/BOOTX64.EFI";
    static const char long_name_path[] = "/usbboot/usb long filename.txt";
    static const char chain_path[] = "/usbboot/CHAIN.TXT";
    static const char late_path[] = "/usbboot/LATE.TXT";
    static const char readme_content[] = "Hello from USB FAT32 boot partition.\n";
    static const char bootx64_content[] = "TangPingOS can walk USB FAT32 EFI/BOOT.\n";
    static const char long_name_content[] = "USB FAT32 long file names are visible now.\n";
    static const char chain_prefix[] = "USB FAT32 chained file cluster 10 begins here.\n";
    static const char chain_middle[] = "USB FAT32 chained file cluster 11 continues here.\n";
    static const char chain_suffix[] = "USB FAT32 chained file end.\n";
    static const char late_content[] = "USB FAT32 late root entry from the second directory cluster.\n";
    uint64_t fd = sys_open("/usbboot/info.txt");
    uint64_t read_len = 0;

    if (fd == (uint64_t)-1) {
        return;
    }
    if (buffer_len < 512) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(33);
    }
    while (read_len + 1 < buffer_len) {
        uint64_t chunk = sys_read(fd, buffer + read_len, buffer_len - 1 - read_len);
        if (chunk == (uint64_t)-1) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(33);
        }
        if (chunk == 0) {
            break;
        }
        read_len += chunk;
    }
    sys_close(fd);
    if (read_len == 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(33);
    }
    buffer[read_len] = '\0';
    if (!starts_with(buffer, info_prefix) ||
        !contains_text(buffer, info_fs) ||
        !contains_text(buffer, info_label)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(33);
    }

    if (!read_file_equals(readme_path, buffer, buffer_len, readme_content) ||
        !read_file_equals(bootx64_path, buffer, buffer_len, bootx64_content) ||
        !read_file_equals(long_name_path, buffer, buffer_len, long_name_content) ||
        !read_file_equals(late_path, buffer, buffer_len, late_content) ||
        !read_file_matches_segments(chain_path, buffer, buffer_len,
                                    chain_prefix, 'U', 470, chain_middle,
                                    'V', 180, chain_suffix) ||
        !read_file_equals("/usbboot/readme.txt", buffer, buffer_len, readme_content) ||
        !read_file_equals("/usbboot/efi/boot/bootx64.efi", buffer, buffer_len,
                          bootx64_content) ||
        !read_file_matches_segments("/usbboot/chain.txt", buffer, buffer_len,
                                    chain_prefix, 'U', 470, chain_middle,
                                    'V', 180, chain_suffix) ||
        !read_file_equals("/usbboot/late.txt", buffer, buffer_len, late_content)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(33);
    }

    write_literal(ok, sizeof(ok) - 1);
}

static void run_usb_block_write_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: USB BOT block write ok\n";
    static const char fail[] = "shell.elf: USB BOT block write failed\n";

    if (find_block_device_by_name("usb0p1", 0, 0) != 0) {
        return;
    }
    if (usbtestwrite_run(buffer, buffer_len, 0) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(30);
    }

    write_literal(ok, sizeof(ok) - 1);
}

#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
static int verify_exfat_vfs_write_path(const char *path, const char *content,
                                       char *buffer, uint64_t buffer_len) {
    uint64_t len = string_length(content);
    uint64_t fd;
    uint64_t read_len;

    if (buffer_len < len + 1) {
        return -1;
    }
    if (write_file_text(path, content, OPEN_CREATE | OPEN_TRUNC) != 0) {
        return -1;
    }

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        return -1;
    }
    read_len = sys_read(fd, buffer, buffer_len - 1);
    sys_close(fd);
    buffer[read_len < buffer_len ? read_len : buffer_len - 1] = '\0';
    if (read_len != len || !strings_equal(buffer, content)) {
        return -1;
    }

    return 0;
}

static int verify_exfat_vfs_unlink_path(const char *path) {
    uint64_t fd;

    if (sys_unlink(path) != 0) {
        return -1;
    }
    fd = sys_open(path);
    if (fd != (uint64_t)-1) {
        sys_close(fd);
        return -1;
    }
    return 0;
}

static void fill_exfat_growth_pattern(char *buffer, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        buffer[i] = (char)('A' + (i % 26));
    }
}

static int verify_exfat_growth_pattern(const char *buffer, uint64_t len) {
    for (uint64_t i = 0; i < len; i++) {
        if (buffer[i] != (char)('A' + (i % 26))) {
            return -1;
        }
    }
    return 0;
}

static int verify_exfat_direct_write_growth_path(const char *path,
                                                 char *buffer,
                                                 uint64_t buffer_len) {
    uint64_t grow_len = 5000;
    uint64_t fd;
    uint64_t read_len;
    uint64_t total_read = 0;

    if (buffer_len < grow_len + 1) {
        return -1;
    }
    (void)sys_unlink(path);
    fill_exfat_growth_pattern(buffer, grow_len);
    if (sys_write_file(path, 0, buffer, grow_len) != grow_len) {
        return -1;
    }

    fd = sys_open(path);
    if (fd == (uint64_t)-1) {
        return -1;
    }
    while (total_read < grow_len) {
        read_len = sys_read(fd, buffer + total_read, grow_len - total_read);
        if (read_len == (uint64_t)-1) {
            sys_close(fd);
            return -1;
        }
        if (read_len == 0) {
            break;
        }
        total_read += read_len;
    }
    sys_close(fd);
    if (total_read != grow_len ||
        verify_exfat_growth_pattern(buffer, grow_len) != 0) {
        return -1;
    }

    return verify_exfat_vfs_unlink_path(path);
}

static void run_usb_exfat_user_write_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: USB exFAT user write ok\n";
    static const char fail[] = "shell.elf: USB exFAT user write failed\n";
    static const char path[] = "/usbdisk/UWRITE.TXT";
    static const char append_path[] = "/usbdisk/UAPPEND.TXT";
    static const char copy_path[] = "/usbdisk/UCOPY.TXT";
    static const char moved_path[] = "/usbdisk/UMOVED.TXT";
    static const char grow_path[] = "/usbdisk/UGROW.TXT";
    static const char persist_path[] = "/usbdisk/PERSIST.TXT";
    static const char content[] = "TangPingOS user write on USB exFAT.\n";
    static const char append_a[] = "alpha";
    static const char append_b[] = "beta";
    static const char append_expected[] = "alphabeta";
    static const char persist_expected[] = "TangPingOS persisted USB exFAT data.\n";
    static const char persisted_ok[] = "shell.elf: USB exFAT persistence read ok\n";
    static const char hello_expected[] =
        "Hello from TangPingOS initrd.\n"
        "This file was packed into the ISO, loaded by Limine, parsed by the kernel, and read by user space.\n";
    uint64_t fd;
    int saw_persisted_file;

    fd = sys_open("/usbdisk/HELLO.TXT");
    if (fd == (uint64_t)-1) {
        return;
    }
    sys_close(fd);

    saw_persisted_file = read_file_equals(persist_path, buffer, buffer_len, persist_expected);

    (void)sys_unlink(path);
    (void)sys_unlink(append_path);
    (void)sys_unlink(copy_path);
    (void)sys_unlink(moved_path);
    (void)sys_unlink(grow_path);

    if (verify_exfat_vfs_write_path(persist_path, persist_expected, buffer, buffer_len) != 0) {
        write_literal("USB exFAT step: persist write\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (verify_exfat_vfs_write_path(path, content, buffer, buffer_len) != 0) {
        write_literal("USB exFAT step: write\n", 22);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (verify_exfat_vfs_unlink_path(path) != 0) {
        write_literal("USB exFAT step: unlink write\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (write_file_text(append_path, append_a, OPEN_CREATE | OPEN_TRUNC) != 0 ||
        write_file_text(append_path, append_b, OPEN_CREATE | OPEN_APPEND) != 0 ||
        !read_file_equals(append_path, buffer, buffer_len, append_expected)) {
        write_literal("USB exFAT step: append\n", 23);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (verify_exfat_vfs_unlink_path(append_path) != 0) {
        write_literal("USB exFAT step: unlink append\n", 30);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (copy_file_data("/hello.txt", copy_path, buffer, buffer_len) != 0) {
        write_literal("USB exFAT step: copy source\n", 28);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (copy_file_data(copy_path, moved_path, buffer, buffer_len) != 0) {
        write_literal("USB exFAT step: copy moved\n", 27);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (sys_unlink(copy_path) != 0) {
        write_literal("USB exFAT step: unlink copy\n", 28);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (!read_file_equals(moved_path, buffer, buffer_len, hello_expected)) {
        write_literal("USB exFAT step: read moved\n", 27);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (verify_exfat_direct_write_growth_path(grow_path, buffer, buffer_len) != 0) {
        write_literal("USB exFAT step: grow\n", 21);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }
    if (verify_exfat_vfs_unlink_path(moved_path) != 0) {
        write_literal("USB exFAT step: unlink moved\n", 29);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(34);
    }

    if (saw_persisted_file) {
        write_literal(persisted_ok, sizeof(persisted_ok) - 1);
    }
    write_literal(ok, sizeof(ok) - 1);
}

static void run_exfat_vfs_write_self_test(char *buffer, uint64_t buffer_len) {
    static const char ok[] = "shell.elf: exFAT VFS write ok\n";
    static const char usb_ok[] = "shell.elf: USB exFAT VFS write ok\n";
    static const char wrap_ok[] = "shell.elf: USB xHCI ring wrap ok\n";
    static const char fail[] = "shell.elf: exFAT VFS write failed\n";
    static const char usb_fail[] = "shell.elf: USB exFAT VFS write failed\n";
    static const char wrap_fail[] = "shell.elf: USB xHCI ring wrap failed\n";
    static const char path[] = "/usb/NOTE.TXT";
    static const char second_path[] = "/usb/LOG.TXT";
    static const char grow_path[] = "/usb/GROW.TXT";
    static const char usb_path[] = "/usbdisk/USB.TXT";
    static const char usb_second_path[] = "/usbdisk/TWO.TXT";
    static const char usb_grow_path[] = "/usbdisk/GROW.TXT";
    static const char content[] = "TangPingOS wrote NOTE.TXT through VFS.\n";
    static const char second_content[] = "TangPingOS created LOG.TXT dynamically.\n";
    static const char second_recreated[] = "TangPingOS recreated LOG.TXT after unlink.\n";
    static const char usb_content[] = "TangPingOS wrote USB.TXT through USB exFAT.\n";
    static const char usb_second_content[] = "TangPingOS created TWO.TXT on USB exFAT.\n";
    static const char usb_second_recreated[] = "TangPingOS recreated TWO.TXT after unlink.\n";
    uint64_t usb_fd;

    if (verify_exfat_vfs_write_path(path, content, buffer, buffer_len) != 0 ||
        verify_exfat_vfs_write_path(second_path, second_content, buffer, buffer_len) != 0 ||
        verify_exfat_vfs_unlink_path(second_path) != 0 ||
        verify_exfat_vfs_write_path(second_path, second_recreated, buffer, buffer_len) != 0 ||
        verify_exfat_direct_write_growth_path(grow_path, buffer, buffer_len) != 0) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(26);
    }
    write_literal(ok, sizeof(ok) - 1);

    usb_fd = sys_open("/usbdisk/HELLO.TXT");
    if (usb_fd == (uint64_t)-1) {
        return;
    }
    sys_close(usb_fd);
    if (verify_exfat_vfs_write_path(usb_path, usb_content, buffer, buffer_len) != 0 ||
        verify_exfat_vfs_write_path(usb_second_path, usb_second_content, buffer, buffer_len) != 0 ||
        verify_exfat_vfs_unlink_path(usb_second_path) != 0 ||
        verify_exfat_vfs_write_path(usb_second_path, usb_second_recreated, buffer, buffer_len) != 0 ||
        verify_exfat_direct_write_growth_path(usb_grow_path, buffer, buffer_len) != 0) {
        write_literal(usb_fail, sizeof(usb_fail) - 1);
        sys_exit(31);
    }
    write_literal(usb_ok, sizeof(usb_ok) - 1);

    for (uint64_t i = 0; i < 360; i++) {
        if (usbtestwrite_run(buffer, buffer_len, 0) != 0) {
            write_literal(wrap_fail, sizeof(wrap_fail) - 1);
            sys_exit(32);
        }
    }
    write_literal(wrap_ok, sizeof(wrap_ok) - 1);
}
#endif

static __attribute__((unused)) void run_development_startup_self_tests(
    char *heap, uint64_t heap_len) {
    static const char shell_prefix[] = "shell.elf: startup commands\n";

    run_startup_self_test(heap);
    write_literal(shell_prefix, sizeof(shell_prefix) - 1);
    cmd_ls("/");
    cmd_ls("/dev");
    cmd_cat("/hello.txt", heap, 256);
    run_ramfs_self_test(heap, 256);
    run_lseek_self_test(heap, 256);
    run_unlink_self_test();
    run_shell_script_self_test(heap, 256);
    run_block_self_test(heap, 512);
    run_virtio_block_self_test(heap, 512);
    run_mount_info_self_test();
    run_block_mount_self_test(heap, heap_len);
    run_fat32_mount_self_test(heap, heap_len);
    run_usb_mount_self_test(heap, heap_len);
    run_writeaudit_self_test(heap, heap_len);
    run_writeunlock_reject_self_test();
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    run_writeunlock_success_self_test(heap, heap_len);
#endif
    run_usb_fat32_mount_self_test(heap, heap_len);
    run_usb_block_write_self_test(heap, heap_len);
#ifdef TANGPINGOS_TEST_EXFAT_COMMIT
    run_usb_exfat_user_write_self_test(heap, heap_len);
    run_exfat_vfs_write_self_test(heap, heap_len);
#endif
    cmd_ls("/");
    cmd_cat("/ram-note.txt", heap, 256);
    run_dup2_stdin_self_test(heap, 256);
}

#ifdef TANGPINGOS_TEST_MACOS_EXFAT_READ
static void run_macos_exfat_read_self_test(char *buffer, uint64_t buffer_len) {
    static const char expected[] = "hello from macOS exFAT\n";
    static const char ok[] = "shell.elf: macOS exFAT read ok\n";
    static const char fail[] = "shell.elf: macOS exFAT read failed\n";
    uint64_t fd;
    uint64_t read_len;

    if (buffer_len < sizeof(expected)) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(41);
    }

    fd = sys_open("/usbdisk/hi.txt");
    if (fd == (uint64_t)-1) {
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(41);
    }
    read_len = sys_read(fd, buffer, buffer_len);
    if (read_len != sizeof(expected) - 1 ||
        sys_read(fd, buffer + read_len, buffer_len - read_len) != 0) {
        sys_close(fd);
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(41);
    }
    for (uint64_t i = 0; i < read_len; i++) {
        if (buffer[i] != expected[i]) {
            sys_close(fd);
            write_literal(fail, sizeof(fail) - 1);
            sys_exit(41);
        }
    }
    sys_close(fd);

    fd = sys_open("/usbdisk/empty.txt");
    if (fd == (uint64_t)-1 || sys_read(fd, buffer, buffer_len) != 0) {
        if (fd != (uint64_t)-1) {
            sys_close(fd);
        }
        write_literal(fail, sizeof(fail) - 1);
        sys_exit(41);
    }
    sys_close(fd);
    write_literal(ok, sizeof(ok) - 1);
}
#endif

__attribute__((noreturn))
static void run_interactive_shell(uint64_t input_fd, char *buffer, uint64_t buffer_len) {
    static const char ready[] =
        "\fshell.elf: interactive shell ready\n"
        "TangPingOS is ready. Type help for commands.\n";
    char user[ACCOUNT_NAME_MAX];
    char line[SHELL_LINE_MAX];
    char cwd[SHELL_PATH_MAX];

    if (copy_cstr_limited(cwd, sizeof(cwd), "/") != 0) {
        sys_exit(18);
    }

    write_literal(ready, sizeof(ready) - 1);
    for (;;) {
        if (account_name_by_uid(sys_geteuid(), user, sizeof(user)) != 0) {
            copy_cstr_limited(user, sizeof(user), "unknown");
        }
        write_cstr(user);
        write_literal("@x86:", 5);
        write_cstr(cwd);
        write_literal(sys_geteuid() == 0 ? "# " : "$ ", 2);
        read_line(input_fd, line, sizeof(line));
        run_shell_command(line, cwd, buffer, buffer_len);
    }
}

__attribute__((noreturn))
void _start(void) {
    static const char pid_prefix[] = "shell.elf: pid=";
    static const char brk_prefix[] = "shell.elf: brk=";
    static const char heap_fail[] = "shell.elf: sbrk failed\n";
    static const char tty_ok[] = "shell.elf: /dev/tty open\n";
    static const char tty_write_ok[] = "shell.elf: write(/dev/tty) ok\n";
    static const char bad_fd_write_rejected[] = "shell.elf: bad write fd rejected\n";
    static const char dup2_tty_ok[] = "shell.elf: dup2(/dev/tty, stdio) ok\n";
    static const char dup2_tty_fail[] = "shell.elf: dup2(/dev/tty, stdio) failed\n";
    static const char tty_fallback[] = "shell.elf: /dev/tty unavailable, using stdin\n";
    static const char newline[] = "\n";

    write_literal(pid_prefix, sizeof(pid_prefix) - 1);
    write_u64_decimal(sys_getpid());
    write_literal(newline, sizeof(newline) - 1);

    write_literal(brk_prefix, sizeof(brk_prefix) - 1);
    write_u64_hex(sys_brk(0));
    write_literal(newline, sizeof(newline) - 1);

    char *heap = sys_sbrk(12288);
    if (heap == (void *)0) {
        write_literal(heap_fail, sizeof(heap_fail) - 1);
        sys_exit(8);
    }

#if defined(TANGPINGOS_TEST_USER_PROGRAMS) || defined(TANGPINGOS_TEST_EXFAT_COMMIT)
    run_development_startup_self_tests(heap, 12288);
#endif
#ifdef TANGPINGOS_TEST_MACOS_EXFAT_READ
    run_macos_exfat_read_self_test(heap, 12288);
#endif

    uint64_t input_fd = sys_open("/dev/tty");
    if (input_fd == (uint64_t)-1) {
        write_literal(tty_fallback, sizeof(tty_fallback) - 1);
        input_fd = STDIN_FD;
    } else {
        write_literal(tty_ok, sizeof(tty_ok) - 1);
        if (sys_dup2(input_fd, STDIN_FD) != STDIN_FD ||
            sys_dup2(input_fd, STDOUT_FD) != STDOUT_FD) {
            write_literal(dup2_tty_fail, sizeof(dup2_tty_fail) - 1);
            sys_exit(15);
        }
        write_literal(dup2_tty_ok, sizeof(dup2_tty_ok) - 1);
        sys_write(input_fd, tty_write_ok, sizeof(tty_write_ok) - 1);
    }

    if (sys_write(99, tty_write_ok, sizeof(tty_write_ok) - 1) == (uint64_t)-1) {
        write_literal(bad_fd_write_rejected, sizeof(bad_fd_write_rejected) - 1);
    } else {
        sys_exit(13);
    }

#ifdef TANGPINGOS_TEST_USER_FAULT
    *(volatile uint64_t *)0 = 0x55;
#endif

    run_interactive_shell(input_fd, heap, 12288);
}
