#include <stddef.h>
#include <stdint.h>

#include "kstring.h"
#include "sfs.h"
#include "timer.h"

typedef struct
{
    uint8_t type; // 0 bebas, VFS_TYPE_FILE, VFS_TYPE_DIR
    uint8_t pad[3];
    uint32_t size;
    uint32_t direct[SFS_DIRECT]; // LBA blok data; 0 = belum dialokasi
    uint32_t mtime;
    uint32_t reserved;
} sfs_inode_t;

typedef struct
{
    uint32_t inode; // 0 = slot kosong
    char name[SFS_DIRENT_SIZE - 4];
} sfs_dirent_t;

_Static_assert(sizeof(sfs_inode_t) == SFS_INODE_SIZE, "inode harus 64 byte");
_Static_assert(sizeof(sfs_dirent_t) == SFS_DIRENT_SIZE, "dirent harus 32 byte");
_Static_assert(SFS_DIRENT_SIZE - 4 == VFS_NAME_MAX + 1, "nama maksimum 27 + NUL");

/* ---------- bitmap ---------- */

static int bit_test(const uint8_t *map, uint32_t i) { return (map[i >> 3] >> (i & 7u)) & 1u; }
static void bit_set(uint8_t *map, uint32_t i) { map[i >> 3] |= (uint8_t)(1u << (i & 7u)); }
static void bit_clear(uint8_t *map, uint32_t i) { map[i >> 3] &= (uint8_t)~(1u << (i & 7u)); }

static int flush_inode_bitmap(sfs_t *fs)
{
    return blockdev_write(fs->dev, fs->sb.inode_bitmap_block, fs->inode_bitmap);
}

static int flush_block_bitmap(sfs_t *fs)
{
    return blockdev_write(fs->dev, fs->sb.block_bitmap_block, fs->block_bitmap);
}

/* ---------- inode ---------- */

static int read_inode(sfs_t *fs, uint32_t ino, sfs_inode_t *out)
{
    uint8_t buf[BLOCK_SIZE];

    if (ino == 0 || ino >= fs->sb.inode_count)
        return VFS_EINVAL;
    if (blockdev_read(fs->dev, fs->sb.inode_table_block + ino / SFS_INODES_PER_BLOCK, buf) != 0)
        return VFS_EIO;
    memcpy(out, buf + (ino % SFS_INODES_PER_BLOCK) * SFS_INODE_SIZE, SFS_INODE_SIZE);
    return VFS_OK;
}

static int write_inode(sfs_t *fs, uint32_t ino, const sfs_inode_t *in)
{
    uint8_t buf[BLOCK_SIZE];
    uint32_t lba;

    if (ino == 0 || ino >= fs->sb.inode_count)
        return VFS_EINVAL;
    lba = fs->sb.inode_table_block + ino / SFS_INODES_PER_BLOCK;
    if (blockdev_read(fs->dev, lba, buf) != 0)
        return VFS_EIO;
    memcpy(buf + (ino % SFS_INODES_PER_BLOCK) * SFS_INODE_SIZE, in, SFS_INODE_SIZE);
    return (blockdev_write(fs->dev, lba, buf) == 0) ? VFS_OK : VFS_EIO;
}

static uint32_t alloc_inode(sfs_t *fs)
{
    for (uint32_t i = 1; i < fs->sb.inode_count; i++)
    {
        if (!bit_test(fs->inode_bitmap, i))
        {
            bit_set(fs->inode_bitmap, i);
            if (flush_inode_bitmap(fs) != 0)
            {
                bit_clear(fs->inode_bitmap, i);
                return 0;
            }
            fs->free_inodes--;
            return i;
        }
    }
    return 0;
}

static void free_inode(sfs_t *fs, uint32_t ino)
{
    bit_clear(fs->inode_bitmap, ino);
    flush_inode_bitmap(fs);
    fs->free_inodes++;
}

/* ---------- blok data (mengembalikan LBA; 0 = gagal) ---------- */

