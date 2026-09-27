#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmsdio.h"
#include "dmsdio_port.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmgpio.h"
#include "dmgpio_types.h"
#include "dmosi.h"

#include <errno.h>
#include <string.h>

/* "DSDI" packed into a uint32_t - see dmdrvi context magic-field convention */
#define DMSDIO_CONTEXT_MAGIC 0x44534449UL

/* ---- SD command indices (Physical Layer spec, section 4.7) ---- */
#define SD_CMD_GO_IDLE_STATE          0
#define SD_CMD_ALL_SEND_CID           2
#define SD_CMD_SEND_RELATIVE_ADDR     3
#define SD_CMD_SWITCH_FUNC            6
#define SD_CMD_SELECT_DESELECT_CARD   7
#define SD_CMD_SEND_IF_COND           8
#define SD_CMD_SEND_CSD               9
#define SD_CMD_STOP_TRANSMISSION      12
#define SD_CMD_SEND_STATUS            13
#define SD_CMD_SET_BLOCKLEN           16
#define SD_CMD_READ_SINGLE_BLOCK      17
#define SD_CMD_READ_MULTIPLE_BLOCK    18
#define SD_CMD_WRITE_BLOCK            24
#define SD_CMD_WRITE_MULTIPLE_BLOCK   25
#define SD_CMD_ERASE_WR_BLK_START     32
#define SD_CMD_ERASE_WR_BLK_END       33
#define SD_CMD_ERASE                  38
#define SD_CMD_APP_CMD                55

/* ACMD indices - always preceded by CMD55 (APP_CMD) with the card's RCA */
#define SD_ACMD_SET_BUS_WIDTH          6
#define SD_ACMD_SD_STATUS             13
#define SD_ACMD_SEND_OP_COND          41
#define SD_ACMD_SEND_SCR               51

/* CMD8 argument / R7 fields */
#define SD_CMD8_VOLTAGE_2V7_3V6  0x00000100UL
#define SD_CMD8_CHECK_PATTERN    0x000000AAUL
#define SD_CMD8_ARGUMENT         (SD_CMD8_VOLTAGE_2V7_3V6 | SD_CMD8_CHECK_PATTERN)

/* ACMD41 (OCR) argument / response bits */
#define SD_OCR_VOLTAGE_WINDOW_2V7_3V6  0x00FF8000UL
#define SD_OCR_HCS                     (1UL << 30) /* Host Capacity Support (argument) / Card Capacity Status (response) */
#define SD_OCR_BUSY                    (1UL << 31) /* 0 = card still powering up */

/* R1 card status error bits actually acted on (Physical Layer spec, section 4.10.1) */
#define SD_STATUS_OUT_OF_RANGE      (1UL << 31)
#define SD_STATUS_ADDRESS_ERROR     (1UL << 30)
#define SD_STATUS_BLOCK_LEN_ERROR   (1UL << 29)
#define SD_STATUS_CARD_ECC_FAILED   (1UL << 21)
#define SD_STATUS_CC_ERROR          (1UL << 20)
#define SD_STATUS_ERROR             (1UL << 19)
#define SD_STATUS_ERROR_MASK        (SD_STATUS_OUT_OF_RANGE | SD_STATUS_ADDRESS_ERROR | SD_STATUS_BLOCK_LEN_ERROR | \
                                      SD_STATUS_CARD_ECC_FAILED | SD_STATUS_CC_ERROR | SD_STATUS_ERROR)

/* SD Status / Switch Function status share the same 64-byte (512-bit) shape */
#define SD_SWITCH_STATUS_SIZE  64U
#define SD_SCR_SIZE            8U

/* Bounded loop guards - real hardware and the mocked port must both make
 * progress well inside these, so hitting the cap always means "genuinely
 * unreachable/broken", never "just needs one more spin". */
#define SD_OP_COND_MAX_ATTEMPTS       1000U  /* ACMD41 busy-poll, spec allows up to ~1s */
#define SD_MAX_BLOCKS_PER_TRANSFER    256U   /* keeps any single port call's duration bounded */

/**
 * @brief One host controller context - one dmdrvi_create() call, one
 *        physical SDMMC/SDIO peripheral instance
 *
 * Everything about the currently-inserted card (if any) lives directly in
 * this struct rather than a separately-allocated "card" object - there is
 * only ever one card slot per host controller, and dmsdio_generation_t is
 * what lets an already-open handle notice the card it was opened against
 * is gone (see dmsdio_handle_t below).
 */
struct dmdrvi_context
{
    uint32_t             magic;
    dmsdio_config_t       config;
    dmosi_mutex_t         lock;         /**< Serializes every command/data-phase sequence below */

    bool                  card_present; /**< Whether /dev/dmsdioN/0 is currently announced */
    dmsdio_card_info_t    card;         /**< Valid only while card_present */
    uint16_t              rca;          /**< Relative Card Address, valid only while card_present */

    /* Optional card-detect friend GPIO (dmspi's chip-select friend_group
     * pattern) - card_detect_path == NULL means no CD line is wired for
     * this instance, and presence is instead re-checked by attempting
     * identification itself (see rescan_card()). */
    char                 *card_detect_path;
    bool                  card_detect_present_high;

    /* Hot-plug worker - see docs/README.md "Hot-plug detection". The GPIO
     * interrupt handler (ISR context) only pushes a dummy byte into
     * hotplug_queue; every debounce delay, re-read, and card
     * re-identification happens in hotplug_thread (task context), since
     * re-identifying a card means exchanging real commands over the bus. */
    dmosi_thread_t        hotplug_thread;
    dmosi_queue_t         hotplug_queue;
    volatile bool         hotplug_stop;
};

/**
 * @brief Handle dmdrvi_open()/_close() hand back and forth for the card sub-node
 *
 * Captures the card generation at open() time so a read/write/ioctl issued
 * through a handle opened against a card that has since been removed (and
 * possibly replaced by a different one) fails with -ENODEV instead of
 * silently operating on/confusing itself with whatever is inserted now.
 */
