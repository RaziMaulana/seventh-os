#include <stddef.h>
#include <stdint.h>

#include "fit_arena.h"
#include "kstring.h"

const char *fit_strategy_name(fit_strategy_t s)
{
    static const char *const names[] = {"First Fit", "Best Fit", "Worst Fit"};

    return ((unsigned)s < FIT_COUNT) ? names[s] : "?";
}

void fit_init(fit_arena_t *a)
{
    memset(a, 0, sizeof(*a));
    a->blocks[0].start = 0;
    a->blocks[0].size = FIT_ARENA_UNITS;
    a->blocks[0].free = 1;
    a->count = 1;
}

static void insert_at(fit_arena_t *a, uint32_t index, fit_block_t blk)
{
    for (uint32_t i = a->count; i > index; i--)
        a->blocks[i] = a->blocks[i - 1u];
    a->blocks[index] = blk;
    a->count++;
}

static void remove_at(fit_arena_t *a, uint32_t index)
{
    for (uint32_t i = index; i + 1u < a->count; i++)
        a->blocks[i] = a->blocks[i + 1u];
    a->count--;
}

static uint32_t free_total(const fit_arena_t *a)
{
    uint32_t sum = 0;

    for (uint32_t i = 0; i < a->count; i++)
        if (a->blocks[i].free)
            sum += a->blocks[i].size;
    return sum;
}

// Satu-satunya perbedaan ketiga algoritma ada di fungsi ini.
static int pick_block(fit_arena_t *a, fit_strategy_t s, uint32_t size)
{
    int pick = -1;

    for (uint32_t i = 0; i < a->count; i++)
    {
        const fit_block_t *b = &a->blocks[i];

        a->steps++;
        if (!b->free || b->size < size)
            continue;
        if (s == FIT_FIRST)
            return (int)i; // berhenti di lubang pertama yang muat
        if (pick < 0 ||
            (s == FIT_BEST && b->size < a->blocks[pick].size) || // terkecil yang muat
            (s == FIT_WORST && b->size > a->blocks[pick].size))  // terbesar
            pick = (int)i;
    }
    return pick;
}

int fit_alloc(fit_arena_t *a, fit_strategy_t s, uint32_t size, uint8_t id)
{
    int idx = pick_block(a, s, size);

    if (size == 0 || idx < 0 || (a->blocks[idx].size > size && a->count >= FIT_MAX_BLOCKS))
    {
        if (free_total(a) < size)
        {
            a->fail_full++;
            return FIT_ERR_FULL;
        }
        a->fail_frag++;
        return FIT_ERR_FRAG;
    }

    if (a->blocks[idx].size > size)
    { // split: sisa menjadi lubang baru
        fit_block_t rest;

        rest.start = a->blocks[idx].start + size;
        rest.size = a->blocks[idx].size - size;
        rest.free = 1;
        rest.id = 0;
        a->blocks[idx].size = size;
        insert_at(a, (uint32_t)idx + 1u, rest);
    }
    a->blocks[idx].free = 0;
    a->blocks[idx].id = id;
    a->allocs_ok++;
    return (int)a->blocks[idx].start;
}

int fit_free(fit_arena_t *a, uint8_t id)
{
    for (uint32_t i = 0; i < a->count; i++)
    {
        if (a->blocks[i].free || a->blocks[i].id != id)
            continue;

        a->blocks[i].free = 1;
        a->blocks[i].id = 0;
        if (i + 1u < a->count && a->blocks[i + 1u].free)
        { // gabung dengan lubang sesudahnya
            a->blocks[i].size += a->blocks[i + 1u].size;
            remove_at(a, i + 1u);
        }
        if (i > 0 && a->blocks[i - 1u].free)
        { // gabung dengan lubang sebelumnya
            a->blocks[i - 1u].size += a->blocks[i].size;
            remove_at(a, i);
        }
        return 0;
    }
    return -1;
}

void fit_get_stats(const fit_arena_t *a, fit_stats_t *out)
{
    memset(out, 0, sizeof(*out));
    for (uint32_t i = 0; i < a->count; i++)
    {
        if (a->blocks[i].free)
        {
            out->free_units += a->blocks[i].size;
            out->holes++;
            if (a->blocks[i].size > out->largest_free)
                out->largest_free = a->blocks[i].size;
        }
        else
        {
            out->used_units += a->blocks[i].size;
        }
    }
    if (out->free_units > 0)
        out->frag_percent = (out->free_units - out->largest_free) * 100u / out->free_units;
}

void fit_render_map(const fit_arena_t *a, char *out)
{
    for (uint32_t i = 0; i < a->count; i++)
    {
        const fit_block_t *b = &a->blocks[i];
        char c;

        if (b->free)
            c = '.';
        else if (b->id == FIT_RESERVED_ID)
            c = '#';
        else
            c = (char)('A' + (b->id - 1u) % 26u);
        for (uint32_t u = 0; u < b->size; u++)
            out[b->start + u] = c;
    }
    out[FIT_ARENA_UNITS] = '\0';
}

// Contoh buku teks (diskalakan): lubang 6, 25, 10, 15, 30 dipisahkan area terpakai.
void fit_setup_classic(fit_arena_t *a)
{
    static const uint32_t holes[5] = {6, 25, 10, 15, 30};
    uint32_t pos = 0;

    memset(a, 0, sizeof(*a));
    for (uint32_t i = 0; i < 5; i++)
    {
        a->blocks[a->count].start = pos;
        a->blocks[a->count].size = holes[i];
        a->blocks[a->count].free = 1;
        a->count++;
        pos += holes[i];

        uint32_t gap = (i < 4) ? 2u : FIT_ARENA_UNITS - pos; // pemisah; yang terakhir = sisa arena
        a->blocks[a->count].start = pos;
        a->blocks[a->count].size = gap;
        a->blocks[a->count].free = 0;
        a->blocks[a->count].id = FIT_RESERVED_ID;
        a->count++;
        pos += gap;
    }
}