#ifndef SEVENTHOS_RAMDISK_H
#define SEVENTHOS_RAMDISK_H

#include <stdint.h>
#include "blockdev.h"

int ramdisk_create(blockdev_t *dev, const char *name, uint32_t blocks); // 0 = sukses

#endif