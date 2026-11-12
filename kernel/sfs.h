#ifndef SEVENTHOS_SFS_H
#define SEVENTHOS_SFS_H

#include <stdint.h>
#include "blockdev.h"
#include "vfs.h"

#define SFS_MAGIC 0x37534653u // "SFS7"
#define SFS_INODE_COUNT 128u
#define SFS_DIRECT 12u
#define SFS_INODE_SIZE 64u
#define SFS_DIRENT_SIZE 32u
#define SFS_INODES_PER_BLOCK (BLOCK_SIZE / SFS_INODE_SIZE)
#define SFS_ROOT_INODE 1u

typedef struct
{
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_bitmap_block;
    uint32_t block_bitmap_block;
    uint32_t inode_table_block;
    uint32_t inode_table_blocks;
    uint32_t data_start;
    uint32_t root_inode;
} sfs_super_t;

typedef struct
{
    blockdev_t *dev;
    sfs_super_t sb;
    uint8_t inode_bitmap[BLOCK_SIZE]; // salinan di RAM, write-through ke disk
    uint8_t block_bitmap[BLOCK_SIZE];
    uint32_t free_inodes;
    uint32_t free_blocks;
} sfs_t;

int sfs_format(blockdev_t *dev);
int sfs_mount(blockdev_t *dev, sfs_t *fs);

extern const vfs_fs_ops_t sfs_ops;

#endif