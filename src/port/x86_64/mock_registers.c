/*
 * Card registers of the mock SD card (CID, CSD, SCR, SD Status, CMD6
 * switch status) and its sparse block storage.
 */
#include "mock_card.h"
#include "dmod.h"
#include <string.h>

/* Set `width` bits ending at `msb` in a 128-bit register (MSW first). */
static void reg128_set(uint32_t reg[4], unsigned msb, unsigned width, uint64_t value)
{
    unsigned lsb = msb + 1u - width;
    for (unsigned i = 0; i < width; i++)
    {
        unsigned bit = lsb + i;
        uint32_t mask = 1u << (bit % 32u);
        if ((value >> i) & 1u)
        {
            reg[3u - bit / 32u] |= mask;
        }
        else
        {
            reg[3u - bit / 32u] &= ~mask;
        }
    }
}

uint64_t mock_card_capacity_blocks(dmsdio_card_type_t type)
{
    switch (type)
    {
        case dmsdio_card_type_sdsc_v1: return 1ull << 21;   /* 1 GiB  */
        case dmsdio_card_type_sdsc_v2: return 1ull << 22;   /* 2 GiB  */
        case dmsdio_card_type_sdhc:    return 1ull << 24;   /* 8 GiB  */
        case dmsdio_card_type_sdxc:    return 1ull << 27;   /* 64 GiB */
        default:                       return 0;
    }
}

void mock_card_cid(const mock_host_t* host, uint32_t out[4])
{
    static const char name[5] = { 'D', 'M', 'S', 'D', '0' };
    memset(out, 0, 4 * sizeof(uint32_t));
    reg128_set(out, 127, 8, 0x03);                      /* MID */
    reg128_set(out, 119, 8, 'S');                       /* OID */
    reg128_set(out, 111, 8, 'D');
    for (unsigned i = 0; i < 5; i++)
    {
        reg128_set(out, 103 - 8 * i, 8, (uint64_t)(name[i] + (i == 4 ? host->type : 0)));
    }
    reg128_set(out, 63, 8, 0x10);                       /* PRV 1.0 */
    reg128_set(out, 55, 32, 0xC0DE0000u + host->type);  /* PSN */
    reg128_set(out, 19, 8, 24);                         /* MDT year 2024 */
    reg128_set(out, 11, 4, 9);                          /* MDT month 9 */
    reg128_set(out, 0, 1, 1);
}

static void csd_common(const mock_host_t* host, uint32_t out[4])
{
    reg128_set(out, 103, 8, 0x32);                      /* TRAN_SPEED 25 MHz */
    reg128_set(out, 95, 12, 0x5B5);                     /* CCC incl. classes 5, 10 */
    reg128_set(out, 46, 1, 1);                          /* ERASE_BLK_EN */
    reg128_set(out, 45, 7, 0x7F);                       /* SECTOR_SIZE */
    reg128_set(out, 12, 1, host->write_protect ? 1 : 0);/* TMP_WRITE_PROTECT */
    reg128_set(out, 0, 1, 1);
}

void mock_card_csd(const mock_host_t* host, uint32_t out[4])
{
    memset(out, 0, 4 * sizeof(uint32_t));
    csd_common(host, out);
    if (host->type == dmsdio_card_type_sdsc_v1 || host->type == dmsdio_card_type_sdsc_v2)
    {
        /* CSD v1: (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN */
        uint32_t bl_len = (host->type == dmsdio_card_type_sdsc_v1) ? 9u : 10u;
        reg128_set(out, 127, 2, 0);
        reg128_set(out, 83, 4, bl_len);
        reg128_set(out, 73, 12, 4095);
        reg128_set(out, 49, 3, 7);
        reg128_set(out, 25, 4, bl_len);
        return;
    }
    /* CSD v2: (C_SIZE + 1) * 512 KiB */
    uint64_t c_size = mock_card_capacity_blocks(host->type) / 1024u - 1u;
    reg128_set(out, 127, 2, 1);
    reg128_set(out, 83, 4, 9);
    reg128_set(out, 69, 22, c_size);
    reg128_set(out, 25, 4, 9);
}

