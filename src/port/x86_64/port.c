#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "mock_control.h"
#include "dmod.h"

#include <string.h>

/**
 * @brief x86_64 host-native mock of a single SD card, for dmsdio_test.c
 *
 * Understands exactly the CMD/ACMD sequence dmsdio.c actually sends (see
 * identify_card()/transfer_blocks() there) and nothing else - this is not
 * a general-purpose SD simulator, just enough of one to drive dmsdio.c's
 * own state machine through every path the mocked protocol tests exercise.
 */

#define MOCK_BACKING_BLOCKS  64U /* small on purpose - tests only ever touch a handful of blocks */
#define MOCK_RCA             0x1234U

typedef struct
{
    bool     app_cmd_pending; /* true immediately after CMD55 - next index is an ACMD */
    bool     selected;        /* true after CMD7 */
    bool     removed;

    dmsdio_card_type_t card_type;
    uint64_t           capacity_blocks;
    bool               high_speed_supported;
    bool               bus_4bit_supported;
    bool               high_speed_selected;

    dmsdio_error_t inject_error;
    uint32_t       inject_count;

    uint8_t backing_store[MOCK_BACKING_BLOCKS * DMSDIO_BLOCK_SIZE];
} mock_state_t;

static mock_state_t g_mock;

/* ---- Encoders - the exact inverse of dmsdio.c's parse_cid()/parse_csd()/
 * parse_scr(), field-for-field. Kept private to this file: no real port
 * ever needs to *build* a response, only decode one. ---- */

static void encode_cid(dmsdio_response_t *r)
{
    memset(r, 0, sizeof(*r));
    uint8_t mid = 0x03;
    char oid[2] = { 'D', 'M' };
    char pnm[5] = { 'M', 'O', 'C', 'K', '0' };
    uint8_t prv = 0x10;
    uint32_t psn = 0xDEADBEEFUL;
    uint8_t mdt_year_offset = 25; /* 2025 */
    uint8_t mdt_month = 6;

    r->words[0] = ((uint32_t)mid << 24) | ((uint32_t)(uint8_t)oid[0] << 16) |
                  ((uint32_t)(uint8_t)oid[1] << 8) | (uint32_t)(uint8_t)pnm[0];
    r->words[1] = ((uint32_t)(uint8_t)pnm[1] << 24) | ((uint32_t)(uint8_t)pnm[2] << 16) |
                  ((uint32_t)(uint8_t)pnm[3] << 8) | (uint32_t)(uint8_t)pnm[4];
    r->words[2] = ((uint32_t)prv << 24) | ((psn >> 8) & 0x00FFFFFFUL);
    uint16_t mdt = (uint16_t)(((uint32_t)mdt_year_offset << 4) | mdt_month);
    r->words[3] = ((psn & 0xFFUL) << 24) | ((uint32_t)mdt << 8) | 0x01UL;
}

static void encode_csd(dmsdio_response_t *r, dmsdio_card_type_t card_type, uint64_t capacity_blocks, bool high_speed_supported)
{
    memset(r, 0, sizeof(*r));

    bool is_block_addressed = (card_type == dmsdio_card_type_sdhc || card_type == dmsdio_card_type_sdxc);
    uint8_t structure_version = is_block_addressed ? 1U : 0U;
    uint8_t tran_speed = high_speed_supported ? 0x5AU : 0x32U; /* 50 MHz : 25 MHz */

    r->words[0] = ((uint32_t)structure_version << 30) | tran_speed; /* CSD_STRUCTURE [127:126], TRAN_SPEED [103:96] */

    if (!is_block_addressed)
    {
        /* CSD 1.0: READ_BL_LEN=9 (512), C_SIZE_MULT=0 ->
         * capacity_blocks = (C_SIZE+1) * 4. */
        uint32_t read_bl_len = 9U;
        uint32_t c_size_mult = 0U;
        uint32_t c_size = (uint32_t)(capacity_blocks / 4U);
        c_size = (c_size > 0U) ? (c_size - 1U) : 0U;
        if (c_size > 0xFFFU) c_size = 0xFFFU;

        r->words[1] |= (read_bl_len & 0xFU) << 16;   /* READ_BL_LEN [83:80] */
        r->words[1] |= (c_size >> 2) & 0x3FFU;       /* C_SIZE[73:64] */
        r->words[2] |= (c_size & 0x3U) << 30;        /* C_SIZE[63:62] */
        r->words[2] |= (c_size_mult & 0x7U) << 15;   /* C_SIZE_MULT [49:47] */
    }
    else
    {
        /* CSD 2.0: capacity_blocks = (C_SIZE+1) * 1024. */
        uint32_t c_size = (uint32_t)(capacity_blocks / 1024U);
        c_size = (c_size > 0U) ? (c_size - 1U) : 0U;

        r->words[1] |= (c_size >> 16) & 0x3FU;       /* C_SIZE [69:64] */
        r->words[2] |= (c_size & 0xFFFFU) << 16;     /* C_SIZE [63:48] */
    }
}

