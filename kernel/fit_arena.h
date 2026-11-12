#ifndef SEVENTHOS_FIT_ARENA_H
#define SEVENTHOS_FIT_ARENA_H

#include <stdint.h>

#define FIT_ARENA_UNITS 128u
#define FIT_MAX_BLOCKS 64u
#define FIT_RESERVED_ID 255u

#define FIT_ERR_FULL (-1) // total memori bebas lebih kecil dari permintaan
#define FIT_ERR_FRAG (-2) // total cukup, tetapi tidak ada SATU lubang yang muat

typedef enum
{
    FIT_FIRST = 0,
    FIT_BEST,
    FIT_WORST,
    FIT_COUNT
} fit_strategy_t;

typedef struct
{
    uint32_t start;
    uint32_t size;
    uint8_t free;
    uint8_t id; // 0 = bebas, 1..254 = alokasi, 255 = area tercadang
} fit_block_t;

typedef struct
{
    fit_block_t blocks[FIT_MAX_BLOCKS]; // berurutan menurut alamat, menutup seluruh arena
    uint32_t count;
    uint32_t steps;
    uint32_t allocs_ok;
    uint32_t fail_full;
    uint32_t fail_frag;
} fit_arena_t;

typedef struct
{
    uint32_t free_units;
    uint32_t used_units;
    uint32_t largest_free;
    uint32_t holes;
    uint32_t frag_percent; 
} fit_stats_t;

void fit_init(fit_arena_t *a);
void fit_setup_classic(fit_arena_t *a);
int fit_alloc(fit_arena_t *a, fit_strategy_t s, uint32_t size, uint8_t id); // start >= 0, atau FIT_ERR_*
int fit_free(fit_arena_t *a, uint8_t id);                                   // 0 / -1
void fit_get_stats(const fit_arena_t *a, fit_stats_t *out);
void fit_render_map(const fit_arena_t *a, char *out); // out >= FIT_ARENA_UNITS + 1
const char *fit_strategy_name(fit_strategy_t s);

#endif