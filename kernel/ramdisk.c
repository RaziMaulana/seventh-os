#include <stddef.h>
#include <stdint.h>

#include "heap.h"
#include "kstring.h"
#include "ramdisk.h"

static int ram_read(blockdev_t *dev, uint32_t lba, void *buffer)
{
    memcpy(buffer, (uint8_t *)dev->private_data + (size_t)lba * dev->block_size,
           dev->block_size);
    return 0;
}

static int ram_write(blockdev_t *dev, uint32_t lba, const void *buffer)
{
    memcpy((uint8_t *)dev->private_data + (size_t)lba * dev->block_size, buffer,
           dev->block_size);
    return 0;
}

int ramdisk_create(blockdev_t *dev, const char *name, uint32_t blocks)
{
    void *memory = kcalloc(blocks, BLOCK_SIZE); // disk "fisik" = memori heap kernel

    if (memory == NULL)
        return -1;

    dev->name = name;
    dev->block_size = BLOCK_SIZE;
    dev->block_count = blocks;
    dev->read_block = ram_read;
    dev->write_block = ram_write;
    dev->private_data = memory;
    dev->reads = 0;
    dev->writes = 0;
    return 0;
}