static uint32_t alloc_block(sfs_t *fs)
{
    static const uint8_t zero[BLOCK_SIZE];
    uint32_t count = fs->sb.total_blocks - fs->sb.data_start;

    for (uint32_t i = 0; i < count; i++)
    {
        if (bit_test(fs->block_bitmap, i))
            continue;

        uint32_t lba = fs->sb.data_start + i;

        if (blockdev_write(fs->dev, lba, zero) != 0) // blok baru selalu bersih
            return 0;
        bit_set(fs->block_bitmap, i);
        if (flush_block_bitmap(fs) != 0)
        {
            bit_clear(fs->block_bitmap, i);
            return 0;
        }
        fs->free_blocks--;
        return lba;
    }
    return 0;
}

static void free_block(sfs_t *fs, uint32_t lba)
{
    bit_clear(fs->block_bitmap, lba - fs->sb.data_start);
    flush_block_bitmap(fs);
    fs->free_blocks++;
}

/* ---------- baca/tulis isi inode (dipakai file DAN direktori) ---------- */

static int inode_read_raw(sfs_t *fs, const sfs_inode_t *in, uint32_t off, void *dst, uint32_t len)
{
    uint8_t buf[BLOCK_SIZE];
    uint8_t *out = dst;
    uint32_t done = 0;

    if (off >= in->size)
        return 0;
    if (len > in->size - off)
        len = in->size - off;

    while (done < len)
    {
        uint32_t pos = off + done;
        uint32_t idx = pos / BLOCK_SIZE;
        uint32_t boff = pos % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - boff;

        if (chunk > len - done)
            chunk = len - done;

        if (idx >= SFS_DIRECT || in->direct[idx] == 0)
        {
            memset(out + done, 0, chunk);
        }
        else
        {
            if (blockdev_read(fs->dev, in->direct[idx], buf) != 0)
                return VFS_EIO;
            memcpy(out + done, buf + boff, chunk);
        }
        done += chunk;
    }
    return (int)done;
}

// Menulis dan menyimpan inode (ukuran, mtime). Tidak boleh ada "lubang": off <= size.
static int inode_write_raw(sfs_t *fs, uint32_t ino, sfs_inode_t *in,
                           uint32_t off, const void *src, uint32_t len)
{
    uint8_t buf[BLOCK_SIZE];
    const uint8_t *data = src;
    uint32_t done = 0;

    if (len == 0)
        return 0;
    if (off > in->size)
        return VFS_EINVAL;
    if (off + len < off || off + len > SFS_DIRECT * BLOCK_SIZE)
        return VFS_EFBIG;

    // Pra-periksa: hitung blok baru yang dibutuhkan agar tidak menulis setengah jalan.
    uint32_t need = 0;
    for (uint32_t b = off / BLOCK_SIZE; b <= (off + len - 1u) / BLOCK_SIZE; b++)
        if (in->direct[b] == 0)
            need++;
    if (need > fs->free_blocks)
        return VFS_ENOSPC;

    while (done < len)
    {
        uint32_t pos = off + done;
        uint32_t idx = pos / BLOCK_SIZE;
        uint32_t boff = pos % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - boff;

        if (chunk > len - done)
            chunk = len - done;

        if (in->direct[idx] == 0)
        {
            uint32_t lba = alloc_block(fs);
            if (lba == 0)
                break;
            in->direct[idx] = lba;
        }
        if (blockdev_read(fs->dev, in->direct[idx], buf) != 0) // read-modify-write
            return VFS_EIO;
        memcpy(buf + boff, data + done, chunk);
        if (blockdev_write(fs->dev, in->direct[idx], buf) != 0)
            return VFS_EIO;
        done += chunk;
    }

    if (off + done > in->size)
        in->size = off + done;
    in->mtime = timer_get_ticks();
    if (write_inode(fs, ino, in) != VFS_OK)
        return VFS_EIO;
    return (done > 0) ? (int)done : VFS_ENOSPC;
}