typedef struct
{
    dmdrvi_context_t     context;
    dmsdio_generation_t  generation;
} dmsdio_handle_t;

static int is_valid_context(dmdrvi_context_t context)
{
    return context != NULL && context->magic == DMSDIO_CONTEXT_MAGIC;
}

/* ---- Response field extraction ----
 *
 * See dmsdio_response_t's own doc comment for the words[] packing this
 * relies on.
 */

static uint32_t r1_status(const dmsdio_response_t *response)
{
    return response->words[3];
}

static dmsdio_error_t check_r1_errors(const dmsdio_response_t *response)
{
    return ((r1_status(response) & SD_STATUS_ERROR_MASK) != 0) ? dmsdio_error_data_crc : dmsdio_error_none;
}

static uint32_t r3_ocr(const dmsdio_response_t *response)
{
    return response->words[3];
}

static uint16_t r6_rca(const dmsdio_response_t *response)
{
    return (uint16_t)(response->words[3] >> 16);
}

static bool r7_matches_check_pattern(const dmsdio_response_t *response)
{
    return (response->words[3] & 0xFFFUL) == SD_CMD8_ARGUMENT;
}

/**
 * @brief Parse a 128-bit CID register (see dmsdio_response_t's packing)
 *
 * Field bit ranges are as tabled in the Physical Layer spec, section 5.1.
 */
static void parse_cid(const dmsdio_response_t *response, dmsdio_cid_t *cid)
{
    const uint32_t *w = response->words;

    memset(cid, 0, sizeof(*cid));
    cid->manufacturer_id = (uint8_t)(w[0] >> 24);                    /* MID [127:120] */
    cid->oem_id[0] = (char)(w[0] >> 16);                               /* OID [119:112] */
    cid->oem_id[1] = (char)(w[0] >> 8);                                /* OID [111:104] */
    cid->oem_id[2] = '\0';
    cid->product_name[0] = (char)(w[0]);                               /* PNM [103:96] */
    cid->product_name[1] = (char)(w[1] >> 24);                        /* PNM [95:88] */
    cid->product_name[2] = (char)(w[1] >> 16);                        /* PNM [87:80] */
    cid->product_name[3] = (char)(w[1] >> 8);                         /* PNM [79:72] */
    cid->product_name[4] = (char)(w[1]);                               /* PNM [71:64] */
    cid->product_name[5] = '\0';
    cid->product_revision = (uint8_t)(w[2] >> 24);                    /* PRV [63:56] */
    cid->serial_number = (uint32_t)((w[2] & 0x00FFFFFFUL) << 8) | (uint8_t)(w[3] >> 24); /* PSN [55:24] */

    uint16_t mdt = (uint16_t)((w[3] >> 8) & 0x0FFFU);                  /* MDT [19:8] */
    cid->manufacturing_year = (uint16_t)(2000U + (mdt >> 4));
    cid->manufacturing_month = (uint8_t)(mdt & 0x0FU);
}

/**
 * @brief Parse a 128-bit CSD register into just the fields this driver uses
 *
 * CSD structure version 0 (SDSC) and version 1 (SDHC/SDXC) lay capacity out
 * completely differently (Physical Layer spec, section 5.3.2 vs 5.3.3) -
 * everything else this driver cares about (TAAC/TRAN_SPEED -> a
 * conservative max_transfer_speed_hz, PERM_WRITE_PROTECT) is at the same
 * bit offset in both.
 */
static void parse_csd(const dmsdio_response_t *response, dmsdio_csd_t *csd)
{
    const uint32_t *w = response->words;

    memset(csd, 0, sizeof(*csd));
    csd->csd_structure_version = (uint8_t)(w[0] >> 30);  /* CSD_STRUCTURE [127:126] */

    /* TRAN_SPEED [103:96] - the low byte of w[0] (w[0] covers bits
     * [127:96]) - only the two transfer-rate-unit/time-value combinations
     * real SD cards actually use are decoded; anything else conservatively
     * falls back to the mandatory default-speed rate so a card is never
     * driven faster than it is safe to. */
    uint8_t tran_speed = (uint8_t)(w[0] & 0xFFU);
    csd->max_transfer_speed_hz = (tran_speed == 0x32) ? 25000000U  /* 25 MHz - default speed */
                                : (tran_speed == 0x5A) ? 50000000U /* 50 MHz - high speed */
                                : 25000000U;

    if (csd->csd_structure_version == 0)
    {
        /* CSD 1.0 (SDSC): C_SIZE [73:62] (12 bits), C_SIZE_MULT [49:47] (3 bits),
         * READ_BL_LEN [83:80] (4 bits). Capacity in bytes =
         * (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN, then converted to
         * DMSDIO_BLOCK_SIZE-sized blocks regardless of the card's native
         * READ_BL_LEN (see DMSDIO_BLOCK_SIZE's own comment). */
        uint32_t read_bl_len = (w[1] >> 16) & 0x0FU;                              /* [83:80] */
        uint32_t c_size = ((w[1] & 0x000003FFUL) << 2) | ((w[2] >> 30) & 0x3U);   /* [73:62] */
        uint32_t c_size_mult = (w[2] >> 15) & 0x7U;                               /* [49:47] */
        uint64_t capacity_bytes = (uint64_t)(c_size + 1U) << (c_size_mult + 2U + read_bl_len);
        csd->capacity_blocks = capacity_bytes / DMSDIO_BLOCK_SIZE;
    }
    else
    {
        /* CSD 2.0 (SDHC/SDXC): C_SIZE [69:48] (22 bits). Capacity in
         * DMSDIO_BLOCK_SIZE (512-byte) blocks = (C_SIZE+1) * 1024. */
        uint32_t c_size = ((w[1] & 0x0000003FUL) << 16) | (w[2] >> 16);           /* [69:48] */
        csd->capacity_blocks = (uint64_t)(c_size + 1U) * 1024ULL;
    }

    csd->read_only = ((w[3] >> 13) & 0x1U) != 0; /* PERM_WRITE_PROTECT [12] */
}

