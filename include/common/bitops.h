/**
 * @file
 */
#ifndef __BITOPS_H__
#define __BITOPS_H__

#include <sys/param.h>

#define BITS_PER_CHAR 8
#define BITS_PER_LONG (BITS_PER_CHAR * sizeof(long))

/* Find last bit set:
 * the index of the last bit that's set, or 0 if value is zero.
 */
static inline unsigned long _flsl(unsigned long word)
{
    return word ? sizeof(long) * BITS_PER_CHAR - __builtin_clz(word) : 0;
}

static inline void clear_bit(unsigned long bit, unsigned long *word)
{
    *word &= ~(1 << bit);
}

static inline void set_bit(unsigned long bit, unsigned long *word)
{
    *word |= (1 << bit);
}

static inline void bitmap_clear_bit(unsigned long *map, unsigned long bit)
{
    clear_bit(bit % BITS_PER_LONG, &map[bit / BITS_PER_LONG]);
}

static inline void bitmap_set_bit(unsigned long *map, unsigned long bit)
{
    set_bit(bit % BITS_PER_LONG, &map[bit / BITS_PER_LONG]);
}

static inline unsigned long bitmap_get_bit(unsigned long *map,
                                           unsigned long bit)
{
    return map[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG) & 1;
}

static inline unsigned long find_first_bit(const unsigned long *addr,
                                           unsigned long size)
{
    for (unsigned long i = 0; i * BITS_PER_LONG < size; i++) {
        if (addr[i]) {
            return MIN(i * BITS_PER_LONG + __builtin_ffsl(addr[i]) - 1, size);
        }
    }

    return size;
}

static inline unsigned long find_first_zero_bit(const unsigned long *addr,
                                                unsigned long size)
{
    for (unsigned long i = 0; i * BITS_PER_LONG < size; i++) {
        if (addr[i] != ~0ul) {
            return MIN(i * BITS_PER_LONG + __builtin_ffsl(~addr[i]) - 1, size);
        }
    }

    return size;
}

#endif