static int inode_truncate(sfs_t *fs, uint32_t ino, sfs_inode_t *in)
{
    for (uint32_t i = 0; i < SFS_DIRECT; i++)
    {
        if (in->direct[i] != 0)
        {
            free_block(fs, in->direct[i]);
            in->direct[i] = 0;
        }
    }
    in->size = 0;
    in->mtime = timer_get_ticks();
    return write_inode(fs, ino, in);
}

/* ---------- direktori ---------- */

static int name_eq(const char *a, const char *b)
{
    uint32_t i = 0;

    while (a[i] != '\0' && a[i] == b[i])
        i++;
    return a[i] == b[i];
}

// Mengembalikan indeks slot (>= 0), VFS_ENOENT, atau error lain.
static int dir_find(sfs_t *fs, const sfs_inode_t *dir, const char *name, uint32_t *ino_out)
{
    sfs_dirent_t e;

    for (uint32_t off = 0; off + SFS_DIRENT_SIZE <= dir->size; off += SFS_DIRENT_SIZE)
    {
        if (inode_read_raw(fs, dir, off, &e, SFS_DIRENT_SIZE) != (int)SFS_DIRENT_SIZE)
            return VFS_EIO;
        if (e.inode != 0 && name_eq(e.name, name))
        {
            if (ino_out != NULL)
                *ino_out = e.inode;
            return (int)(off / SFS_DIRENT_SIZE);
        }
    }
    return VFS_ENOENT;
}

static int dir_add(sfs_t *fs, uint32_t dir_ino, sfs_inode_t *dir, const char *name, uint32_t ino)
{
    sfs_dirent_t e, probe;
    uint32_t off;

    memset(&e, 0, sizeof(e));
    e.inode = ino;
    for (uint32_t i = 0; i < VFS_NAME_MAX && name[i] != '\0'; i++)
        e.name[i] = name[i];

    for (off = 0; off + SFS_DIRENT_SIZE <= dir->size; off += SFS_DIRENT_SIZE)
    {
        if (inode_read_raw(fs, dir, off, &probe, SFS_DIRENT_SIZE) != (int)SFS_DIRENT_SIZE)
            return VFS_EIO;
        if (probe.inode == 0)
            break; // pakai ulang slot kosong
    } // jika tidak ada: off == size -> tambah di ujung

    int rc = inode_write_raw(fs, dir_ino, dir, off, &e, SFS_DIRENT_SIZE);

    if (rc == (int)SFS_DIRENT_SIZE)
        return VFS_OK;
    return (rc == VFS_EFBIG) ? VFS_ENOSPC : (rc < 0 ? rc : VFS_EIO);
}

static int dir_clear_slot(sfs_t *fs, uint32_t dir_ino, sfs_inode_t *dir, uint32_t slot)
{
    sfs_dirent_t e;

    memset(&e, 0, sizeof(e));
    return (inode_write_raw(fs, dir_ino, dir, slot * SFS_DIRENT_SIZE, &e, SFS_DIRENT_SIZE) == (int)SFS_DIRENT_SIZE) ? VFS_OK : VFS_EIO;
}

static int dir_is_empty(sfs_t *fs, const sfs_inode_t *dir)
{
    sfs_dirent_t e;

    for (uint32_t off = 0; off + SFS_DIRENT_SIZE <= dir->size; off += SFS_DIRENT_SIZE)
    {
        if (inode_read_raw(fs, dir, off, &e, SFS_DIRENT_SIZE) != (int)SFS_DIRENT_SIZE)
            return 0;
        if (e.inode != 0)
            return 0;
    }
    return 1;
}

/* ---------- operasi VFS ---------- */

static uint32_t op_root(void *ctx)
{
    return ((sfs_t *)ctx)->sb.root_inode;
}

static int op_lookup(void *ctx, uint32_t dir_ino, const char *name, uint32_t *ino)
{
    sfs_t *fs = ctx;
    sfs_inode_t dir;
    int rc = read_inode(fs, dir_ino, &dir);

    if (rc != VFS_OK)
        return rc;
    if (dir.type != VFS_TYPE_DIR)
        return VFS_ENOTDIR;
    rc = dir_find(fs, &dir, name, ino);
    return (rc < 0) ? rc : VFS_OK;
}

