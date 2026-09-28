#include "dmsdio_internal.h"
#include "dmsdio_sd.h"
#include <string.h>

/*
 * Card identification and bus negotiation (SD Physical Layer Simplified
 * Specification, 4.2 "Card Identification Mode" and 4.3 "Data Transfer
 * Mode"). Runs with the context lock held.
 */

#define SSR_BUS_WIDTH_4BIT      2u
#define SWITCH_GROUP1_SUPPORT   415u    /* bits 415:400 */
#define SWITCH_GROUP1_RESULT    379u    /* bits 379:376 */

static int power_cycle(struct dmdrvi_context* ctx)
{
    dmsdio_instance_t instance = ctx->config.instance;
    uint32_t actual_hz = 0;

    int ret = dmsdio_port_set_power(instance, false);
    if (ret == 0)
    {
        dmosi_thread_sleep(1);
        ret = dmsdio_port_set_power(instance, true);
    }
    if (ret == 0)
    {
        ret = dmsdio_port_set_bus_width(instance, dmsdio_bus_width_1bit);
    }
    if (ret == 0)
    {
        ret = dmsdio_port_set_clock(instance, SD_CLOCK_IDENTIFICATION_HZ, &actual_hz);
    }
    if (ret == 0 && (actual_hz == 0 || actual_hz > SD_CLOCK_IDENTIFICATION_HZ))
    {
        DMOD_LOG_ERROR("dmsdio: identification clock %u Hz out of range\n", (unsigned)actual_hz);
        ret = -ERANGE;
    }
    if (ret == 0)
    {
        dmosi_thread_sleep(SD_POWER_UP_DELAY_MS);
    }
    return ret;
}

/* CMD8: a v1.x card does not answer, a v2.0+ card must echo the pattern. */
static int send_if_cond(struct dmdrvi_context* ctx, bool* v2)
{
    dmsdio_response_t resp;
    uint32_t arg = SD_IF_COND_VHS_27_36 | SD_IF_COND_CHECK_PATTERN;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_SEND_IF_COND, arg, dmsdio_response_short, &resp);
    if (ret == -ETIMEDOUT)
    {
        *v2 = false;
        return 0;
    }
    if (ret != 0)
    {
        return ret;
    }
    if ((resp.words[0] & 0xFFFu) != arg)
    {
        DMOD_LOG_ERROR("dmsdio: CMD8 echo mismatch (0x%03x)\n", (unsigned)(resp.words[0] & 0xFFFu));
        return -EPROTO;
    }
    *v2 = true;
    return 0;
}

static int op_cond_once(struct dmdrvi_context* ctx, uint32_t arg, uint32_t* ocr)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_app(ctx, SD_ACMD_SD_SEND_OP_COND, arg, dmsdio_response_short_no_crc, &resp);
    if (ret == 0)
    {
        *ocr = resp.words[0];
    }
    return ret;
}

/* ACMD41 until the card reports power-up complete. */
static int send_op_cond(struct dmdrvi_context* ctx, bool v2, uint32_t* ocr)
{
    uint32_t arg = SD_OCR_VOLTAGE_WINDOW | (v2 ? SD_OCR_HCS : 0u);
    uint32_t waited = 0;
    for (;;)
    {
        int ret = op_cond_once(ctx, arg, ocr);
        if (ret == -ETIMEDOUT && waited == 0)
        {
            return -ENODEV;     /* nothing answers CMD55: no SD memory card */
        }
        if (ret != 0)
        {
            return ret;
        }
        if ((*ocr & SD_OCR_VOLTAGE_WINDOW) == 0)
        {
            return -ENOTSUP;
        }
        if (*ocr & SD_OCR_BUSY)
        {
            return 0;
        }
        if (waited >= ctx->config.init_timeout_ms)
        {
            DMOD_LOG_ERROR("dmsdio: card did not finish power-up in %u ms\n",
                           (unsigned)ctx->config.init_timeout_ms);
            return -ETIMEDOUT;
        }
        dmosi_thread_sleep(SD_ACMD41_POLL_MS);
        waited += SD_ACMD41_POLL_MS;
    }
}

