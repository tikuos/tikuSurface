/*
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 * settings.c - checked coordinate records independent of storage layout.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "settings.h"
#include "touch.h"
#include <string.h>
/** @brief Compute the reflected CRC-32 of a settings prefix. */
static uint32_t checksum(const unsigned char *p, unsigned n)
{
    uint32_t crc = UINT32_MAX;
    for (unsigned i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
/** @brief Store a little-endian 16-bit field. */
static void put16(unsigned char *p, uint16_t value) { p[0] = (unsigned char)value; p[1] = (unsigned char)(value >> 8); }
/** @brief Read an unaligned little-endian 16-bit field. */
static uint16_t get16(const unsigned char *p) { return (uint16_t)(p[0] | (unsigned)p[1] << 8); }

int surface_settings_encode(const surface_touch_map_t *m, unsigned char out[SURFACE_SETTINGS_BYTES])
{
    if (!out || !surface_touch_map_valid(m)) return 0;
    memset(out, 0, SURFACE_SETTINGS_BYTES);
    memcpy(out, "TSCL", 4); out[4] = 1; out[5] = SURFACE_SETTINGS_BYTES;
    put16(out + 6, 800); put16(out + 8, 480);
    put16(out + 10, m->x_min); put16(out + 12, m->x_max);
    put16(out + 14, m->y_min); put16(out + 16, m->y_max);
    out[18] = m->swap; out[19] = m->invert_x; out[20] = m->invert_y;
    uint32_t crc = checksum(out, 28);
    for (unsigned i = 0; i < 4; ++i) out[28 + i] = (unsigned char)(crc >> (8u * i));
    return 1;
}

unsigned surface_settings_decode(const unsigned char *data, unsigned n, surface_touch_map_t *out)
{
    if (!data || !out || n != SURFACE_SETTINGS_BYTES || memcmp(data, "TSCL", 4)) return SURFACE_SETTINGS_CORRUPT;
    uint32_t crc = 0;
    for (unsigned i = 0; i < 4; ++i) crc |= (uint32_t)data[28 + i] << (8u * i);
    if (checksum(data, 28) != crc) return SURFACE_SETTINGS_CORRUPT;
    if (data[4] != 1 || data[5] != SURFACE_SETTINGS_BYTES || get16(data + 6) != 800 || get16(data + 8) != 480)
        return SURFACE_SETTINGS_INCOMPATIBLE;
    for (unsigned i = 21; i < 28; ++i) if (data[i]) return SURFACE_SETTINGS_INCOMPATIBLE;
    surface_touch_map_t m = {get16(data + 10), get16(data + 12), get16(data + 14), get16(data + 16),
        data[18], data[19], data[20]};
    if (!surface_touch_map_valid(&m)) return SURFACE_SETTINGS_CORRUPT;
    *out = m;
    return SURFACE_SETTINGS_SAVED;
}

unsigned surface_settings_load(int found, const unsigned char *record, unsigned length,
    surface_touch_map_t *map)
{
    if (found == 1) return SURFACE_SETTINGS_DEFAULT;
    if (found != 0) return SURFACE_SETTINGS_UNAVAILABLE;
    return surface_settings_decode(record, length, map);
}

unsigned surface_settings_stored(int result)
{
    return result == 0 ? SURFACE_SETTINGS_SAVED : SURFACE_SETTINGS_FAILED;
}

int surface_preferences_valid(const surface_preferences_t *p)
{return p&&p->background<6&&p->text<2&&p->html<3&&p->markdown<3;}
int surface_preferences_encode(const surface_preferences_t *p,unsigned char out[SURFACE_SETTINGS_BYTES])
{
    if(!out||!surface_preferences_valid(p))return 0;
    memset(out,0,SURFACE_SETTINGS_BYTES);memcpy(out,"TSDP",4);out[4]=1;out[5]=SURFACE_SETTINGS_BYTES;
    out[6]=p->background;out[7]=p->text;out[8]=p->html;out[9]=p->markdown;
    uint32_t crc=checksum(out,28);for(unsigned i=0;i<4;++i)out[28+i]=(unsigned char)(crc>>(8*i));return 1;
}
unsigned surface_preferences_decode(const unsigned char *data,unsigned n,surface_preferences_t *out)
{
    if(!data||!out||n!=SURFACE_SETTINGS_BYTES||memcmp(data,"TSDP",4))return SURFACE_SETTINGS_CORRUPT;
    uint32_t crc=0;for(unsigned i=0;i<4;++i)crc|=(uint32_t)data[28+i]<<(8*i);
    if(checksum(data,28)!=crc)return SURFACE_SETTINGS_CORRUPT;
    if(data[4]!=1||data[5]!=SURFACE_SETTINGS_BYTES)return SURFACE_SETTINGS_INCOMPATIBLE;
    for(unsigned i=10;i<28;++i)if(data[i])return SURFACE_SETTINGS_INCOMPATIBLE;
    surface_preferences_t p={data[6],data[7],data[8],data[9]};if(!surface_preferences_valid(&p))return SURFACE_SETTINGS_CORRUPT;
    *out=p;return SURFACE_SETTINGS_SAVED;
}