static int op_create(void *ctx, uint32_t dir_ino, const char *name, uint8_t type, uint32_t *ino_out)
{
    sfs_t *fs = ctx;
    sfs_inode_t dir, in;
    int rc = read_inode(fs, dir_ino, &dir);

    if (rc != VFS_OK)
        return rc;
    if (dir.type != VFS_TYPE_DIR)
        return VFS_ENOTDIR;

    int found = dir_find(fs, &dir, name, NULL);
    if (found >= 0)
        return VFS_EEXIST;
    if (found != VFS_ENOENT)
        return found;

    uint32_t ino = alloc_inode(fs);
    if (ino == 0)
        return VFS_ENOSPC;

    memset(&in, 0, sizeof(in));
    in.type = type;
    in.mtime = timer_get_ticks();

    rc = write_inode(fs, ino, &in);
    if (rc == VFS_OK)
        rc = dir_add(fs, dir_ino, &dir, name, ino);
    if (rc != VFS_OK)
    { // rollback
        free_inode(fs, ino);
        return rc;
    }
    if (ino_out != NULL)
        *ino_out = ino;
    return VFS_OK;
}

static int op_remove(void *ctx, uint32_t dir_ino, const char *name)
{
    sfs_t *fs = ctx;
    sfs_inode_t dir, in;
    uint32_t ino;
    int rc = read_inode(fs, dir_ino, &dir);

    if (rc != VFS_OK)
        return rc;
    if (dir.type != VFS_TYPE_DIR)
        return VFS_ENOTDIR;

    int slot = dir_find(fs, &dir, name, &ino);
    if (slot < 0)
        return slot;

    rc = read_inode(fs, ino, &in);
    if (rc != VFS_OK)
        return rc;
    if (in.type == VFS_TYPE_DIR && !dir_is_empty(fs, &in))
        return VFS_ENOTEMPTY;

    rc = dir_clear_slot(fs, dir_ino, &dir, (uint32_t)slot);
    if (rc != VFS_OK)
        return rc;

    for (uint32_t i = 0; i < SFS_DIRECT; i++)
        if (in.direct[i] != 0)
            free_block(fs, in.direct[i]);

    memset(&in, 0, sizeof(in));
    write_inode(fs, ino, &in);
    free_inode(fs, ino);
    return VFS_OK;
}

static int op_read(void *ctx, uint32_t ino, uint32_t off, void *buf, uint32_t len)
{
    sfs_t *fs = ctx;
    sfs_inode_t in;
    int rc = read_inode(fs, ino, &in);

    if (rc != VFS_OK)
        return rc;
    if (in.type != VFS_TYPE_FILE)
        return VFS_EISDIR;
    return inode_read_raw(fs, &in, off, buf, len);
}

static int op_write(void *ctx, uint32_t ino, uint32_t off, const void *buf, uint32_t len)
{
    sfs_t *fs = ctx;
    sfs_inode_t in;
    int rc = read_inode(fs, ino, &in);

    if (rc != VFS_OK)
        return rc;
    if (in.type != VFS_TYPE_FILE)
        return VFS_EISDIR;
    return inode_write_raw(fs, ino, &in, off, buf, len);
}

static int op_truncate(void *ctx, uint32_t ino)
{
    sfs_t *fs = ctx;
    sfs_inode_t in;
    int rc = read_inode(fs, ino, &in);

    if (rc != VFS_OK)
        return rc;
    if (in.type != VFS_TYPE_FILE)
        return VFS_EISDIR;
    return inode_truncate(fs, ino, &in);
}

static int op_stat(void *ctx, uint32_t ino, vfs_stat_t *st)
{
    sfs_inode_t in;
    int rc = read_inode((sfs_t *)ctx, ino, &in);

    if (rc != VFS_OK)
        return rc;
    st->type = in.type;
    st->inode = ino;
    st->size = in.size;
    st->mtime = in.mtime;
    st->blocks = 0;
    for (uint32_t i = 0; i < SFS_DIRECT; i++)
        if (in.direct[i] != 0)
            st->blocks++;
    return VFS_OK;
}

