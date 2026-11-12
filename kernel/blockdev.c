#include <stddef.h>

#include "blockdev.h"

int blockdev_read(blockdev_t *dev, uint32_t lba, void *buffer)
{
    if (dev == NULL || dev->read_block == NULL || lba >= dev->block_count)
        return -1;
    dev->reads++;
    return dev->read_block(dev, lba, buffer);
}

int blockdev_write(blockdev_t *dev, uint32_t lba, const void *buffer)
{
    if (dev == NULL || dev->write_block == NULL || lba >= dev->block_count)
        return -1;
    dev->writes++;
    return dev->write_block(dev, lba, buffer);
}