static void encode_scr(uint8_t *buffer, bool bus_4bit_supported)
{
    memset(buffer, 0, DMSDIO_BLOCK_SIZE > 8 ? 8 : DMSDIO_BLOCK_SIZE);
    buffer[0] = 0x02; /* SCR_STRUCTURE=0, SD_SPEC=2 (Version 2.00) */
    buffer[1] = bus_4bit_supported ? 0x05U : 0x01U; /* SD_BUS_WIDTHS: bit0=1bit always, bit2=4bit */
}

static void encode_switch_status(uint8_t *buffer, bool accept_high_speed)
{
    memset(buffer, 0, 64);
    buffer[16] = accept_high_speed ? 0x01U : 0x0FU; /* group 1 selected function, low nibble */
}

/* ---- Mock control ---- */

dmod_dmsdio_port_api_declaration(1.0, void, _mock_reset, ( dmsdio_card_type_t card_type, uint64_t capacity_blocks ))
{
    memset(&g_mock, 0, sizeof(g_mock));
    g_mock.card_type = card_type;
    g_mock.capacity_blocks = capacity_blocks;
    g_mock.high_speed_supported = true;
    g_mock.bus_4bit_supported = true;

    for (uint32_t i = 0; i < sizeof(g_mock.backing_store); i++)
    {
        g_mock.backing_store[i] = (uint8_t)i;
    }
}

dmod_dmsdio_port_api_declaration(1.0, void, _mock_set_removed, ( bool removed ))
{
    g_mock.removed = removed;
}

dmod_dmsdio_port_api_declaration(1.0, void, _mock_set_high_speed_supported, ( bool supported ))
{
    g_mock.high_speed_supported = supported;
}

dmod_dmsdio_port_api_declaration(1.0, void, _mock_set_4bit_supported, ( bool supported ))
{
    g_mock.bus_4bit_supported = supported;
}

dmod_dmsdio_port_api_declaration(1.0, void, _mock_inject_error, ( dmsdio_error_t error, uint32_t count ))
{
    g_mock.inject_error = error;
    g_mock.inject_count = count;
}

dmod_dmsdio_port_api_declaration(1.0, uint8_t*, _mock_get_backing_store, ( void ))
{
    return g_mock.backing_store;
}

/* Applied uniformly by every primitive below - a single knob for "the next
 * N low-level operations fail", regardless of whether that operation is a
 * command or a data phase (a real transient bus fault would not
 * distinguish either). */
