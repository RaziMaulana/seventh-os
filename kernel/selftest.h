#ifndef SEVENTHOS_SELFTEST_H
#define SEVENTHOS_SELFTEST_H

#include <stdint.h>

uint32_t selftest_run(int verbose);   
void     selftest_exit(int success);   

#endif