/**
 * @brief Parse the 8-byte SCR register (Physical Layer spec, section 5.6)
 */
static void parse_scr(const uint8_t *buffer, dmsdio_scr_t *scr)
{
    memset(scr, 0, sizeof(*scr));
    scr->sd_spec_version = (uint8_t)(buffer[0] & 0x0FU);          /* SD_SPEC [59:56] */
    scr->supports_4bit_bus = (buffer[1] & 0x04U) != 0;            /* SD_BUS_WIDTHS bit 2 [50] */
}

/* ---- Command layer ----
 *
 * Every SD command in this driver funnels through send_cmd()/send_acmd() so
 * retry policy lives in exactly one place. Retries only ever apply to
 * transient errors (no response / CRC) - a structural error (bad argument,
 * card physically gone) fails immediately, retrying it would just waste
 * the same command_timeout_ms up to max_retries times for no reason.
 */

static bool is_retryable(dmsdio_error_t err)
{
    return err == dmsdio_error_no_response || err == dmsdio_error_command_crc;
}

static dmsdio_error_t send_cmd(dmdrvi_context_t ctx, uint8_t cmd_index, uint32_t argument,
                                dmsdio_response_type_t response_type, dmsdio_response_t *response)
{
    dmsdio_error_t err = dmsdio_error_none;

    for (uint32_t attempt = 0; attempt <= ctx->config.max_retries; attempt++)
    {
        err = dmsdio_port_send_command(ctx->config.instance, cmd_index, argument, response_type, response);
        if (!is_retryable(err))
        {
            return err;
        }
    }
    return err;
}

static dmsdio_error_t send_acmd(dmdrvi_context_t ctx, uint16_t rca, uint8_t acmd_index, uint32_t argument,
                                 dmsdio_response_type_t response_type, dmsdio_response_t *response)
{
    dmsdio_response_t app_cmd_response;
    dmsdio_error_t err = send_cmd(ctx, SD_CMD_APP_CMD, (uint32_t)rca << 16, dmsdio_response_r1, &app_cmd_response);
    if (err != dmsdio_error_none)
    {
        return err;
    }

    return send_cmd(ctx, acmd_index, argument, response_type, response);
}

/**
 * @brief Map a dmsdio_error_t (or an R1 status error already checked by the
 *        caller) to the errno-compatible value dmdrvi's API surface expects
 */
static int to_errno(dmsdio_error_t err)
{
    switch (err)
    {
        case dmsdio_error_none:             return 0;
        case dmsdio_error_no_response:      return -ETIMEDOUT;
        case dmsdio_error_command_crc:      return -EIO;
        case dmsdio_error_data_timeout:     return -ETIMEDOUT;
        case dmsdio_error_data_crc:         return -EIO;
        case dmsdio_error_removed:          return -ENODEV;
        case dmsdio_error_invalid_argument: return -EINVAL;
        case dmsdio_error_not_supported:
        default:                            return -ENOTSUP;
    }
}

/* ---- Card identification ---- */

static bool is_block_addressed(dmsdio_card_type_t type)
{
    return type == dmsdio_card_type_sdhc || type == dmsdio_card_type_sdxc;
}

static uint32_t block_address_argument(dmdrvi_context_t ctx, uint64_t block_index)
{
    if (is_block_addressed(ctx->card.card_type))
    {
        return (uint32_t)block_index;
    }
    return (uint32_t)(block_index * DMSDIO_BLOCK_SIZE);
}

/**
 * @brief ACMD51 (SEND_SCR) - card must already be selected (transfer state)
 */
static dmsdio_error_t read_scr(dmdrvi_context_t ctx, uint16_t rca, dmsdio_scr_t *scr)
{
    uint8_t buffer[SD_SCR_SIZE];
    dmsdio_response_t response;

    dmsdio_error_t err = send_acmd(ctx, rca, SD_ACMD_SEND_SCR, 0, dmsdio_response_r1, &response);
    if (err == dmsdio_error_none)
    {
        err = check_r1_errors(&response);
    }
    if (err != dmsdio_error_none)
    {
        return err;
    }

    err = dmsdio_port_read_blocks(ctx->config.instance, buffer, SD_SCR_SIZE, 1);
    if (err != dmsdio_error_none)
    {
        return err;
    }

    parse_scr(buffer, scr);
    return dmsdio_error_none;
}

/**
 * @brief Attempt the CMD6 High Speed switch; leaves the card's function
 *        selection unchanged (default speed) on any failure
 *
 * The card must already be selected. Only function group 1 (access mode)
 * is touched - every other group's nibble in the argument is 0xF ("no
 * change"), matching the Physical Layer spec's CMD6 argument layout.
 */
static bool switch_to_high_speed(dmdrvi_context_t ctx)
{
    uint8_t status[SD_SWITCH_STATUS_SIZE];
    dmsdio_response_t response;

    /* mode=1 (switch, bit31), function group 1 = 1 (high speed), every
     * other group = 0xF (no change). */
    uint32_t argument = 0x80FFFFF1UL;

    dmsdio_error_t err = send_cmd(ctx, SD_CMD_SWITCH_FUNC, argument, dmsdio_response_r1, &response);
    if (err == dmsdio_error_none)
    {
        err = check_r1_errors(&response);
    }
    if (err != dmsdio_error_none)
    {
        return false;
    }

    err = dmsdio_port_read_blocks(ctx->config.instance, status, SD_SWITCH_STATUS_SIZE, 1);
    if (err != dmsdio_error_none)
    {
        return false;
    }

    /* Byte 16, low nibble = function group 1's now-selected function
     * (Physical Layer spec, "Switch Function Status"). 1 = high speed was
     * actually accepted; anything else (most commonly 0xF, "not
     * supported") means the card stayed at default speed. */
    return (status[16] & 0x0FU) == 0x1U;
}

