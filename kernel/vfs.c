#include <stddef.h>
#include <stdint.h>

#include "blockdev.h"
#include "kstring.h"
#include "ramdisk.h"
#include "sfs.h"
#include "sync.h"
#include "vfs.h"

#define RAMDISK_BLOCKS 1024u // 512 KiB

static blockdev_t disk;
static sfs_t root_fs;
static const vfs_fs_ops_t *ops;
static void *fs_ctx;
static mutex_t vfs_mutex;
static int mounted;

const char *vfs_strerror(int err)
{
    switch (err)
    {
    case VFS_OK:
        return "ok";
    case VFS_ENOENT:
        return "no such file or directory";
    case VFS_EEXIST:
        return "already exists";
    case VFS_ENOTDIR:
        return "not a directory";
    case VFS_EISDIR:
        return "is a directory";
    case VFS_ENOSPC:
        return "no space left on device";
    case VFS_ENOTEMPTY:
        return "directory not empty";
    case VFS_EINVAL:
        return "invalid argument";
    case VFS_EIO:
        return "I/O error";
    case VFS_ENAMETOOLONG:
        return "name too long";
    case VFS_EFBIG:
        return "file too large (max 6144 bytes)";
    default:
        return "unknown error";
    }
}

/* ---------- normalisasi path (leksikal: ".", "..", "//") ---------- */

int vfs_normalize(const char *cwd, const char *path, char *out)
{
    uint32_t len = 0;

    out[0] = '\0';
    if (path[0] != '/')
    { // path relatif: mulai dari cwd
        for (; cwd[len] != '\0'; len++)
        {
            if (len >= VFS_PATH_MAX - 1u)
                return VFS_ENAMETOOLONG;
            out[len] = cwd[len];
        }
        out[len] = '\0';
        if (len == 1 && out[0] == '/')
        { // root direpresentasikan sebagai ""
            len = 0;
            out[0] = '\0';
        }
    }

    const char *p = path;
    while (*p != '\0')
    {
        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        const char *start = p;
        while (*p != '\0' && *p != '/')
            p++;
        uint32_t clen = (uint32_t)(p - start);

        if (clen == 1 && start[0] == '.')
            continue;
        if (clen == 2 && start[0] == '.' && start[1] == '.')
        {
            while (len > 0 && out[len - 1] != '/')
                len--;
            if (len > 0)
                len--; // buang '/' terakhir
            out[len] = '\0';
            continue;
        }
        if (clen > VFS_NAME_MAX || len + 1u + clen >= VFS_PATH_MAX)
            return VFS_ENAMETOOLONG;

        out[len++] = '/';
        for (uint32_t i = 0; i < clen; i++)
            out[len++] = start[i];
        out[len] = '\0';
    }

    if (len == 0)
    {
        out[0] = '/';
        out[1] = '\0';
    }
    return VFS_OK;
}

/* ---------- resolusi path -> inode (dipanggil dengan mutex dipegang) ---------- */

static int resolve(const char *path, uint32_t *ino_out)
{
    uint32_t ino = ops->root(fs_ctx);
    const char *p = path;
    char name[VFS_NAME_MAX + 1];

    while (*p != '\0')
    {
        while (*p == '/')
            p++;
        if (*p == '\0')
            break;

        uint32_t n = 0;
        while (*p != '\0' && *p != '/')
        {
            if (n >= VFS_NAME_MAX)
                return VFS_ENAMETOOLONG;
            name[n++] = *p++;
        }
        name[n] = '\0';

        int rc = ops->lookup(fs_ctx, ino, name, &ino);
        if (rc != VFS_OK)
            return rc;
    }
    *ino_out = ino;
    return VFS_OK;
}

// "/a/b" -> parent = inode "/a", leaf = "b"
static int resolve_parent(const char *path, uint32_t *parent, char *leaf)
{
    uint32_t len = (uint32_t)strlen(path);
    uint32_t cut = len;
    char dir[VFS_PATH_MAX];

    if (len <= 1)
        return VFS_EINVAL; // "/" tidak punya induk

    while (cut > 0 && path[cut - 1] != '/')
        cut--;

    uint32_t llen = len - cut;
    if (llen == 0)
        return VFS_EINVAL;
    if (llen > VFS_NAME_MAX)
        return VFS_ENAMETOOLONG;

    for (uint32_t i = 0; i < llen; i++)
        leaf[i] = path[cut + i];
    leaf[llen] = '\0';

    if (cut > 1)
    {
        for (uint32_t i = 0; i < cut - 1u; i++)
            dir[i] = path[i];
        dir[cut - 1u] = '\0';
    }
    else
    {
        dir[0] = '/';
        dir[1] = '\0';
    }
    return resolve(dir, parent);
}

/* ---------- API publik: semua dikunci mutex ---------- */

int vfs_stat(const char *path, vfs_stat_t *st)
{
    uint32_t ino;
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = resolve(path, &ino);
    if (rc == VFS_OK)
        rc = ops->stat(fs_ctx, ino, st);
    mutex_unlock(&vfs_mutex);
    return rc;
}