static int op_readdir(void *ctx, uint32_t dir_ino, uint32_t index, vfs_dirent_t *out)
{
    sfs_t *fs = ctx;
    sfs_inode_t dir, child;
    sfs_dirent_t e;
    uint32_t seen = 0;
    int rc = read_inode(fs, dir_ino, &dir);

    if (rc != VFS_OK)
        return rc;
    if (dir.type != VFS_TYPE_DIR)
        return VFS_ENOTDIR;

    for (uint32_t off = 0; off + SFS_DIRENT_SIZE <= dir.size; off += SFS_DIRENT_SIZE)
    {
        if (inode_read_raw(fs, &dir, off, &e, SFS_DIRENT_SIZE) != (int)SFS_DIRENT_SIZE)
            return VFS_EIO;
        if (e.inode == 0)
            continue;
        if (seen++ != index)
            continue;

        uint32_t i = 0;
        for (; i < VFS_NAME_MAX && e.name[i] != '\0'; i++)
            out->name[i] = e.name[i];
        out->name[i] = '\0';
        out->type = 0;
        out->size = 0;
        if (read_inode(fs, e.inode, &child) == VFS_OK)
        {
            out->type = child.type;
            out->size = child.size;
        }
        return 0;
    }
    return 1; // akhir direktori
}

static void op_info(void *ctx, vfs_info_t *info)
{
    sfs_t *fs = ctx;

    info->fs_name = "SeventhFS";
    info->block_size = fs->sb.block_size;
    info->total_blocks = fs->sb.total_blocks - fs->sb.data_start;
    info->free_blocks = fs->free_blocks;
    info->total_inodes = fs->sb.inode_count - 1u;
    info->free_inodes = fs->free_inodes;
    info->max_file_size = SFS_DIRECT * BLOCK_SIZE;
}

// fsck ringan: pointer blok valid & ditandai terpakai, tak ada blok dipakai dua kali,
// tak ada blok "bocor", entri direktori menunjuk inode yang aktif, hitung bebas cocok.
static int op_check(void *ctx, uint32_t *problems_out)
{
    sfs_t *fs = ctx;
    sfs_inode_t in;
    sfs_dirent_t e;
    uint8_t refmap[BLOCK_SIZE];
    uint32_t problems = 0, set_blocks = 0;
    uint32_t data_blocks = fs->sb.total_blocks - fs->sb.data_start;

    memset(refmap, 0, sizeof(refmap));

    for (uint32_t ino = 1; ino < fs->sb.inode_count; ino++)
    {
        if (!bit_test(fs->inode_bitmap, ino))
            continue;
        if (read_inode(fs, ino, &in) != VFS_OK ||
            (in.type != VFS_TYPE_FILE && in.type != VFS_TYPE_DIR))
        {
            problems++;
            continue;
        }
        for (uint32_t d = 0; d < SFS_DIRECT; d++)
        {
            uint32_t lba = in.direct[d];

            if (lba == 0)
                continue;
            if (lba < fs->sb.data_start || lba >= fs->sb.total_blocks)
            {
                problems++;
                continue;
            }
            uint32_t i = lba - fs->sb.data_start;

            if (!bit_test(fs->block_bitmap, i))
                problems++; // dipakai, tapi tercatat bebas
            if (bit_test(refmap, i))
                problems++; // dipakai dua kali
            bit_set(refmap, i);
        }
        if (in.type == VFS_TYPE_DIR)
        {
            for (uint32_t off = 0; off + SFS_DIRENT_SIZE <= in.size; off += SFS_DIRENT_SIZE)
            {
                if (inode_read_raw(fs, &in, off, &e, SFS_DIRENT_SIZE) != (int)SFS_DIRENT_SIZE)
                {
                    problems++;
                    break;
                }
                if (e.inode != 0 &&
                    (e.inode >= fs->sb.inode_count || !bit_test(fs->inode_bitmap, e.inode)))
                    problems++; // entri menunjuk inode yang tidak aktif
            }
        }
    }

    for (uint32_t i = 0; i < data_blocks; i++)
    {
        if (bit_test(fs->block_bitmap, i))
        {
            set_blocks++;
            if (!bit_test(refmap, i))
                problems++; // blok bocor (terpakai tapi tak ada pemilik)
        }
    }
    if (data_blocks - set_blocks != fs->free_blocks)
        problems++;

    *problems_out = problems;
    return VFS_OK;
}