/**
 * @brief Full card (re-)identification sequence - CMD0 through CMD7 plus
 *        SCR/High-Speed setup, run with the card-detect debounced and the
 *        bus already idle
 *
 * On success, populates ctx->card/ctx->rca and bumps ctx->card.generation;
 * on failure, ctx->card_present is left false (or, if a card was
 * previously present, becomes false - see rescan_card()).
 */
static bool identify_card(dmdrvi_context_t ctx)
{
    dmsdio_response_t response;
    dmsdio_generation_t next_generation = ctx->card.generation + 1U;

    dmsdio_port_set_bus_width(ctx->config.instance, dmsdio_bus_width_1bit);
    dmsdio_port_set_speed_mode(ctx->config.instance, dmsdio_speed_mode_identification);

    /* CMD0: reset to idle state. No response expected - if there is no
     * card, or it is unresponsive, this alone can never fail; the very
     * next command (CMD8/ACMD41) is what actually detects "nothing here". */
    send_cmd(ctx, SD_CMD_GO_IDLE_STATE, 0, dmsdio_response_none, NULL);

    /* CMD8: SEND_IF_COND. A real response with a matching echo means a
     * 2.0+ card (proceed with HCS set in ACMD41, below); no response at
     * all means either a pre-2.0 card or nothing responding - both are
     * handled identically by ACMD41 without HCS, and ACMD41's own timeout
     * is what ultimately reports "no card". */
    bool is_v2_or_later = (send_cmd(ctx, SD_CMD_SEND_IF_COND, SD_CMD8_ARGUMENT, dmsdio_response_r7, &response) == dmsdio_error_none)
                        && r7_matches_check_pattern(&response);

    uint32_t ocr = 0;
    uint32_t acmd41_argument = SD_OCR_VOLTAGE_WINDOW_2V7_3V6 | (is_v2_or_later ? SD_OCR_HCS : 0U);
    bool powered_up = false;

    for (uint32_t attempt = 0; attempt < SD_OP_COND_MAX_ATTEMPTS; attempt++)
    {
        if (send_acmd(ctx, 0, SD_ACMD_SEND_OP_COND, acmd41_argument, dmsdio_response_r3, &response) != dmsdio_error_none)
        {
            return false; /* No card, or not an SD card - ACMD41 itself is unanswered */
        }

        ocr = r3_ocr(&response);
        if ((ocr & SD_OCR_BUSY) != 0)
        {
            powered_up = true;
            break;
        }

        Dmod_ThreadSleep(1);
    }

    if (!powered_up)
    {
        return false; /* Card never finished powering up within our budget */
    }

    dmsdio_card_type_t card_type;
    if (!is_v2_or_later)
    {
        card_type = dmsdio_card_type_sdsc_v1;
    }
    else if ((ocr & SD_OCR_HCS) == 0)
    {
        card_type = dmsdio_card_type_sdsc_v2;
    }
    else
    {
        card_type = dmsdio_card_type_sdhc; /* refined to sdxc below, once capacity is known */
    }

    /* CMD2: ALL_SEND_CID. */
    if (send_cmd(ctx, SD_CMD_ALL_SEND_CID, 0, dmsdio_response_r2, &response) != dmsdio_error_none)
    {
        return false;
    }
    dmsdio_cid_t cid;
    parse_cid(&response, &cid);

    /* CMD3: SEND_RELATIVE_ADDR - card moves from identification to
     * stand-by state and publishes its RCA. */
    if (send_cmd(ctx, SD_CMD_SEND_RELATIVE_ADDR, 0, dmsdio_response_r6, &response) != dmsdio_error_none)
    {
        return false;
    }
    uint16_t rca = r6_rca(&response);

    /* Past CMD3 the card is in stand-by/data-transfer state - safe to
     * raise the clock before the CSD/CID work that follows. */
    dmsdio_port_set_speed_mode(ctx->config.instance, dmsdio_speed_mode_default_speed);

    /* CMD9: SEND_CSD (stand-by state). */
    if (send_cmd(ctx, SD_CMD_SEND_CSD, (uint32_t)rca << 16, dmsdio_response_r2, &response) != dmsdio_error_none)
    {
        return false;
    }
    dmsdio_csd_t csd;
    parse_csd(&response, &csd);

    if (card_type == dmsdio_card_type_sdhc && (csd.capacity_blocks * DMSDIO_BLOCK_SIZE) > (32ULL * 1024 * 1024 * 1024))
    {
        card_type = dmsdio_card_type_sdxc;
    }

    /* CMD7: SELECT_DESELECT_CARD - moves to transfer state; every data
     * command (SCR, block I/O, switch function) requires this first. */
    if (send_cmd(ctx, SD_CMD_SELECT_DESELECT_CARD, (uint32_t)rca << 16, dmsdio_response_r1b, &response) != dmsdio_error_none)
    {
        return false;
    }
    if (check_r1_errors(&response) != dmsdio_error_none)
    {
        return false;
    }

    /* SDSC's native block length can be anything the CSD's READ_BL_LEN
     * says - force it to DMSDIO_BLOCK_SIZE so every card family behaves
     * identically from here on (SDHC/SDXC ignore CMD16 entirely). */
    if (!is_block_addressed(card_type))
    {
        if (send_cmd(ctx, SD_CMD_SET_BLOCKLEN, DMSDIO_BLOCK_SIZE, dmsdio_response_r1, &response) != dmsdio_error_none ||
            check_r1_errors(&response) != dmsdio_error_none)
        {
            return false;
        }
    }

    dmsdio_scr_t scr;
    if (read_scr(ctx, rca, &scr) != dmsdio_error_none)
    {
        return false;
    }

    dmsdio_bus_width_t bus_width = dmsdio_bus_width_1bit;
    if (scr.supports_4bit_bus && ctx->config.max_bus_width == dmsdio_bus_width_4bit)
    {
        dmsdio_response_t acmd6_response;
        if (send_acmd(ctx, rca, SD_ACMD_SET_BUS_WIDTH, 0x2U /* 4-bit */, dmsdio_response_r1, &acmd6_response) == dmsdio_error_none &&
            check_r1_errors(&acmd6_response) == dmsdio_error_none)
        {
            dmsdio_port_set_bus_width(ctx->config.instance, dmsdio_bus_width_4bit);
            bus_width = dmsdio_bus_width_4bit;
        }
    }

    dmsdio_speed_mode_t speed_mode = dmsdio_speed_mode_default_speed;
    if (ctx->config.allow_high_speed && csd.max_transfer_speed_hz > 25000000U)
    {
        if (switch_to_high_speed(ctx))
        {
            dmsdio_port_set_speed_mode(ctx->config.instance, dmsdio_speed_mode_high_speed);
            speed_mode = dmsdio_speed_mode_high_speed;
        }
    }

    ctx->rca = rca;
    ctx->card.card_type = card_type;
    ctx->card.generation = next_generation;
    ctx->card.cid = cid;
    ctx->card.csd = csd;
    ctx->card.scr = scr;
    ctx->card.bus_width = bus_width;
    ctx->card.speed_mode = speed_mode;
    return true;
}