static int create_node(const char *path, uint8_t type)
{
    uint32_t parent;
    char leaf[VFS_NAME_MAX + 1];
    int rc = resolve_parent(path, &parent, leaf);

    if (rc != VFS_OK)
        return rc;
    return ops->create(fs_ctx, parent, leaf, type, NULL);
}

int vfs_mkdir(const char *path)
{
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = create_node(path, VFS_TYPE_DIR);
    mutex_unlock(&vfs_mutex);
    return rc;
}

int vfs_create(const char *path)
{
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = create_node(path, VFS_TYPE_FILE);
    if (rc == VFS_EEXIST)
        rc = VFS_OK; // touch pada yang sudah ada: tidak apa-apa
    mutex_unlock(&vfs_mutex);
    return rc;
}

int vfs_write_file(const char *path, const void *data, uint32_t len, int append)
{
    uint32_t ino;
    vfs_stat_t st;
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);

    rc = resolve(path, &ino);
    if (rc == VFS_ENOENT)
    { // file belum ada: buat dulu
        uint32_t parent;
        char leaf[VFS_NAME_MAX + 1];

        rc = resolve_parent(path, &parent, leaf);
        if (rc == VFS_OK)
            rc = ops->create(fs_ctx, parent, leaf, VFS_TYPE_FILE, &ino);
    }
    if (rc == VFS_OK)
        rc = ops->stat(fs_ctx, ino, &st);
    if (rc == VFS_OK && st.type != VFS_TYPE_FILE)
        rc = VFS_EISDIR;
    if (rc == VFS_OK && !append)
    { // overwrite: kosongkan dulu (tidak atomik)
        rc = ops->truncate(fs_ctx, ino);
        st.size = 0;
    }
    if (rc == VFS_OK && len > 0)
    {
        int written = ops->write(fs_ctx, ino, append ? st.size : 0u, data, len);

        if (written < 0)
            rc = written;
        else if ((uint32_t)written != len)
            rc = VFS_ENOSPC;
    }
    mutex_unlock(&vfs_mutex);
    return rc;
}

int vfs_read(const char *path, uint32_t offset, void *buf, uint32_t len)
{
    uint32_t ino;
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = resolve(path, &ino);
    if (rc == VFS_OK)
        rc = ops->read(fs_ctx, ino, offset, buf, len);
    mutex_unlock(&vfs_mutex);
    return rc;
}

static int remove_node(const char *path, int want_dir)
{
    uint32_t parent, ino;
    vfs_stat_t st;
    char leaf[VFS_NAME_MAX + 1];
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);

    rc = resolve_parent(path, &parent, leaf);
    if (rc == VFS_OK)
        rc = ops->lookup(fs_ctx, parent, leaf, &ino);
    if (rc == VFS_OK)
        rc = ops->stat(fs_ctx, ino, &st);
    if (rc == VFS_OK && want_dir && st.type != VFS_TYPE_DIR)
        rc = VFS_ENOTDIR;
    if (rc == VFS_OK && !want_dir && st.type == VFS_TYPE_DIR)
        rc = VFS_EISDIR;
    if (rc == VFS_OK)
        rc = ops->remove(fs_ctx, parent, leaf);

    mutex_unlock(&vfs_mutex);
    return rc;
}

int vfs_remove(const char *path) { return remove_node(path, 0); }
int vfs_rmdir(const char *path) { return remove_node(path, 1); }

int vfs_readdir(const char *path, uint32_t index, vfs_dirent_t *out)
{
    uint32_t ino;
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = resolve(path, &ino);
    if (rc == VFS_OK)
        rc = ops->readdir(fs_ctx, ino, index, out);
    mutex_unlock(&vfs_mutex);
    return rc;
}

void vfs_info(vfs_info_t *info)
{
    memset(info, 0, sizeof(*info));
    if (!mounted)
        return;
    mutex_lock(&vfs_mutex);
    ops->info(fs_ctx, info);
    info->device = disk.name;
    info->dev_reads = disk.reads;
    info->dev_writes = disk.writes;
    mutex_unlock(&vfs_mutex);
}

int vfs_check(uint32_t *problems)
{
    int rc;

    if (!mounted)
        return VFS_EIO;
    mutex_lock(&vfs_mutex);
    rc = ops->check(fs_ctx, problems);
    mutex_unlock(&vfs_mutex);
    return rc;
}

int vfs_init(void)
{
    static const char readme[] =
        "Welcome to SeventhOS!\nThis file lives on ramdisk0 (SeventhFS via VFS).\n";
    int rc;

    mutex_init(&vfs_mutex, "vfs", 20);

    if (ramdisk_create(&disk, "ramdisk0", RAMDISK_BLOCKS) != 0)
        return VFS_ENOSPC;
    rc = sfs_format(&disk);
    if (rc == VFS_OK)
        rc = sfs_mount(&disk, &root_fs);
    if (rc != VFS_OK)
        return rc;

    ops = &sfs_ops;
    fs_ctx = &root_fs;
    mounted = 1;

    vfs_mkdir("/docs");
    vfs_mkdir("/tmp");
    vfs_write_file("/README.txt", readme, sizeof(readme) - 1u, 0);
    return VFS_OK;
}