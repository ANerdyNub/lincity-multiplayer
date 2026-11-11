/* ---------------------------------------------------------------------- *
 * mapwire.h
 * Fixed-width little-endian map serialization shared by the network code
 * and tests.  Nothing here depends on host struct layout, alignment, or
 * byte order: every field is written explicitly.
 * ---------------------------------------------------------------------- */

#ifndef __mapwire_h__
#define __mapwire_h__

#include <stddef.h>
#include <stdint.h>

#include "lin-city.h"

#define MAP_INFO_WIRE_BYTES 40u

static inline size_t
map_wire_bytes (int side_len)
{
    if (side_len <= 0 || side_len > WORLD_SIDE_LEN)
        return 0;
    return (size_t)(2u + 2u + MAP_INFO_WIRE_BYTES + 4u)
        * (size_t)side_len * (size_t)side_len;
}

#define MAP_WIRE_BYTES map_wire_bytes (WORLD_SIDE_LEN)

static inline void
map_put_u16le (unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static inline void
map_put_u32le (unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static inline uint16_t
map_get_u16le (const unsigned char *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t
map_get_u32le (const unsigned char *p)
{
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

/* Field by field, deliberately not a copy of Map_Point_Info. */
static inline unsigned char *
map_put_info (unsigned char *p, const Map_Point_Info *m)
{
    map_put_u32le (p, (uint32_t)m->population); p += 4;
    map_put_u32le (p, (uint32_t)m->flags);      p += 4;
    map_put_u16le (p, (uint16_t)m->coal_reserve); p += 2;
    map_put_u16le (p, (uint16_t)m->ore_reserve);  p += 2;
    map_put_u32le (p, (uint32_t)m->int_1); p += 4;
    map_put_u32le (p, (uint32_t)m->int_2); p += 4;
    map_put_u32le (p, (uint32_t)m->int_3); p += 4;
    map_put_u32le (p, (uint32_t)m->int_4); p += 4;
    map_put_u32le (p, (uint32_t)m->int_5); p += 4;
    map_put_u32le (p, (uint32_t)m->int_6); p += 4;
    map_put_u32le (p, (uint32_t)m->int_7); p += 4;
    return p;
}

static inline const unsigned char *
map_get_info (const unsigned char *p, Map_Point_Info *m)
{
    m->population = (int)map_get_u32le (p); p += 4;
    m->flags = (int)map_get_u32le (p);      p += 4;
    m->coal_reserve = (unsigned short)map_get_u16le (p); p += 2;
    m->ore_reserve = (unsigned short)map_get_u16le (p);  p += 2;
    m->int_1 = (int)map_get_u32le (p); p += 4;
    m->int_2 = (int)map_get_u32le (p); p += 4;
    m->int_3 = (int)map_get_u32le (p); p += 4;
    m->int_4 = (int)map_get_u32le (p); p += 4;
    m->int_5 = (int)map_get_u32le (p); p += 4;
    m->int_6 = (int)map_get_u32le (p); p += 4;
    m->int_7 = (int)map_get_u32le (p); p += 4;
    return p;
}

static inline unsigned char *
map_wire_encode (int side_len,
    short type[][WORLD_SIDE_LEN],
    short group[][WORLD_SIDE_LEN],
    Map_Point_Info info[][WORLD_SIDE_LEN],
    int pollution[][WORLD_SIDE_LEN],
    unsigned char *out)
{
    unsigned char *p = out;
    int x, y;

    if (!out || map_wire_bytes (side_len) == 0)
        return NULL;
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            map_put_u16le (p, (uint16_t)type[x][y]);
            p += 2;
        }
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            map_put_u16le (p, (uint16_t)group[x][y]);
            p += 2;
        }
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++)
            p = map_put_info (p, &info[x][y]);
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            map_put_u32le (p, (uint32_t)pollution[x][y]);
            p += 4;
        }
    }
    return p;
}

static inline const unsigned char *
map_wire_decode (int side_len,
    const unsigned char *in,
    short type[][WORLD_SIDE_LEN],
    short group[][WORLD_SIDE_LEN],
    Map_Point_Info info[][WORLD_SIDE_LEN],
    int pollution[][WORLD_SIDE_LEN])
{
    const unsigned char *p = in;
    int x, y;

    if (!in || map_wire_bytes (side_len) == 0)
        return NULL;
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            type[x][y] = (short)map_get_u16le (p);
            p += 2;
        }
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            group[x][y] = (short)map_get_u16le (p);
            p += 2;
        }
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++)
            p = map_get_info (p, &info[x][y]);
    }
    for (x = 0; x < side_len; x++) {
        for (y = 0; y < side_len; y++) {
            pollution[x][y] = (int)map_get_u32le (p);
            p += 4;
        }
    }
    return p;
}

#endif /* __mapwire_h__ */