/* ---- Block I/O ---- */

/**
 * @brief SD_STATUS_ERROR_MASK's counterpart for the CURRENT_STATE field -
 *        polls CMD13/SEND_STATUS until the card reports READY_FOR_DATA
 *        (bit 8) or ctx->config.data_timeout_ms elapses
 *
 * Used after a write's data phase and after an erase - both leave the card
 * internally busy (committing to flash) for longer than the bus transfer
 * itself took, well past when dmsdio_port_write_blocks()/_send_command()
 * already returned.
 */
static dmsdio_error_t wait_ready_for_data(dmdrvi_context_t ctx)
{
    /* Card status bit 8 (Physical Layer spec, section 4.10.1) - see
     * SD_STATUS_ERROR_MASK's neighbouring bits for the same response. */
    const uint32_t READY_FOR_DATA = (1UL << 8);

    for (uint32_t waited_ms = 0; waited_ms < ctx->config.data_timeout_ms; waited_ms++)
    {
        dmsdio_response_t response;
        dmsdio_error_t err = send_cmd(ctx, SD_CMD_SEND_STATUS, (uint32_t)ctx->rca << 16, dmsdio_response_r1, &response);
        if (err != dmsdio_error_none)
        {
            return err;
        }
        err = check_r1_errors(&response);
        if (err != dmsdio_error_none)
        {
            return err;
        }

        if ((r1_status(&response) & READY_FOR_DATA) != 0)
        {
            return dmsdio_error_none;
        }

        Dmod_ThreadSleep(1);
    }

    return dmsdio_error_data_timeout;
}

/**
 * @brief Read/write raw DMSDIO_BLOCK_SIZE-aligned blocks
 *
 * Picks CMD17/CMD24 (single) vs. CMD18/CMD25 (multiple, terminated by
 * CMD12) based on block_count - never a protocol decision the caller needs
 * to make itself.
 */
static dmsdio_error_t transfer_blocks(dmdrvi_context_t ctx, uint64_t start_block, void *buffer,
                                       uint32_t block_count, bool is_write)
{
    uint32_t argument = block_address_argument(ctx, start_block);
    uint8_t cmd_index = is_write
        ? ((block_count > 1) ? SD_CMD_WRITE_MULTIPLE_BLOCK : SD_CMD_WRITE_BLOCK)
        : ((block_count > 1) ? SD_CMD_READ_MULTIPLE_BLOCK  : SD_CMD_READ_SINGLE_BLOCK);

    dmsdio_response_t response;
    dmsdio_error_t err = send_cmd(ctx, cmd_index, argument, dmsdio_response_r1, &response);
    if (err == dmsdio_error_none)
    {
        err = check_r1_errors(&response);
    }

    if (err == dmsdio_error_none)
    {
        err = is_write
            ? dmsdio_port_write_blocks(ctx->config.instance, buffer, DMSDIO_BLOCK_SIZE, block_count)
            : dmsdio_port_read_blocks(ctx->config.instance, buffer, DMSDIO_BLOCK_SIZE, block_count);
    }

    if (block_count > 1)
    {
        /* Always issue STOP_TRANSMISSION for a multi-block transfer, even
         * on error, so the card is not left mid-stream for whatever
         * command comes next. */
        dmsdio_response_t stop_response;
        send_cmd(ctx, SD_CMD_STOP_TRANSMISSION, 0, dmsdio_response_r1b, &stop_response);
    }

    if (err == dmsdio_error_none && is_write)
    {
        err = wait_ready_for_data(ctx);
    }

    return err;
}

/**
 * @brief Read/write an arbitrary byte range, handling non-block-aligned
 *        offsets/sizes via read-modify-write on the boundary blocks
 *
 * Splits the range into up to three pieces: a partial leading block, a run
 * of whole blocks transferred directly (no scratch buffer), and a partial
 * trailing block - exactly the "direct full-sector transfers and safe
 * partial-sector read-modify-write" acceptance criterion. is_write selects
 * between reading into `data` and writing from `data`.
 */
