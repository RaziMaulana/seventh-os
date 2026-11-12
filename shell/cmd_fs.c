#include <stddef.h>
#include <stdint.h>

#include "cmd_fs.h"
#include "heap.h"
#include "kstring.h"
#include "shell_util.h"
#include "terminal.h"
#include "timer.h"
#include "vfs.h"

static char cwd[VFS_PATH_MAX] = "/";

const char *cmd_fs_cwd(void)
{
    return cwd;
}

static void fs_error(const char *what, int rc)
{
    terminal_write(what);
    terminal_write(": ");
    terminal_write(vfs_strerror(rc));
    terminal_write("\n");
}

// Ambil satu token (dipisah spasi); *rest menunjuk ke sisa baris.
static void next_token(const char *s, char *out, uint32_t max, const char **rest)
{
    uint32_t n = 0;

    while (*s == ' ')
        s++;
    while (*s != '\0' && *s != ' ')
    {
        if (n < max - 1u)
            out[n++] = *s;
        s++;
    }
    out[n] = '\0';
    while (*s == ' ')
        s++;
    *rest = s;
}

static int to_abs(const char *arg, char *abs)
{
    int rc = vfs_normalize(cwd, arg, abs);

    if (rc != VFS_OK)
        fs_error(arg, rc);
    return rc;
}

// Pola umum: satu argumen path -> satu operasi VFS. Diam bila sukses (gaya Unix).
static void simple_op(const char *args, const char *usage, int (*op)(const char *))
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX];
    const char *rest;

    next_token(args, arg, sizeof(arg), &rest);
    if (arg[0] == '\0')
    {
        terminal_write("usage: ");
        terminal_write(usage);
        terminal_write("\n");
        return;
    }
    if (to_abs(arg, abs) != VFS_OK)
        return;

    int rc = op(abs);
    if (rc != VFS_OK)
        fs_error(abs, rc);
}

void cmd_pwd(void)
{
    terminal_write(cwd);
    terminal_write("\n");
}

void cmd_cd(const char *args)
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX];
    const char *rest;
    vfs_stat_t st;

    next_token(args, arg, sizeof(arg), &rest);
    if (to_abs(arg[0] != '\0' ? arg : "/", abs) != VFS_OK)
        return;

    int rc = vfs_stat(abs, &st);
    if (rc != VFS_OK)
    {
        fs_error(abs, rc);
        return;
    }
    if (st.type != VFS_TYPE_DIR)
    {
        fs_error(abs, VFS_ENOTDIR);
        return;
    }
    memcpy(cwd, abs, strlen(abs) + 1u);
}

void cmd_ls(const char *args)
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX];
    const char *rest;
    vfs_stat_t st;
    vfs_dirent_t e;
    uint32_t index = 0;
    int rc;

    next_token(args, arg, sizeof(arg), &rest);
    if (to_abs(arg[0] != '\0' ? arg : ".", abs) != VFS_OK)
        return;

    rc = vfs_stat(abs, &st);
    if (rc != VFS_OK)
    {
        fs_error(abs, rc);
        return;
    }
    if (st.type == VFS_TYPE_FILE)
    {
        terminal_write(abs);
        terminal_write("  ");
        terminal_write_dec(st.size);
        terminal_write(" bytes\n");
        return;
    }

    terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    terminal_write("TYPE  NAME                         SIZE\n");
    terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);

    while ((rc = vfs_readdir(abs, index, &e)) == 0)
    {
        if (e.type == VFS_TYPE_DIR)
        {
            terminal_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
            terminal_write("dir   ");
            terminal_write_pad(e.name, 29);
            terminal_write("-\n");
            terminal_set_color(COLOR_LIGHT_GREY, COLOR_BLACK);
        }
        else
        {
            terminal_write("file  ");
            terminal_write_pad(e.name, 29);
            terminal_write_dec(e.size);
            terminal_write("\n");
        }
        index++;
    }
    if (rc < 0)
        fs_error(abs, rc);
    terminal_write_dec(index);
    terminal_write(" item(s)\n");
}

