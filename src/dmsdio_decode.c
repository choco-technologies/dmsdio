#include "dmsdio_internal.h"
#include <string.h>

/*
 * Register decoders. Bit positions follow the SD Physical Layer
 * Simplified Specification (CID: 5.2, CSD: 5.3, SCR: 5.6, SD Status: 4.10.2).
 */

#define CSD_CAPACITY_UNIT_V2    (512ull * 1024ull)

/* Extract `width` bits ending at `msb` from a 128-bit register (MSW first). */
static uint32_t reg128_bits(const uint32_t raw[4], unsigned msb, unsigned width)
{
    unsigned lsb   = msb + 1u - width;
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++)
    {
        unsigned bit  = lsb + i;
        uint32_t word = raw[3u - bit / 32u];
        value |= ((word >> (bit % 32u)) & 1u) << i;
    }
    return value;
}

/* Extract `width` bits ending at `msb` from a big-endian byte register. */
static uint32_t bytes_bits(const uint8_t* raw, size_t total_bits, unsigned msb, unsigned width)
{
    unsigned lsb   = msb + 1u - width;
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++)
    {
        unsigned bit  = lsb + i;
        uint8_t  byte = raw[total_bits / 8u - 1u - bit / 8u];
        value |= (uint32_t)((byte >> (bit % 8u)) & 1u) << i;
    }
    return value;
}

static int decode_csd_capacity(const uint32_t raw[4], dmsdio_csd_t* csd)
{
    if (csd->structure == 0)
    {
        uint64_t c_size = reg128_bits(raw, 73, 12);
        uint32_t mult   = reg128_bits(raw, 49, 3);
        if (csd->read_bl_len < 9 || csd->read_bl_len > 11)
        {
            return -EPROTO;
        }
        csd->capacity_bytes = (c_size + 1u) << (mult + 2u + csd->read_bl_len);
        return 0;
    }
    if (csd->structure == 1)
    {
        uint64_t c_size = reg128_bits(raw, 69, 22);
        if (csd->read_bl_len != 9)
        {
            return -EPROTO;
        }
        csd->capacity_bytes = (c_size + 1u) * CSD_CAPACITY_UNIT_V2;
        return 0;
    }
    /* CSD v3 (SDUC) needs extended addressing that dmsdio does not implement. */
    return -ENOTSUP;
}

dmod_dmsdio_api_declaration(1.0, int, _decode_csd, ( const uint32_t raw[4], dmsdio_csd_t* csd ))
{
    if (raw == NULL || csd == NULL)
    {
        return -EINVAL;
    }
    memset(csd, 0, sizeof(*csd));
    csd->structure          = (uint8_t)reg128_bits(raw, 127, 2);
    csd->tran_speed         = (uint8_t)reg128_bits(raw, 103, 8);
    csd->ccc                = (uint16_t)reg128_bits(raw, 95, 12);
    csd->read_bl_len        = (uint8_t)reg128_bits(raw, 83, 4);
    csd->erase_blk_en       = reg128_bits(raw, 46, 1) != 0;
    csd->sector_size        = (uint8_t)(reg128_bits(raw, 45, 7) + 1u);
    csd->perm_write_protect = reg128_bits(raw, 13, 1) != 0;
    csd->tmp_write_protect  = reg128_bits(raw, 12, 1) != 0;

    int ret = decode_csd_capacity(raw, csd);
    if (ret == 0 && csd->capacity_bytes == 0)
    {
        ret = -EPROTO;
    }
    return ret;
}

dmod_dmsdio_api_declaration(1.0, int, _decode_cid, ( const uint32_t raw[4], dmsdio_cid_t* cid ))
{
    if (raw == NULL || cid == NULL)
    {
        return -EINVAL;
    }
    memset(cid, 0, sizeof(*cid));
    cid->manufacturer_id = (uint8_t)reg128_bits(raw, 127, 8);
    for (unsigned i = 0; i < 2; i++)
    {
        cid->oem_id[i] = (char)reg128_bits(raw, 119 - 8 * i, 8);
    }
    for (unsigned i = 0; i < 5; i++)
    {
        cid->product_name[i] = (char)reg128_bits(raw, 103 - 8 * i, 8);
    }
    cid->product_revision  = (uint8_t)reg128_bits(raw, 63, 8);
    cid->serial_number     = reg128_bits(raw, 55, 32);
    cid->manufacture_year  = (uint16_t)(2000u + reg128_bits(raw, 19, 8));
    cid->manufacture_month = (uint8_t)reg128_bits(raw, 11, 4);
    return 0;
}

dmod_dmsdio_api_declaration(1.0, int, _decode_scr, ( const uint8_t raw[8], dmsdio_scr_t* scr ))
{
    if (raw == NULL || scr == NULL)
    {
        return -EINVAL;
    }
    memset(scr, 0, sizeof(*scr));
    uint32_t structure  = bytes_bits(raw, 64, 63, 4);
    uint32_t bus_widths = bytes_bits(raw, 64, 51, 4);
    uint32_t cmd_support = bytes_bits(raw, 64, 33, 2);

    scr->sd_spec               = (uint8_t)bytes_bits(raw, 64, 59, 4);
    scr->data_stat_after_erase = bytes_bits(raw, 64, 55, 1) != 0;
    scr->sd_spec3              = bytes_bits(raw, 64, 47, 1) != 0;
    scr->bus_width_1bit        = (bus_widths & 0x1u) != 0;
    scr->bus_width_4bit        = (bus_widths & 0x4u) != 0;
    scr->cmd23_supported       = (cmd_support & 0x2u) != 0;

    return (structure != 0 || !scr->bus_width_1bit) ? -EPROTO : 0;
}

static uint32_t au_size_bytes(uint32_t code)
{
    static const uint32_t large_mib[] = { 8, 12, 16, 24, 32, 64 };
    if (code == 0)
    {
        return 0;
    }
    if (code <= 9)
    {
        return (16u * 1024u) << (code - 1u);
    }
    return large_mib[code - 10u] * 1024u * 1024u;
}

dmod_dmsdio_api_declaration(1.0, int, _decode_ssr, ( const uint8_t raw[64], dmsdio_ssr_t* ssr ))
{
    if (raw == NULL || ssr == NULL)
    {
        return -EINVAL;
    }
    memset(ssr, 0, sizeof(*ssr));
    ssr->bus_width_code    = (uint8_t)bytes_bits(raw, 512, 511, 2);
    ssr->speed_class       = (uint8_t)bytes_bits(raw, 512, 447, 8);
    ssr->au_size_bytes     = au_size_bytes(bytes_bits(raw, 512, 431, 4));
    ssr->erase_size        = (uint16_t)bytes_bits(raw, 512, 423, 16);
    ssr->erase_timeout_s   = (uint8_t)bytes_bits(raw, 512, 407, 6);
    ssr->erase_offset_s    = (uint8_t)bytes_bits(raw, 512, 401, 2);
    ssr->discard_supported = bytes_bits(raw, 512, 313, 1) != 0;
    return 0;
}