static int transfer_bytes(dmdrvi_context_t ctx, void *data, size_t size, uint64_t offset, bool is_write)
{
    uint8_t *cursor = (uint8_t *)data;
    size_t remaining = size;
    uint64_t byte_offset = offset;
    size_t done = 0;

    while (remaining > 0)
    {
        uint64_t block_index = byte_offset / DMSDIO_BLOCK_SIZE;
        uint32_t block_offset = (uint32_t)(byte_offset % DMSDIO_BLOCK_SIZE);

        if (block_offset == 0 && remaining >= DMSDIO_BLOCK_SIZE)
        {
            uint64_t whole_blocks = remaining / DMSDIO_BLOCK_SIZE;
            if (whole_blocks > SD_MAX_BLOCKS_PER_TRANSFER)
            {
                whole_blocks = SD_MAX_BLOCKS_PER_TRANSFER;
            }

            dmsdio_error_t err = transfer_blocks(ctx, block_index, cursor, (uint32_t)whole_blocks, is_write);
            if (err != dmsdio_error_none)
            {
                return (done > 0) ? (int)done : to_errno(err);
            }

            size_t chunk = (size_t)whole_blocks * DMSDIO_BLOCK_SIZE;
            cursor += chunk;
            byte_offset += chunk;
            remaining -= chunk;
            done += chunk;
        }
        else
        {
            uint8_t scratch[DMSDIO_BLOCK_SIZE];
            uint32_t take = DMSDIO_BLOCK_SIZE - block_offset;
            if ((size_t)take > remaining)
            {
                take = (uint32_t)remaining;
            }

            dmsdio_error_t err = transfer_blocks(ctx, block_index, scratch, 1, false);
            if (err != dmsdio_error_none)
            {
                return (done > 0) ? (int)done : to_errno(err);
            }

            if (is_write)
            {
                memcpy(scratch + block_offset, cursor, take);
                err = transfer_blocks(ctx, block_index, scratch, 1, true);
                if (err != dmsdio_error_none)
                {
                    return (done > 0) ? (int)done : to_errno(err);
                }
            }
            else
            {
                memcpy(cursor, scratch + block_offset, take);
            }

            cursor += take;
            byte_offset += take;
            remaining -= take;
            done += take;
        }
    }

    return (int)done;
}

/* ---- Hot-plug ---- */

static void announce_card(dmdrvi_context_t ctx)
{
    if (ctx->card_present)
    {
        return;
    }

    ctx->card_present = true;

    dmdrvi_dev_num_t dev_num;
    dev_num.major = ctx->config.instance;
    dev_num.minor = 0;
    dev_num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    dev_num.alt_name[0] = '\0';
    dmdrvi_device_available(ctx, &dev_num);

    DMOD_LOG_INFO("dmsdio%u: card present (%llu blocks, generation %u)\n",
        (unsigned)ctx->config.instance, (unsigned long long)ctx->card.csd.capacity_blocks, (unsigned)ctx->card.generation);
}

static void unannounce_card(dmdrvi_context_t ctx)
{
    if (!ctx->card_present)
    {
        return;
    }

    ctx->card_present = false;

    dmdrvi_dev_num_t dev_num;
    dev_num.major = ctx->config.instance;
    dev_num.minor = 0;
    dev_num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    dev_num.alt_name[0] = '\0';
    dmdrvi_device_unavailable(ctx, &dev_num);

    DMOD_LOG_INFO("dmsdio%u: card removed\n", (unsigned)ctx->config.instance);
}

/**
 * @brief Re-check whether a card is present and (re-)identify it if so
 *
 * Called both once at dmdrvi_create() time (a card may already be sitting
 * in the slot at boot) and from the hot-plug worker thread on every
 * debounced card-detect transition. When there is no CD line at all, this
 * is also the *only* detection mechanism - callers with no CD friend
 * configured are expected to call this periodically themselves (see
 * dmod_init()'s poll timer) instead of relying on hardware interrupts.
 */
static void rescan_card(dmdrvi_context_t ctx)
{
    dmosi_mutex_lock(ctx->lock);

    if (identify_card(ctx))
    {
        announce_card(ctx);
    }
    else
    {
        unannounce_card(ctx);
    }

    dmosi_mutex_unlock(ctx->lock);
}

static void hotplug_worker(void *arg)
{
    dmdrvi_context_t ctx = (dmdrvi_context_t)arg;
    uint8_t dummy;

    /* Debounce: wait for a burst of edge interrupts (bouncing contacts) to
     * go quiet for DMSDIO_DEBOUNCE_MS before trusting the new state - a
     * short, fixed settle time is enough since we always re-derive the
     * real state from the CD pin itself (or the protocol probe) rather
     * than trusting which edge fired. */
    while (dmosi_queue_receive(ctx->hotplug_queue, &dummy, -1) == 0)
    {
        if (ctx->hotplug_stop)
        {
            break;
        }

        Dmod_ThreadSleep(50);

        /* Drain any further events that arrived during the settle time -
         * they all resolve to the same rescan. */
        while (dmosi_queue_receive(ctx->hotplug_queue, &dummy, 0) == 0)
        {
        }

        rescan_card(ctx);
    }
}

static void card_detect_interrupt(dmdrvi_context_t ctx, dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_mask_t state)
{
    (void)port; (void)pins; (void)state;

    /* ISR context - only enqueue, never touch the bus here. */
    uint8_t dummy = 0;
    dmosi_queue_send(ctx->hotplug_queue, &dummy, 0);
}

/* ---- Configuration ---- */

static int check_config_parameters(const dmsdio_config_t *cfg)
{
    if (cfg->command_timeout_ms == 0 || cfg->data_timeout_ms == 0)
    {
        DMOD_LOG_ERROR("dmsdio: timeouts must be non-zero\n");
        return -EINVAL;
    }
    return 0;
}

dmod_dmsdio_api_declaration(1.0, bool, _validate_config, ( const dmsdio_config_t *config ))
{
    return (config != NULL) && (check_config_parameters(config) == 0);
}

static const char *detect_config_section(dmini_context_t ini)
{
    if (dmini_has_key(ini, "dmsdio", "instance"))
    {
        return "dmsdio";
    }
    if (dmini_section_count(ini) > 0)
    {
        const char *first = dmini_section_name(ini, 0);
        if (first != NULL)
        {
            return first;
        }
    }
    return "dmsdio";
}