void cmd_mkdir(const char *args) { simple_op(args, "mkdir <path>", vfs_mkdir); }
void cmd_touch(const char *args) { simple_op(args, "touch <path>", vfs_create); }
void cmd_rm(const char *args) { simple_op(args, "rm <file>", vfs_remove); }
void cmd_rmdir(const char *args) { simple_op(args, "rmdir <directory>", vfs_rmdir); }

void cmd_cat(const char *args)
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX];
    const char *rest;
    uint8_t buf[128];
    vfs_stat_t st;
    uint32_t offset = 0;
    char last = '\n';
    int n;

    next_token(args, arg, sizeof(arg), &rest);
    if (arg[0] == '\0')
    {
        terminal_write("usage: cat <file>\n");
        return;
    }
    if (to_abs(arg, abs) != VFS_OK)
        return;

    n = vfs_stat(abs, &st);
    if (n != VFS_OK)
    {
        fs_error(abs, n);
        return;
    }
    if (st.type == VFS_TYPE_DIR)
    {
        fs_error(abs, VFS_EISDIR);
        return;
    }

    while ((n = vfs_read(abs, offset, buf, sizeof(buf))) > 0)
    {
        for (int i = 0; i < n; i++)
        {
            char c = (char)buf[i];

            if (c == '\t')
                c = ' ';
            if (c != '\n' && (c < 32 || c > 126))
                c = '.';
            terminal_putchar(c);
            last = c;
        }
        offset += (uint32_t)n;
    }
    if (n < 0)
        fs_error(abs, n);
    if (last != '\n')
        terminal_putchar('\n');
}

// "\n" -> baris baru, "\\" -> backslash, agar file multibaris bisa ditulis dari satu baris shell.
static uint32_t unescape(const char *src, char *dst, uint32_t max)
{
    uint32_t n = 0;

    while (*src != '\0' && n < max)
    {
        if (src[0] == '\\' && src[1] == 'n')
        {
            dst[n++] = '\n';
            src += 2;
        }
        else if (src[0] == '\\' && src[1] == '\\')
        {
            dst[n++] = '\\';
            src += 2;
        }
        else
        {
            dst[n++] = *src++;
        }
    }
    return n;
}

static void write_common(const char *args, int append)
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX], text[128];
    const char *rest;

    next_token(args, arg, sizeof(arg), &rest);
    if (arg[0] == '\0')
    {
        terminal_write(append ? "usage: append <file> <text>\n" : "usage: write <file> <text>\n");
        return;
    }
    if (to_abs(arg, abs) != VFS_OK)
        return;

    uint32_t len = unescape(rest, text, sizeof(text));
    int rc = vfs_write_file(abs, text, len, append);

    if (rc != VFS_OK)
    {
        fs_error(abs, rc);
        return;
    }
    terminal_write(append ? "appended " : "wrote ");
    terminal_write_dec(len);
    terminal_write(" byte(s) to ");
    terminal_write(abs);
    terminal_write("\n");
}

void cmd_write(const char *args) { write_common(args, 0); }
void cmd_append(const char *args) { write_common(args, 1); }

void cmd_stat(const char *args)
{
    char arg[VFS_PATH_MAX], abs[VFS_PATH_MAX];
    const char *rest;
    vfs_stat_t st;

    next_token(args, arg, sizeof(arg), &rest);
    if (to_abs(arg[0] != '\0' ? arg : ".", abs) != VFS_OK)
        return;

    int rc = vfs_stat(abs, &st);
    if (rc != VFS_OK)
    {
        fs_error(abs, rc);
        return;
    }
    uint32_t hz = timer_get_frequency();
    uint32_t tenth_seconds = st.mtime * 10u / hz;

    terminal_write("  path    : ");
    terminal_write(abs);
    terminal_write("\n  type    : ");
    terminal_write(st.type == VFS_TYPE_DIR ? "directory" : "regular file");
    terminal_write("\n  inode   : ");
    terminal_write_dec(st.inode);
    terminal_write("\n  size    : ");
    terminal_write_dec(st.size);
    terminal_write(" bytes in ");
    terminal_write_dec(st.blocks);
    terminal_write(" block(s)\n  modified: uptime ");
    print_tenths(tenth_seconds);
    terminal_write(" s\n");
}