static int read_cid_and_rca(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_ALL_SEND_CID, 0, dmsdio_response_long, &resp);
    if (ret != 0)
    {
        return ret;
    }
    memcpy(card->cid_raw, resp.words, sizeof(card->cid_raw));
    dmsdio_decode_cid(card->cid_raw, &card->cid);

    ret = dmsdio_cmd_send(ctx, SD_CMD_SEND_RELATIVE_ADDR, 0, dmsdio_response_short, &resp);
    if (ret != 0)
    {
        return ret;
    }
    card->rca = (uint16_t)(resp.words[0] >> 16);
    if ((resp.words[0] & SD_R6_ERRORS) != 0 || card->rca == 0)
    {
        DMOD_LOG_ERROR("dmsdio: invalid CMD3 response 0x%08x\n", (unsigned)resp.words[0]);
        return -EPROTO;
    }
    ctx->card.rca = card->rca;  /* used by the CMD55/CMD13 helpers from here on */
    return 0;
}

static int read_csd(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_SEND_CSD, (uint32_t)card->rca << 16,
                              dmsdio_response_long, &resp);
    if (ret != 0)
    {
        return ret;
    }
    memcpy(card->csd_raw, resp.words, sizeof(card->csd_raw));
    ret = dmsdio_decode_csd(card->csd_raw, &card->csd);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("dmsdio: unsupported or malformed CSD (%d)\n", ret);
        return ret;
    }
    card->capacity_bytes  = card->csd.capacity_bytes;
    card->block_count     = card->capacity_bytes / DMSDIO_BLOCK_SIZE;
    card->write_protected = card->csd.perm_write_protect || card->csd.tmp_write_protect;
    return 0;
}

/* Cross-check OCR (CCS) against the CSD structure and name the card family. */
static int classify(dmsdio_card_info_t* card, bool v2)
{
    bool ccs = (card->ocr & SD_OCR_HCS) != 0;
    card->block_addressing = ccs;
    if (!ccs)
    {
        card->type = v2 ? dmsdio_card_type_sdsc_v2 : dmsdio_card_type_sdsc_v1;
        return card->csd.structure == 0 ? 0 : -EPROTO;
    }
    if (!v2 || card->csd.structure != 1)
    {
        return -EPROTO;
    }
    card->type = (card->capacity_bytes > 32ull * 1024u * 1024u * 1024u)
               ? dmsdio_card_type_sdxc : dmsdio_card_type_sdhc;
    return 0;
}

static int identify(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    bool v2 = false;
    int ret = power_cycle(ctx);
    if (ret == 0)
    {
        ret = dmsdio_cmd_send(ctx, SD_CMD_GO_IDLE_STATE, 0, dmsdio_response_none, NULL);
    }
    if (ret == 0)
    {
        ret = send_if_cond(ctx, &v2);
    }
    if (ret == 0)
    {
        ret = send_op_cond(ctx, v2, &card->ocr);
    }
    if (ret == 0)
    {
        ret = read_cid_and_rca(ctx, card);
    }
    if (ret == 0)
    {
        ret = read_csd(ctx, card);
    }
    return ret == 0 ? classify(card, v2) : ret;
}

static int set_clock(struct dmdrvi_context* ctx, dmsdio_card_info_t* card, uint32_t hz)
{
    return dmsdio_port_set_clock(ctx->config.instance,
                                 dmsdio_min_u32(hz, ctx->config.max_clock_hz), &card->clock_hz);
}

static int select_card(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    int ret = set_clock(ctx, card, SD_CLOCK_DEFAULT_SPEED_HZ);
    if (ret == 0)
    {
        ret = dmsdio_cmd_send(ctx, SD_CMD_SELECT_CARD, (uint32_t)card->rca << 16,
                              dmsdio_response_short_busy, NULL);
    }
    if (ret == 0)
    {
        ret = dmsdio_cmd_wait_ready(ctx, ctx->config.write_timeout_ms);
    }
    if (ret == 0 && !card->block_addressing)
    {
        dmsdio_response_t resp;
        ret = dmsdio_cmd_send(ctx, SD_CMD_SET_BLOCKLEN, DMSDIO_BLOCK_SIZE,
                              dmsdio_response_short, &resp);
    }
    return ret;
}

static int read_register(struct dmdrvi_context* ctx, uint8_t acmd, uint32_t size)
{
    dmsdio_data_t data = {
        .buffer      = ctx->scratch,
        .block_size  = size,
        .block_count = 1,
        .direction   = dmsdio_direction_read,
        .timeout_ms  = ctx->config.read_timeout_ms,
    };
    return dmsdio_cmd_data(ctx, true, acmd, 0, &data, NULL);
}

static int read_scr(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    int ret = read_register(ctx, SD_ACMD_SEND_SCR, SD_SCR_SIZE);
    if (ret == 0)
    {
        ret = dmsdio_decode_scr(ctx->scratch, &card->scr);
    }
    return ret;
}

