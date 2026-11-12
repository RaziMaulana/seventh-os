#ifndef SEVENTHOS_VFS_H
#define SEVENTHOS_VFS_H

#include <stdint.h>

#define VFS_NAME_MAX 27u
#define VFS_PATH_MAX 128u

#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR 2

#define VFS_OK 0
#define VFS_ENOENT -1
#define VFS_EEXIST -2
#define VFS_ENOTDIR -3
#define VFS_EISDIR -4
#define VFS_ENOSPC -5
#define VFS_ENOTEMPTY -6
#define VFS_EINVAL -7
#define VFS_EIO -8
#define VFS_ENAMETOOLONG -9
#define VFS_EFBIG -10

typedef struct
{
    uint8_t type;
    uint32_t inode;
    uint32_t size;
    uint32_t blocks;
    uint32_t mtime; // tick PIT saat terakhir diubah
} vfs_stat_t;

typedef struct
{
    char name[VFS_NAME_MAX + 1];
    uint8_t type;
    uint32_t size;
} vfs_dirent_t;

typedef struct
{
    const char *fs_name;
    const char *device;
    uint32_t block_size;
    uint32_t total_blocks; // blok data
    uint32_t free_blocks;
    uint32_t total_inodes;
    uint32_t free_inodes;
    uint32_t max_file_size;
    uint32_t dev_reads;
    uint32_t dev_writes;
} vfs_info_t;

// Antarmuka yang harus disediakan setiap driver filesystem.
typedef struct vfs_fs_ops
{
    const char *name;
    uint32_t (*root)(void *fs);
    int (*lookup)(void *fs, uint32_t dir, const char *name, uint32_t *ino);
    int (*create)(void *fs, uint32_t dir, const char *name, uint8_t type, uint32_t *ino);
    int (*remove)(void *fs, uint32_t dir, const char *name);
    int (*read)(void *fs, uint32_t ino, uint32_t offset, void *buf, uint32_t len);
    int (*write)(void *fs, uint32_t ino, uint32_t offset, const void *buf, uint32_t len);
    int (*truncate)(void *fs, uint32_t ino);
    int (*stat)(void *fs, uint32_t ino, vfs_stat_t *st);
    int (*readdir)(void *fs, uint32_t dir, uint32_t index, vfs_dirent_t *out); // 0=ada, 1=habis
    void (*info)(void *fs, vfs_info_t *info);
    int (*check)(void *fs, uint32_t *problems);
} vfs_fs_ops_t;

int vfs_init(void);
const char *vfs_strerror(int err);
int vfs_normalize(const char *cwd, const char *path, char *out); // out >= VFS_PATH_MAX

int vfs_stat(const char *path, vfs_stat_t *st);
int vfs_mkdir(const char *path);
int vfs_create(const char *path); // touch
int vfs_write_file(const char *path, const void *data, uint32_t len, int append);
int vfs_read(const char *path, uint32_t offset, void *buf, uint32_t len); // jumlah byte atau error
int vfs_remove(const char *path);                                         // rm (file)
int vfs_rmdir(const char *path);                                          // direktori kosong
int vfs_readdir(const char *path, uint32_t index, vfs_dirent_t *out);     // 0=ada, 1=habis
void vfs_info(vfs_info_t *info);
int vfs_check(uint32_t *problems);

#endif