void cmd_df(void)
{
    vfs_info_t fi;

    vfs_info(&fi);
    if (fi.fs_name == NULL)
    {
        terminal_write("no filesystem mounted\n");
        return;
    }
    uint32_t used = fi.total_blocks - fi.free_blocks;
    uint32_t pct = used * 100u / fi.total_blocks;

    terminal_write("Filesystem : ");
    terminal_write(fi.fs_name);
    terminal_write(" on ");
    terminal_write(fi.device);
    terminal_write("\nBlock size : ");
    terminal_write_dec(fi.block_size);
    terminal_write(" B   max file size: ");
    terminal_write_dec(fi.max_file_size);
    terminal_write(" B\nData blocks: ");
    terminal_write_dec(used);
    terminal_write("/");
    terminal_write_dec(fi.total_blocks);
    terminal_write(" used  ");
    print_bar(pct);
    terminal_write(" ");
    terminal_write_dec(pct);
    terminal_write("%\nInodes     : ");
    terminal_write_dec(fi.total_inodes - fi.free_inodes);
    terminal_write("/");
    terminal_write_dec(fi.total_inodes);
    terminal_write(" used\nDevice I/O : reads=");
    terminal_write_dec(fi.dev_reads);
    terminal_write(" writes=");
    terminal_write_dec(fi.dev_writes);
    terminal_write("\n");
}

/* ---------- pengujian otomatis CRUD ---------- */

static int file_equals(const char *path, const char *expect, uint32_t len)
{
    char buf[64];
    int n = vfs_read(path, 0, buf, sizeof(buf));

    return n == (int)len && memcmp(buf, expect, len) == 0;
}

int fstest_run(uint32_t *pass_out, uint32_t *fail_out)
{
    static const char t1[] = "Hello SeventhOS";
    static const char t2[] = " - appended";
    static const char t3[] = "short";
    const char *a = "/fstest/a.txt";
    const char *big_path = "/fstest/big.bin";
    char both[64];
    vfs_info_t before, after;
    vfs_stat_t st;
    uint32_t pass = 0, fail = 0, problems = 1;

    terminal_write("Filesystem CRUD test (directory /fstest)\n");
    vfs_info(&before);

    print_check("mkdir /fstest", vfs_mkdir("/fstest") == VFS_OK, &pass, &fail);
    print_check("mkdir duplicate rejected (EEXIST)", vfs_mkdir("/fstest") == VFS_EEXIST, &pass, &fail);
    print_check("touch creates an empty file",
                vfs_create(a) == VFS_OK && vfs_stat(a, &st) == VFS_OK && st.size == 0, &pass, &fail);
    print_check("write (create/update) then read back",
                vfs_write_file(a, t1, sizeof(t1) - 1u, 0) == VFS_OK &&
                    file_equals(a, t1, sizeof(t1) - 1u),
                &pass, &fail);

    memcpy(both, t1, sizeof(t1) - 1u);
    memcpy(both + sizeof(t1) - 1u, t2, sizeof(t2) - 1u);
    print_check("append extends the file",
                vfs_write_file(a, t2, sizeof(t2) - 1u, 1) == VFS_OK &&
                    file_equals(a, both, sizeof(t1) + sizeof(t2) - 2u),
                &pass, &fail);
    print_check("overwrite with shorter text shrinks the file",
                vfs_write_file(a, t3, 5, 0) == VFS_OK && file_equals(a, t3, 5) &&
                    vfs_stat(a, &st) == VFS_OK && st.size == 5,
                &pass, &fail);

    uint8_t *big = kmalloc(2000);
    uint8_t *back = kmalloc(2000);

    if (big == NULL || back == NULL)
    {
        print_check("allocate test buffers", 0, &pass, &fail);
    }
    else
    {
        for (uint32_t i = 0; i < 2000; i++)
            big[i] = (uint8_t)(i * 7u + 3u);

        int ok = vfs_write_file(big_path, big, 2000, 0) == VFS_OK;
        ok = ok && vfs_read(big_path, 0, back, 2000) == 2000 && memcmp(big, back, 2000) == 0;
        ok = ok && vfs_stat(big_path, &st) == VFS_OK && st.size == 2000 && st.blocks == 4;
        print_check("2000-byte file spans 4 blocks, data intact", ok, &pass, &fail);

        print_check("read at offset 500 (100 bytes)",
                    vfs_read(big_path, 500, back, 100) == 100 &&
                        memcmp(big + 500, back, 100) == 0,
                    &pass, &fail);
    }
    kfree(big);
    kfree(back);

    print_check("rmdir on non-empty directory rejected (ENOTEMPTY)",
                vfs_rmdir("/fstest") == VFS_ENOTEMPTY, &pass, &fail);
    print_check("rm on a directory rejected (EISDIR)", vfs_remove("/fstest") == VFS_EISDIR, &pass, &fail);
    print_check("rm deletes files (stat then returns ENOENT)",
                vfs_remove(a) == VFS_OK && vfs_remove(big_path) == VFS_OK &&
                    vfs_stat(a, &st) == VFS_ENOENT,
                &pass, &fail);
    print_check("rmdir removes the empty directory", vfs_rmdir("/fstest") == VFS_OK, &pass, &fail);

    vfs_info(&after);
    print_check("no leaked blocks or inodes",
                after.free_blocks == before.free_blocks && after.free_inodes == before.free_inodes,
                &pass, &fail);
    print_check("consistency check (fsck): 0 problems",
                vfs_check(&problems) == VFS_OK && problems == 0, &pass, &fail);

    terminal_write("Result: ");
    terminal_write_dec(pass);
    terminal_write(" passed, ");
    terminal_write_dec(fail);
    terminal_write(" failed\n");

    *pass_out = pass;
    *fail_out = fail;
    return (fail == 0) ? 0 : -1;
}

