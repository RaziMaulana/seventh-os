#ifndef SEVENTHOS_BLOCKDEV_H
#define SEVENTHOS_BLOCKDEV_H

#include <stdint.h>

#define BLOCK_SIZE 512u

typedef struct blockdev
{
    const char *name;
    uint32_t block_size;
    uint32_t block_count;
    int (*read_block)(struct blockdev *dev, uint32_t lba, void *buffer);
    int (*write_block)(struct blockdev *dev, uint32_t lba, const void *buffer);
    void *private_data;
    uint32_t reads; // statistik I/O
    uint32_t writes;
} blockdev_t;

int blockdev_read(blockdev_t *dev, uint32_t lba, void *buffer);
int blockdev_write(blockdev_t *dev, uint32_t lba, const void *buffer);

#endif