const vfs_fs_ops_t sfs_ops = {
    .name = "SeventhFS",
    .root = op_root,
    .lookup = op_lookup,
    .create = op_create,
    .remove = op_remove,
    .read = op_read,
    .write = op_write,
    .truncate = op_truncate,
    .stat = op_stat,
    .readdir = op_readdir,
    .info = op_info,
    .check = op_check,
};

/* ---------- format & mount ---------- */

int sfs_format(blockdev_t *dev)
{
    static sfs_t fs;
    uint8_t blk[BLOCK_SIZE];
    sfs_inode_t root;

    memset(&fs, 0, sizeof(fs));
    memset(blk, 0, sizeof(blk));

    fs.dev = dev;
    fs.sb.magic = SFS_MAGIC;
    fs.sb.block_size = BLOCK_SIZE;
    fs.sb.total_blocks = dev->block_count;
    fs.sb.inode_count = SFS_INODE_COUNT;
    fs.sb.inode_bitmap_block = 1;
    fs.sb.block_bitmap_block = 2;
    fs.sb.inode_table_block = 3;
    fs.sb.inode_table_blocks = SFS_INODE_COUNT / SFS_INODES_PER_BLOCK;
    fs.sb.data_start = 3u + fs.sb.inode_table_blocks;
    fs.sb.root_inode = SFS_ROOT_INODE;

    if (dev->block_size != BLOCK_SIZE ||
        dev->block_count <= fs.sb.data_start + 8u ||
        dev->block_count - fs.sb.data_start > BLOCK_SIZE * 8u)
        return VFS_EINVAL;

    for (uint32_t b = 0; b < fs.sb.data_start; b++) // bersihkan semua metadata
        if (blockdev_write(dev, b, blk) != 0)
            return VFS_EIO;

    memcpy(blk, &fs.sb, sizeof(fs.sb));
    if (blockdev_write(dev, 0, blk) != 0)
        return VFS_EIO;

    bit_set(fs.inode_bitmap, 0); // inode 0 dicadangkan
    bit_set(fs.inode_bitmap, SFS_ROOT_INODE);
    if (flush_inode_bitmap(&fs) != 0 || flush_block_bitmap(&fs) != 0)
        return VFS_EIO;

    memset(&root, 0, sizeof(root));
    root.type = VFS_TYPE_DIR;
    root.mtime = timer_get_ticks();
    return write_inode(&fs, SFS_ROOT_INODE, &root);
}

int sfs_mount(blockdev_t *dev, sfs_t *fs)
{
    uint8_t blk[BLOCK_SIZE];

    memset(fs, 0, sizeof(*fs));
    if (blockdev_read(dev, 0, blk) != 0)
        return VFS_EIO;
    memcpy(&fs->sb, blk, sizeof(fs->sb));

    if (fs->sb.magic != SFS_MAGIC || fs->sb.block_size != BLOCK_SIZE ||
        fs->sb.total_blocks != dev->block_count)
        return VFS_EINVAL;

    fs->dev = dev;
    if (blockdev_read(dev, fs->sb.inode_bitmap_block, fs->inode_bitmap) != 0 ||
        blockdev_read(dev, fs->sb.block_bitmap_block, fs->block_bitmap) != 0)
        return VFS_EIO;

    for (uint32_t i = 1; i < fs->sb.inode_count; i++)
        if (!bit_test(fs->inode_bitmap, i))
            fs->free_inodes++;
    for (uint32_t i = 0; i < fs->sb.total_blocks - fs->sb.data_start; i++)
        if (!bit_test(fs->block_bitmap, i))
            fs->free_blocks++;
    return VFS_OK;
}