void cmd_fstest(void)
{
    uint32_t pass, fail;

    fstest_run(&pass, &fail);
}

/* ---------- uji disk penuh ---------- */

static void make_name(char *out, const char *prefix, uint32_t n)
{
    char digits[11];
    uint32_t d = 0, len = 0;

    while (*prefix != '\0')
        out[len++] = *prefix++;
    if (n == 0)
        digits[d++] = '0';
    while (n > 0)
    {
        digits[d++] = (char)('0' + n % 10u);
        n /= 10u;
    }
    while (d > 0)
        out[len++] = digits[--d];
    out[len] = '\0';
}

void cmd_fsfill(void)
{
    char path[VFS_PATH_MAX];
    uint8_t chunk[512];
    vfs_info_t before, full, after;
    uint32_t created = 0, problems = 1;
    int rc;

    vfs_info(&before);
    memset(chunk, 'X', sizeof(chunk));

    rc = vfs_mkdir("/fill");
    if (rc != VFS_OK)
    {
        fs_error("/fill", rc);
        return;
    }
    terminal_write("Creating 6 KiB files until the disk is full...\n");

    for (uint32_t i = 0;; i++)
    {
        make_name(path, "/fill/f", i);
        rc = vfs_create(path);
        if (rc != VFS_OK)
            break;
        created++;
        for (uint32_t b = 0; b < 12 && rc == VFS_OK; b++)
            rc = vfs_write_file(path, chunk, sizeof(chunk), 1);
        if (rc != VFS_OK)
            break;
    }

    vfs_info(&full);
    terminal_write("Stopped after ");
    terminal_write_dec(created);
    terminal_write(" file(s): ");
    terminal_write(vfs_strerror(rc));
    terminal_write("\nFree blocks now: ");
    terminal_write_dec(full.free_blocks);
    terminal_write("/");
    terminal_write_dec(full.total_blocks);
    terminal_write("  free inodes: ");
    terminal_write_dec(full.free_inodes);
    terminal_write("\n");

    for (uint32_t i = 0;; i++)
    { // bersihkan semuanya
        make_name(path, "/fill/f", i);
        if (vfs_remove(path) != VFS_OK)
            break;
    }
    vfs_rmdir("/fill");

    vfs_info(&after);
    terminal_write(after.free_blocks == before.free_blocks && after.free_inodes == before.free_inodes
                       ? "Cleanup: all blocks and inodes recovered\n"
                       : "Cleanup: LEAK detected\n");
    terminal_write(vfs_check(&problems) == VFS_OK && problems == 0
                       ? "fsck: filesystem consistent\n"
                       : "fsck: PROBLEMS found\n");
}