void mock_card_scr(const mock_host_t* host, uint8_t out[8])
{
    bool v1 = (host->type == dmsdio_card_type_sdsc_v1);
    bool xc = (host->type == dmsdio_card_type_sdxc);
    memset(out, 0, 8);
    out[0] = v1 ? 0x00 : 0x02;                          /* SCR_STRUCTURE 0, SD_SPEC */
    out[1] = (uint8_t)((v1 ? 0x00 : 0x20) | 0x05);      /* SD_SECURITY, SD_BUS_WIDTHS 1+4 */
    out[2] = xc ? 0x80 : 0x00;                          /* SD_SPEC3 */
    out[3] = xc ? 0x02 : 0x00;                          /* CMD_SUPPORT: CMD23 */
}

void mock_card_ssr(const mock_host_t* host, uint8_t out[64])
{
    memset(out, 0, 64);
    out[0]  = host->card_4bit ? 0x80 : 0x00;            /* DAT_BUS_WIDTH */
    out[8]  = 0x04;                                     /* SPEED_CLASS 10 */
    out[10] = 0x90;                                     /* AU_SIZE 4 MiB */
    out[11] = 0x00;
    out[12] = 0x01;                                     /* ERASE_SIZE 1 AU */
    out[13] = (uint8_t)((1u << 2) | 1u);                /* ERASE_TIMEOUT 1 s, OFFSET 1 s */
    out[24] = (host->type == dmsdio_card_type_sdxc) ? 0x02 : 0x00; /* DISCARD_SUPPORT */
}

void mock_card_switch_status(mock_host_t* host, uint32_t arg, uint8_t out[64])
{
    uint32_t requested = arg & 0xFu;
    bool set = (arg & 0x80000000u) != 0;
    bool ok  = (requested == 0xFu || requested <= 1u) && !(requested == 1u && host->refuse_high_speed);
    uint32_t result = (requested == 0xFu) ? (host->high_speed ? 1u : 0u) : (ok ? requested : 0xFu);

    memset(out, 0, 64);
    out[1]  = 0x64;                                     /* max current 100 mA */
    out[12] = 0x80;                                     /* group 1 support: 0, 1, 15 */
    out[13] = host->refuse_high_speed ? 0x01 : 0x03;
    out[16] = (uint8_t)(result & 0xFu);                 /* group 1 result */
    if (set && ok && requested != 0xFu)
    {
        host->high_speed = (requested == 1u);
    }
}

/* ---- sparse storage ---- */

void mock_store_clear(mock_host_t* host)
{
    Dmod_Free(host->blocks);
    host->blocks = NULL;
    host->block_used = 0;
}

static mock_block_t* find_block(const mock_host_t* host, uint64_t lba)
{
    for (uint32_t i = 0; i < host->block_used; i++)
    {
        if (host->blocks[i].lba == lba)
        {
            return &host->blocks[i];
        }
    }
    return NULL;
}

void mock_store_read(const mock_host_t* host, uint64_t lba, uint8_t* out)
{
    const mock_block_t* block = find_block(host, lba);
    if (block != NULL)
    {
        memcpy(out, block->data, MOCK_BLOCK_SIZE);
        return;
    }
    for (uint32_t i = 0; i < MOCK_BLOCK_SIZE; i++)
    {
        out[i] = dmsdio_mock_pattern(lba, i);
    }
}

int mock_store_write(mock_host_t* host, uint64_t lba, const uint8_t* in)
{
    mock_block_t* block = find_block(host, lba);
    if (block == NULL)
    {
        if (host->blocks == NULL)
        {
            host->blocks = Dmod_Malloc(sizeof(mock_block_t) * DMSDIO_MOCK_MAX_STORED_BLOCKS);
        }
        if (host->blocks == NULL || host->block_used >= DMSDIO_MOCK_MAX_STORED_BLOCKS)
        {
            return -1;
        }
        block = &host->blocks[host->block_used++];
        block->lba = lba;
    }
    memcpy(block->data, in, MOCK_BLOCK_SIZE);
    return 0;
}