static void read_config_parameters(dmsdio_config_t *cfg, dmini_context_t ini)
{
    const char *section = detect_config_section(ini);

    cfg->instance = (dmsdio_instance_t)dmini_get_int(ini, section, "instance", 0);
    cfg->max_bus_width = (dmini_get_int(ini, section, "max_bus_width", 4) >= 4)
        ? dmsdio_bus_width_4bit : dmsdio_bus_width_1bit;
    cfg->allow_high_speed = dmini_get_int(ini, section, "high_speed", 1) != 0;
    cfg->command_timeout_ms = (uint32_t)dmini_get_int(ini, section, "command_timeout_ms", 100);
    cfg->data_timeout_ms = (uint32_t)dmini_get_int(ini, section, "data_timeout_ms", 500);
    cfg->max_retries = (uint32_t)dmini_get_int(ini, section, "max_retries", 3);
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMSDIO driver module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMSDIO driver module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_context_t, _create,
    ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: invalid parameters to dmsdio_dmdrvi_create\n");
        return NULL;
    }

    dmdrvi_context_t ctx = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (ctx == NULL)
    {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->magic = DMSDIO_CONTEXT_MAGIC;

    read_config_parameters(&ctx->config, config);
    if (check_config_parameters(&ctx->config) != 0)
    {
        Dmod_Free(ctx);
        return NULL;
    }

    ctx->lock = dmosi_mutex_create(false);
    ctx->hotplug_queue = dmosi_queue_create(sizeof(uint8_t), 4);
    if (ctx->lock == NULL || ctx->hotplug_queue == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: failed to allocate synchronization primitives\n");
        dmosi_mutex_destroy(ctx->lock);
        dmosi_queue_destroy(ctx->hotplug_queue);
        Dmod_Free(ctx);
        return NULL;
    }

    if (dmsdio_port_init(ctx->config.instance) != 0)
    {
        DMOD_LOG_ERROR("dmsdio: failed to initialize instance %u\n", (unsigned)ctx->config.instance);
        dmosi_mutex_destroy(ctx->lock);
        dmosi_queue_destroy(ctx->hotplug_queue);
        Dmod_Free(ctx);
        return NULL;
    }

    ctx->hotplug_thread = dmosi_thread_create(hotplug_worker, ctx, 1, 2048, "dmsdio_hotplug", NULL);
    if (ctx->hotplug_thread == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: failed to start hot-plug worker\n");
    }

    /* Host context itself is announced as major-only (no card yet); the
     * card sub-node is announced separately by rescan_card() once/if a
     * card is actually found. */
    dev_num->major = ctx->config.instance;
    dev_num->minor = 0;
    dev_num->flags = DMDRVI_NUM_MAJOR;
    dev_num->alt_name[0] = '\0';

    rescan_card(ctx);

    return ctx;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
    {
        return;
    }

    context->hotplug_stop = true;
    if (context->hotplug_queue != NULL)
    {
        uint8_t dummy = 0;
        dmosi_queue_send(context->hotplug_queue, &dummy, 0); /* wake the worker so it can observe hotplug_stop */
    }
    if (context->hotplug_thread != NULL)
    {
        dmosi_thread_join(context->hotplug_thread);
        dmosi_thread_destroy(context->hotplug_thread);
    }
    dmosi_queue_destroy(context->hotplug_queue);

    if (context->card_detect_path != NULL)
    {
        void *file = Dmod_FileOpen(context->card_detect_path, "r+");
        if (file != NULL)
        {
            Dmod_Ioctl(file, dmgpio_ioctl_cmd_set_interrupt_handler, NULL);
            Dmod_FileClose(file);
        }
        Dmod_Free(context->card_detect_path);
    }

    dmsdio_port_deinit(context->config.instance);
    dmosi_mutex_destroy(context->lock);
    context->magic = 0;
    Dmod_Free(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _friend_changed,
    ( dmdrvi_context_t context, const dmdrvi_friend_info_t* info ))
{
    if (!is_valid_context(context) || info == NULL || info->friend_role == NULL ||
        strcmp(info->friend_role, "card_detect") != 0)
    {
        return;
    }

    if (info->state != dmdrvi_dev_state_ready || info->node_path == NULL)
    {
        Dmod_Free(context->card_detect_path);
        context->card_detect_path = NULL;
        return;
    }

    char *path = Dmod_StrDup(info->node_path);
    if (path == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: failed to retain card-detect GPIO path\n");
        return;
    }

    Dmod_Free(context->card_detect_path);
    context->card_detect_path = path;

    void *file = Dmod_FileOpen(path, "r+");
    if (file == NULL)
    {
        DMOD_LOG_ERROR("dmsdio: failed to open card-detect GPIO: %s\n", path);
        return;
    }

    dmgpio_interrupt_handler_t handler = (dmgpio_interrupt_handler_t)card_detect_interrupt;
    Dmod_Ioctl(file, dmgpio_ioctl_cmd_set_interrupt_handler, &handler);
    Dmod_FileClose(file);

    /* A card may already be present/absent before this friend link was
     * even wired up - resolve the real state immediately instead of
     * waiting for the first edge. */
    rescan_card(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void*, _open,
    ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    (void)flags;

    if (!is_valid_context(context) || dev_num == NULL || dev_num->minor != 0 || !context->card_present)
    {
        DMOD_LOG_ERROR("dmsdio: invalid open (no card present or invalid dev_num)\n");
        return NULL;
    }

    dmsdio_handle_t *handle = Dmod_Malloc(sizeof(*handle));
    if (handle == NULL)
    {
        return NULL;
    }

    handle->context = context;
    handle->generation = context->card.generation;
    return handle;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    (void)context;
    Dmod_Free(handle);
}

/**
 * @brief Validates a handle against the context's *current* card generation
 *
 * Returns NULL (and the caller should treat that as -ENODEV) if the handle
 * was opened against a card that is no longer the one in the slot - either
 * because it was removed, or removed and replaced while this handle stayed
 * open.
 */
static dmsdio_handle_t *validate_handle(dmdrvi_context_t context, void *handle)
{
    if (!is_valid_context(context) || handle == NULL)
    {
        return NULL;
    }

    dmsdio_handle_t *h = (dmsdio_handle_t *)handle;
    if (h->context != context || !context->card_present || h->generation != context->card.generation)
    {
        return NULL;
    }

    return h;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_ssize_t, _read,
    ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    if (offset < 0)
    {
        return -EINVAL;
    }
    if (size > (size_t)INT64_MAX)
    {
        return -EOVERFLOW;
    }

    dmsdio_handle_t *h = validate_handle(context, handle);
    if (h == NULL)
    {
        return -ENODEV;
    }
    if (buffer == NULL || size == 0)
    {
        return 0;
    }

    uint64_t capacity_bytes = context->card.csd.capacity_blocks * DMSDIO_BLOCK_SIZE;
    if ((uint64_t)offset >= capacity_bytes)
    {
        return 0; /* EOF */
    }

    size_t clamped_size = size;
    if ((uint64_t)offset + clamped_size > capacity_bytes)
    {
        clamped_size = (size_t)(capacity_bytes - (uint64_t)offset);
    }

    dmosi_mutex_lock(context->lock);
    int result = transfer_bytes(context, buffer, clamped_size, (uint64_t)offset, false);
    dmosi_mutex_unlock(context->lock);

    if (result < 0 && to_errno(dmsdio_error_removed) == result)
    {
        unannounce_card(context);
    }
    return (dmdrvi_ssize_t)result;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, dmdrvi_ssize_t, _write,
    ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    if (offset < 0)
    {
        return -EINVAL;
    }
    if (size > (size_t)INT64_MAX)
    {
        return -EOVERFLOW;
    }

    dmsdio_handle_t *h = validate_handle(context, handle);
    if (h == NULL)
    {
        return -ENODEV;
    }
    if (buffer == NULL || size == 0)
    {
        return 0;
    }
    if (context->card.csd.read_only)
    {
        return -EROFS;
    }

    uint64_t capacity_bytes = context->card.csd.capacity_blocks * DMSDIO_BLOCK_SIZE;
    if ((uint64_t)offset >= capacity_bytes)
    {
        return -ENOSPC;
    }

    size_t clamped_size = size;
    if ((uint64_t)offset + clamped_size > capacity_bytes)
    {
        clamped_size = (size_t)(capacity_bytes - (uint64_t)offset);
    }

    dmosi_mutex_lock(context->lock);
    int result = transfer_bytes(context, (void *)buffer, clamped_size, (uint64_t)offset, true);
    dmosi_mutex_unlock(context->lock);

    if (result < 0 && to_errno(dmsdio_error_removed) == result)
    {
        unannounce_card(context);
    }
    return (dmdrvi_ssize_t)result;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _ioctl,
    ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    dmsdio_handle_t *h = validate_handle(context, handle);
    if (h == NULL)
    {
        return -ENODEV;
    }
    if (command < 0 || command >= dmsdio_ioctl_cmd_max)
    {
        return -EINVAL;
    }

    switch ((dmsdio_ioctl_cmd_t)command)
    {
        case dmsdio_ioctl_cmd_get_card_info:
            if (arg == NULL) return -EINVAL;
            *(dmsdio_card_info_t *)arg = context->card;
            return 0;

        case dmsdio_ioctl_cmd_get_generation:
            if (arg == NULL) return -EINVAL;
            *(dmsdio_generation_t *)arg = context->card.generation;
            return 0;

        case dmsdio_ioctl_cmd_erase:
        {
            if (arg == NULL) return -EINVAL;
            if (context->card.csd.read_only) return -EROFS;

            dmsdio_erase_range_t *range = (dmsdio_erase_range_t *)arg;
            if ((range->offset % DMSDIO_BLOCK_SIZE) != 0 || (range->size % DMSDIO_BLOCK_SIZE) != 0 || range->size == 0)
            {
                return -EINVAL;
            }

            uint64_t start_block = range->offset / DMSDIO_BLOCK_SIZE;
            uint64_t end_block = start_block + (range->size / DMSDIO_BLOCK_SIZE) - 1U;

            dmosi_mutex_lock(context->lock);
            dmsdio_response_t response;
            dmsdio_error_t err = send_cmd(context, SD_CMD_ERASE_WR_BLK_START, block_address_argument(context, start_block), dmsdio_response_r1, &response);
            if (err == dmsdio_error_none) err = check_r1_errors(&response);
            if (err == dmsdio_error_none)
            {
                err = send_cmd(context, SD_CMD_ERASE_WR_BLK_END, block_address_argument(context, end_block), dmsdio_response_r1, &response);
                if (err == dmsdio_error_none) err = check_r1_errors(&response);
            }
            if (err == dmsdio_error_none)
            {
                err = send_cmd(context, SD_CMD_ERASE, 0, dmsdio_response_r1b, &response);
                if (err == dmsdio_error_none) err = check_r1_errors(&response);
            }
            if (err == dmsdio_error_none)
            {
                err = wait_ready_for_data(context);
            }
            dmosi_mutex_unlock(context->lock);

            return to_errno(err);
        }

        default:
            return -EINVAL;
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    dmsdio_handle_t *h = validate_handle(context, handle);
    if (h == NULL)
    {
        return -ENODEV;
    }
    /* Every transfer_blocks() write already waits for the card to report
     * READY_FOR_DATA before returning (see wait_ready_for_data()) - there
     * is nothing left in flight to wait for here. */
    return 0;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmsdio, int, _stat,
    ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    (void)path;

    if (!is_valid_context(context) || stat == NULL)
    {
        return -EINVAL;
    }

    stat->size = context->card_present ? (dmdrvi_size_t)(context->card.csd.capacity_blocks * DMSDIO_BLOCK_SIZE) : 0;
    stat->mode = context->card_present && context->card.csd.read_only ? 0444 : 0666;
    return 0;
}