static bool consume_injected_error(dmsdio_error_t *out_error)
{
    if (g_mock.removed)
    {
        *out_error = dmsdio_error_removed;
        return true;
    }
    if (g_mock.inject_count > 0)
    {
        g_mock.inject_count--;
        *out_error = g_mock.inject_error;
        return true;
    }
    return false;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("dmsdio port module initialized (x86_64 mock)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("dmsdio port module deinitialized (x86_64 mock)\n");
    return 0;
}

/* ---- Lifecycle ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _init, ( dmsdio_instance_t instance ))
{
    (void)instance;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _deinit, ( dmsdio_instance_t instance ))
{
    (void)instance;
    return 0;
}

/* ---- Bus configuration - no real bus to configure ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    (void)instance; (void)width;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_speed_mode, ( dmsdio_instance_t instance, dmsdio_speed_mode_t mode ))
{
    (void)instance; (void)mode;
    return 0;
}

/* ---- Commands ---- */

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _send_command,
    ( dmsdio_instance_t instance, uint8_t cmd_index, uint32_t argument,
      dmsdio_response_type_t response_type, dmsdio_response_t *response ))
{
    (void)instance;

    dmsdio_error_t injected;
    if (consume_injected_error(&injected))
    {
        g_mock.app_cmd_pending = false;
        return injected;
    }

    bool is_acmd = g_mock.app_cmd_pending;
    g_mock.app_cmd_pending = false;

    if (response != NULL)
    {
        memset(response, 0, sizeof(*response));
    }

    if (!is_acmd)
    {
        switch (cmd_index)
        {
            case 0: /* GO_IDLE_STATE */
                return dmsdio_error_none;

            case 8: /* SEND_IF_COND - every mocked card is 2.0+ */
                if (response != NULL) response->words[3] = argument & 0xFFFUL;
                return dmsdio_error_none;

            case 2: /* ALL_SEND_CID */
                if (response != NULL) encode_cid(response);
                return dmsdio_error_none;

            case 3: /* SEND_RELATIVE_ADDR */
                if (response != NULL) response->words[3] = (uint32_t)MOCK_RCA << 16;
                return dmsdio_error_none;

            case 9: /* SEND_CSD */
                if (response != NULL) encode_csd(response, g_mock.card_type, g_mock.capacity_blocks, g_mock.high_speed_supported);
                return dmsdio_error_none;

            case 7: /* SELECT_DESELECT_CARD */
                g_mock.selected = (argument != 0);
                return dmsdio_error_none;

            case 6: /* SWITCH_FUNC (mode=1, group1) - only meaningful once selected */
                g_mock.high_speed_selected = g_mock.high_speed_supported;
                return dmsdio_error_none;

            case 12: /* STOP_TRANSMISSION */
            case 13: /* SEND_STATUS - READY_FOR_DATA always set in the mock */
                if (response != NULL) response->words[3] = (1UL << 8);
                return dmsdio_error_none;

            case 16: /* SET_BLOCKLEN */
            case 17: /* READ_SINGLE_BLOCK */
            case 18: /* READ_MULTIPLE_BLOCK */
            case 24: /* WRITE_BLOCK */
            case 25: /* WRITE_MULTIPLE_BLOCK */
                if ((uint64_t)(argument / DMSDIO_BLOCK_SIZE) >= g_mock.capacity_blocks &&
                    (g_mock.card_type == dmsdio_card_type_sdhc || g_mock.card_type == dmsdio_card_type_sdxc))
                {
                    if (response != NULL) response->words[3] = (1UL << 31); /* OUT_OF_RANGE */
                }
                return dmsdio_error_none;

            case 32: /* ERASE_WR_BLK_START */
            case 33: /* ERASE_WR_BLK_END */
            case 38: /* ERASE */
                return dmsdio_error_none;

            case 55: /* APP_CMD */
                g_mock.app_cmd_pending = true;
                return dmsdio_error_none;

            default:
                return dmsdio_error_none;
        }
    }

    /* ACMD */
    switch (cmd_index)
    {
        case 41: /* SD_SEND_OP_COND */
            if (response != NULL)
            {
                uint32_t ocr = (1UL << 31); /* power-up done, immediately, no busy-poll needed in tests */
                bool is_hc = (g_mock.card_type == dmsdio_card_type_sdhc || g_mock.card_type == dmsdio_card_type_sdxc);
                if (is_hc) ocr |= (1UL << 30);
                response->words[3] = ocr;
            }
            return dmsdio_error_none;

        case 6: /* SET_BUS_WIDTH */
            return dmsdio_error_none;

        case 51: /* SEND_SCR */
        case 13: /* SD_STATUS */
        default:
            return dmsdio_error_none;
    }
}

/* ---- Data transfer ---- */

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _read_blocks,
    ( dmsdio_instance_t instance, void *buffer, uint32_t block_size, uint32_t block_count ))
{
    (void)instance;

    dmsdio_error_t injected;
    if (consume_injected_error(&injected))
    {
        return injected;
    }

    if (block_size == DMSDIO_BLOCK_SIZE)
    {
        /* Real sector read - serve straight from the backing store, wrapping
         * so a test can address any block index without needing a
         * full-capacity-sized buffer. */
        for (uint32_t i = 0; i < block_count; i++)
        {
            uint32_t offset = (i % MOCK_BACKING_BLOCKS) * DMSDIO_BLOCK_SIZE;
            memcpy((uint8_t *)buffer + (size_t)i * block_size, g_mock.backing_store + offset, block_size);
        }
    }
    else if (block_size == 8U)
    {
        encode_scr((uint8_t *)buffer, g_mock.bus_4bit_supported);
    }
    else if (block_size == 64U)
    {
        encode_switch_status((uint8_t *)buffer, g_mock.high_speed_supported);
    }
    else
    {
        return dmsdio_error_not_supported;
    }

    return dmsdio_error_none;
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_error_t, _write_blocks,
    ( dmsdio_instance_t instance, const void *buffer, uint32_t block_size, uint32_t block_count ))
{
    (void)instance;

    dmsdio_error_t injected;
    if (consume_injected_error(&injected))
    {
        return injected;
    }

    if (block_size != DMSDIO_BLOCK_SIZE)
    {
        return dmsdio_error_not_supported;
    }

    for (uint32_t i = 0; i < block_count; i++)
    {
        uint32_t offset = (i % MOCK_BACKING_BLOCKS) * DMSDIO_BLOCK_SIZE;
        memcpy(g_mock.backing_store + offset, (const uint8_t *)buffer + (size_t)i * block_size, block_size);
    }

    return dmsdio_error_none;
}