static int negotiate_bus_width(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    card->bus_width = dmsdio_bus_width_1bit;
    if (ctx->config.max_bus_width != dmsdio_bus_width_4bit || !card->scr.bus_width_4bit)
    {
        return 0;
    }
    /* Disconnect the card's internal DAT3 pull-up before driving 4 lines. */
    int ret = dmsdio_cmd_app(ctx, SD_ACMD_SET_CLR_CARD_DETECT, 0, dmsdio_response_short, NULL);
    if (ret == 0)
    {
        ret = dmsdio_cmd_app(ctx, SD_ACMD_SET_BUS_WIDTH, 2, dmsdio_response_short, NULL);
    }
    if (ret == 0)
    {
        ret = dmsdio_port_set_bus_width(ctx->config.instance, dmsdio_bus_width_4bit);
    }
    if (ret == 0)
    {
        card->bus_width = dmsdio_bus_width_4bit;
    }
    return ret;
}

static int read_ssr(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    int ret = read_register(ctx, SD_ACMD_SD_STATUS, SD_SSR_SIZE);
    if (ret == 0)
    {
        ret = dmsdio_decode_ssr(ctx->scratch, &card->ssr);
    }
    if (ret == 0 && card->bus_width == dmsdio_bus_width_4bit &&
        card->ssr.bus_width_code != SSR_BUS_WIDTH_4BIT)
    {
        DMOD_LOG_ERROR("dmsdio: card did not switch to the 4-bit bus\n");
        ret = -EPROTO;
    }
    return ret;
}

/* Extract `width` bits ending at `msb` from the 512-bit switch status. */
static uint32_t switch_bits(const uint8_t* raw, unsigned msb, unsigned width)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < width; i++)
    {
        unsigned bit = msb + 1u - width + i;
        value |= (uint32_t)((raw[63u - bit / 8u] >> (bit % 8u)) & 1u) << i;
    }
    return value;
}

static int switch_function(struct dmdrvi_context* ctx, uint32_t arg)
{
    dmsdio_data_t data = {
        .buffer      = ctx->scratch,
        .block_size  = SD_SWITCH_STATUS_SIZE,
        .block_count = 1,
        .direction   = dmsdio_direction_read,
        .timeout_ms  = ctx->config.read_timeout_ms,
    };
    return dmsdio_cmd_data(ctx, false, SD_CMD_SWITCH_FUNC, arg, &data, NULL);
}

/* CMD6 High Speed switch - succeeds only if the returned status confirms it. */
static int switch_high_speed(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    card->high_speed = false;
    if (!ctx->config.high_speed || card->scr.sd_spec < 1 ||
        (card->csd.ccc & SD_CCC_SWITCH) == 0 || ctx->config.max_clock_hz <= SD_CLOCK_DEFAULT_SPEED_HZ)
    {
        return 0;
    }
    int ret = switch_function(ctx, SD_SWITCH_CHECK | SD_SWITCH_HIGH_SPEED);
    if (ret != 0 || (switch_bits(ctx->scratch, SWITCH_GROUP1_SUPPORT, 16) & 0x2u) == 0)
    {
        return ret;
    }
    ret = switch_function(ctx, SD_SWITCH_SET | SD_SWITCH_HIGH_SPEED);
    if (ret != 0)
    {
        return ret;
    }
    if (switch_bits(ctx->scratch, SWITCH_GROUP1_RESULT, 4) != SD_SWITCH_HIGH_SPEED)
    {
        DMOD_LOG_WARN("dmsdio: card refused High Speed, staying at default speed\n");
        return 0;
    }
    card->high_speed = true;
    return set_clock(ctx, card, SD_CLOCK_HIGH_SPEED_HZ);
}

static int configure_transfer_mode(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    int ret = select_card(ctx, card);
    if (ret == 0)
    {
        ret = read_scr(ctx, card);
    }
    if (ret == 0)
    {
        ret = negotiate_bus_width(ctx, card);
    }
    if (ret == 0)
    {
        ret = read_ssr(ctx, card);
    }
    if (ret == 0)
    {
        ret = switch_high_speed(ctx, card);
    }
    return ret;
}

int dmsdio_ident_run(struct dmdrvi_context* ctx, dmsdio_card_info_t* card)
{
    memset(card, 0, sizeof(*card));
    ctx->card.rca = 0;

    int ret = identify(ctx, card);
    if (ret == 0)
    {
        ret = configure_transfer_mode(ctx, card);
    }
    if (ret != 0)
    {
        card->type = dmsdio_card_type_none;
    }
    return ret;
}
