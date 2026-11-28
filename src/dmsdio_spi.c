#include "dmsdio_internal.h"
#include "dmsdio_sd.h"
#include <string.h>

/*
 * An SD card on an SPI bus (bus=spi): the same commands as on the SD bus, carried by the SPI mode of the card (SD Physical Layer Simplified
 * Specification, chapter 7). The core above does not know the difference: dmsdio_spi_execute() takes the command and the data phase the
 * port of a host controller would take and gives back what that port would - the response words in the layout of the SD bus (the R1 status
 * word, the OCR, the CID and the CSD as four big-endian words) - so identification, retries and the block I/O are the same code.
 *
 * The bus is the dmspi of the board (the friends_group of the e-paper on the T-Deck Pro, where the card shares the lines with the display and
 * the radio) and the chip select is a dmgpio output: friend_role spi and sd_cs. Every command is one dmspi session (dmspi_ioctl_cmd_session_*):
 * the bus is held and the card selected from the first byte of the command to the end of its data phase, and the bus goes back to the
 * speed it had - the others on it do not feel the card at 400 kHz.
 *
 * The CRC of the SPI mode is off after the reset, except for CMD0 and CMD8: the commands carry the right CRC7 anyway, the CRC16 of the data
 * is sent as 0xFFFF and the received one is not checked.
 */

#define TOKEN_BLOCK_START       0xFEu
#define TOKEN_MULTI_WRITE       0xFCu
#define TOKEN_STOP_TRAN         0xFDu
#define RESPONSE_TRIES          10      /* the card answers within 8 bytes (NCR) */
#define FAST_POLLS              64u     /* single-byte polls before the polling sleeps between them */
#define DUMMY_CLOCK_BYTES       10u     /* 80 clocks with the card deselected after the power-up */
#define SPI_CMD58_READ_OCR      58

#define R1_IDLE                 0x01u
#define R1_ERASE_RESET          0x02u
#define R1_ILLEGAL_COMMAND      0x04u
#define R1_COM_CRC_ERROR        0x08u
#define R1_ERASE_SEQ_ERROR      0x10u
#define R1_ADDRESS_ERROR        0x20u
#define R1_PARAMETER_ERROR      0x40u
#define R1_ERRORS               0x7Eu

/* ---- The friends ---- */

static void spi_close(struct dmdrvi_context* ctx)
{
    if (ctx->spi_file != NULL)
    {
        Dmod_FileClose(ctx->spi_file);
        ctx->spi_file = NULL;
    }
}

static void replace_path(char** slot, const char* path)
{
    Dmod_Free(*slot);
    *slot = (path != NULL) ? Dmod_StrDup(path) : NULL;
}

void dmsdio_spi_friend(struct dmdrvi_context* ctx, const char* role, const char* path)
{
    if (strcmp(role, "spi") == 0)
    {
        spi_close(ctx);
        replace_path(&ctx->spi_path, path);
    }
    else
    {
        replace_path(&ctx->spi_cs_path, path);
    }
}

bool dmsdio_spi_friends_there(const struct dmdrvi_context* ctx)
{
    return ctx->spi_path != NULL && ctx->spi_cs_path != NULL;
}

void dmsdio_spi_release(struct dmdrvi_context* ctx)
{
    Dmod_Free(ctx->spi_scratch);
    ctx->spi_scratch = NULL;
    spi_close(ctx);
    replace_path(&ctx->spi_path, NULL);
    replace_path(&ctx->spi_cs_path, NULL);
}

/* ---- One session on the bus ---- */

static int ioctl_result(int ret)
{
    return ret == 0 ? 0 : (ret < 0 ? ret : -EIO);
}

static int session_begin(struct dmdrvi_context* ctx, bool select, uint32_t hz)
{
    if (ctx->spi_file == NULL && ctx->spi_path != NULL)
    {
        ctx->spi_file = Dmod_FileOpen(ctx->spi_path, "r+");
    }
    if (ctx->spi_file == NULL || (select && ctx->spi_cs_path == NULL))
    {
        return -ENODEV;
    }
    dmspi_session_t session = { .chip_select = select ? ctx->spi_cs_path : NULL, .baudrate = hz };
    int ret = ioctl_result(Dmod_Ioctl(ctx->spi_file, dmspi_ioctl_cmd_session_begin, &session));
    if (ret != 0)
    {
        spi_close(ctx);     /* (a handle that failed is opened again) */
    }
    return ret;
}

static void session_end(struct dmdrvi_context* ctx)
{
    if (ctx->spi_file != NULL)
    {
        Dmod_Ioctl(ctx->spi_file, dmspi_ioctl_cmd_session_end, NULL);
    }
}

/* tx NULL clocks 0xFF out, rx NULL drops what comes in */
static int xfer(struct dmdrvi_context* ctx, const uint8_t* tx, uint8_t* rx, size_t size)
{
    dmspi_transfer_t transfer = { .tx = tx, .rx = rx, .size = size };
    return ioctl_result(Dmod_Ioctl(ctx->spi_file, dmspi_ioctl_cmd_session_transfer, &transfer));
}

static int send_byte(struct dmdrvi_context* ctx, uint8_t value)
{
    return xfer(ctx, &value, NULL, 1);
}

/* Busy: the card holds MISO low until it is done */
static int wait_ready(struct dmdrvi_context* ctx, uint32_t timeout_ms)
{
    Dmod_Timestamp_t start = Dmod_GetUptime();
    for (uint32_t polls = 0;; polls++)
    {
        uint8_t byte = 0;
        int ret = xfer(ctx, NULL, &byte, 1);
        if (ret != 0)
        {
            return ret;
        }
        if (byte == 0xFF)
        {
            return 0;
        }
        if (polls >= FAST_POLLS)
        {
            if (Dmod_GetUptime() - start >= timeout_ms)
            {
                return -ETIMEDOUT;
            }
            dmosi_thread_sleep(1);
        }
    }
}

/* ---- Commands ---- */

/* CRC7 of the command frame, shifted into the last byte with the stop bit */
static uint8_t crc7(const uint8_t* data, size_t size)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < size; i++)
    {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (uint8_t)(crc << 1);
            if ((byte ^ crc) & 0x80u)
            {
                crc ^= 0x09u;
            }
            byte = (uint8_t)(byte << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1u);
}

/* The command and its R1; the stuff byte of CMD12 is skipped */
static int command(struct dmdrvi_context* ctx, uint8_t index, uint32_t argument, uint8_t* r1)
{
    uint8_t frame[6] = { (uint8_t)(0x40u | index), (uint8_t)(argument >> 24), (uint8_t)(argument >> 16), (uint8_t)(argument >> 8),
                         (uint8_t)argument, 0 };
    frame[5] = crc7(frame, 5);
    int ret = xfer(ctx, frame, NULL, sizeof(frame));
    if (ret == 0 && index == SD_CMD_STOP_TRANSMISSION)
    {
        ret = xfer(ctx, NULL, NULL, 1);
    }
    for (int i = 0; ret == 0 && i < RESPONSE_TRIES; i++)
    {
        uint8_t byte = 0xFF;
        ret = xfer(ctx, NULL, &byte, 1);
        if (ret == 0 && (byte & 0x80u) == 0)
        {
            *r1 = byte;
            return 0;
        }
    }
    return ret != 0 ? ret : -ETIMEDOUT;
}

/* The R1 of the SPI mode as the status word of the SD bus, so the status checks above do not change */
static uint32_t r1_status(uint8_t r1)
{
    uint32_t status = 0;
    if (r1 & R1_ILLEGAL_COMMAND) status |= SD_R1_ILLEGAL_COMMAND;
    if (r1 & R1_COM_CRC_ERROR)   status |= SD_R1_COM_CRC_ERROR;
    if (r1 & R1_ERASE_SEQ_ERROR) status |= SD_R1_ERASE_SEQ_ERROR;
    if (r1 & R1_ADDRESS_ERROR)   status |= SD_R1_ADDRESS_ERROR;
    if (r1 & R1_PARAMETER_ERROR) status |= SD_R1_OUT_OF_RANGE;
    if ((r1 & R1_IDLE) == 0)
    {
        status |= SD_R1_READY_FOR_DATA | (SD_STATE_TRAN << 9);     /* out of the idle state, the busy wait has passed */
    }
    return status;
}

static uint32_t be32(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static dmsdio_status_t transport_status(int error)
{
    return error == -ETIMEDOUT ? dmsdio_status_cmd_timeout : dmsdio_status_overrun;
}

/* ---- Data phases ---- */

/*
 * A block moves in one transfer, through a scratch buffer of the module (the internal RAM, aligned, a multiple of four bytes long: the
 * DMA of dmspi takes such a transfer as it is, not by pieces through a bounce buffer), not as the three calls of the token, the data and the CRC.
 * The few bytes read past the CRC of a block can be the token of the next one (the card streams the blocks of CMD18 without a gap): they are
 * kept in `carry` and looked at first.
 */
#define SCRATCH_SIZE            (DMSDIO_BLOCK_SIZE + 16u)
#define PIECE_SIZE              60u         /* A block is sent in pieces below 64 bytes (see write_blocks) */
#define RESPONSE_TRIES          8u          /* bytes the data response token of a write may come after the CRC */
#define CARRY_MAX               8u

typedef struct
{
    uint8_t bytes[CARRY_MAX];
    size_t  size;
} carry_t;

static bool scratch_get(struct dmdrvi_context* ctx)
{
    if (ctx->spi_scratch == NULL)
    {
        ctx->spi_scratch = Dmod_AlignedMalloc(2u * SCRATCH_SIZE, 32u);
    }
    return ctx->spi_scratch != NULL;
}

/* The start token of a block: the carry first, then eight bytes at a time; what comes after the token is left in `head` */
static int poll_token(struct dmdrvi_context* ctx, uint32_t timeout_ms, carry_t* carry, uint8_t* token, uint8_t* head, size_t* head_size)
{
    Dmod_Timestamp_t start = Dmod_GetUptime();
    uint8_t chunk[CARRY_MAX];
    size_t have = carry->size;
    memcpy(chunk, carry->bytes, have);
    carry->size = 0;
    for (uint32_t polls = 0;; polls++)
    {
        if (have == 0)
        {
            int ret = xfer(ctx, NULL, chunk, sizeof(chunk));
            if (ret != 0)
            {
                return ret;
            }
            have = sizeof(chunk);
        }
        for (size_t i = 0; i < have; i++)
        {
            if (chunk[i] != 0xFF)
            {
                *token = chunk[i];
                *head_size = have - 1u - i;
                memcpy(head, chunk + i + 1u, *head_size);
                return 0;
            }
        }
        have = 0;
        if (polls >= FAST_POLLS)
        {
            if (Dmod_GetUptime() - start >= timeout_ms)
            {
                return -ETIMEDOUT;
            }
            dmosi_thread_sleep(1);
        }
    }
}

static dmsdio_status_t read_blocks(struct dmdrvi_context* ctx, const dmsdio_data_t* data)
{
    uint8_t* buffer = (uint8_t*)data->buffer;
    carry_t carry = { .size = 0 };
    if (!scratch_get(ctx) || data->block_size + 2u > SCRATCH_SIZE - 4u)
    {
        return dmsdio_status_invalid;
    }
    for (uint32_t block = 0; block < data->block_count; block++)
    {
        uint8_t token = 0;
        uint8_t head[CARRY_MAX];
        size_t have = 0;
        uint8_t* dst = buffer + (size_t)block * data->block_size;
        int ret = poll_token(ctx, data->timeout_ms, &carry, &token, head, &have);
        if (ret == -ETIMEDOUT)
        {
            return dmsdio_status_data_timeout;
        }
        if (ret != 0 || token != TOKEN_BLOCK_START || have > data->block_size)    /* an error token: 000x xxxx */
        {
            return dmsdio_status_overrun;
        }
        memcpy(dst, head, have);
        size_t rest = data->block_size - have + 2u;                  /* the rest of the data and the CRC */
        size_t length = (rest + 3u) & ~3u;
        if (xfer(ctx, NULL, ctx->spi_scratch, length) != 0)
        {
            return dmsdio_status_overrun;
        }
        memcpy(dst + have, ctx->spi_scratch, data->block_size - have);
        carry.size = length - rest < CARRY_MAX ? length - rest : CARRY_MAX;
        memcpy(carry.bytes, ctx->spi_scratch + rest, carry.size);
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t write_blocks(struct dmdrvi_context* ctx, uint8_t index, const dmsdio_data_t* data)
{
    const uint8_t* buffer = (const uint8_t*)data->buffer;
    if (!scratch_get(ctx) || data->block_size + 8u > SCRATCH_SIZE)
    {
        return dmsdio_status_invalid;
    }
    uint8_t* tx = ctx->spi_scratch;
    for (uint32_t block = 0; block < data->block_count; block++)
    {
        /*
         * A gap byte, the token, the data, the CRC (not checked by the card), sent in pieces below 64 bytes and only sent: what comes in
         * meanwhile is not read. Then the data response token (xxx0 0101: accepted), read by itself: the card answers within the next bytes.
         * Measured on the Elecrow DIS08070H (ESP32-S3, dmspi): sent as one transfer (dmspi takes 64 bytes or more by DMA), the card never
         * answered with an accepted token at the place of the answer (0x00, every write reported as failed although its data was written);
         * in pieces every write is accepted. Pieces of 60 bytes are 9 transfers a block.
         */
        size_t length = 1u + 1u + data->block_size + 2u;
        memset(tx, 0xFF, length);
        tx[1] = index == SD_CMD_WRITE_MULTIPLE_BLOCK ? TOKEN_MULTI_WRITE : TOKEN_BLOCK_START;
        memcpy(tx + 2, buffer + (size_t)block * data->block_size, data->block_size);
        for (size_t sent = 0; sent < length; sent += PIECE_SIZE)
        {
            size_t piece = length - sent < PIECE_SIZE ? length - sent : PIECE_SIZE;
            if (xfer(ctx, tx + sent, NULL, piece) != 0)
            {
                return dmsdio_status_overrun;
            }
        }
        uint8_t response = 0xFF;
        for (unsigned tries = 0; response == 0xFF && tries < RESPONSE_TRIES; tries++)
        {
            if (xfer(ctx, NULL, &response, 1) != 0)
            {
                return dmsdio_status_overrun;
            }
        }
        if ((response & 0x1Fu) != 0x05u)                    /* xxx0 0101: accepted */
        {
            DMOD_LOG_WARN("dmsdio: write: the card answered 0x%02x\n", response);
            return (response & 0x1Fu) == 0x0Bu ? dmsdio_status_data_crc : dmsdio_status_overrun;
        }
        int ret = wait_ready(ctx, data->timeout_ms);
        if (ret != 0)
        {
            return ret == -ETIMEDOUT ? dmsdio_status_data_timeout : dmsdio_status_overrun;
        }
    }
    return dmsdio_status_ok;
}

/* CMD9/CMD10: the register comes as a data block; the layout of the long response of the SD bus is its four big-endian words */
static dmsdio_status_t register_block(struct dmdrvi_context* ctx, uint32_t timeout_ms, dmsdio_response_t* resp)
{
    uint8_t raw[16];
    dmsdio_data_t data = { .buffer = raw, .block_size = sizeof(raw), .block_count = 1, .direction = dmsdio_direction_read,
                           .timeout_ms = timeout_ms };
    dmsdio_status_t status = read_blocks(ctx, &data);
    for (unsigned i = 0; status == dmsdio_status_ok && i < 4u; i++)
    {
        resp->words[i] = be32(raw + 4u * i);
    }
    return status;
}

/* ---- The command with its response and data ---- */

static dmsdio_status_t run(struct dmdrvi_context* ctx, const dmsdio_command_t* cmd, const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    const uint8_t index = cmd->index;
    uint8_t r1 = 0;
    int ret;

    if (index == SD_CMD_STOP_TRANSMISSION && ctx->spi_multi_write)
    {
        ctx->spi_multi_write = false;                       /* the end of CMD25 is a token, not a command */
        ret = send_byte(ctx, 0xFF);
        if (ret == 0) ret = send_byte(ctx, TOKEN_STOP_TRAN);
        if (ret == 0) ret = xfer(ctx, NULL, NULL, 1);
        if (ret == 0) ret = wait_ready(ctx, ctx->config.write_timeout_ms);
        resp->words[0] = r1_status(0);
        return ret == 0 ? dmsdio_status_ok : (ret == -ETIMEDOUT ? dmsdio_status_data_timeout : dmsdio_status_overrun);
    }
    ctx->spi_multi_write = false;

    if (index != SD_CMD_GO_IDLE_STATE && index != SD_CMD_STOP_TRANSMISSION)
    {
        ret = wait_ready(ctx, ctx->config.write_timeout_ms);
        if (ret != 0)
        {
            return transport_status(ret);
        }
    }
    /* ACMD41 of the SPI mode carries the HCS bit only: the other bits are reserved (the voltage window is the business of CMD58) */
    if (index == SD_ACMD_SD_SEND_OP_COND)
    {
        /* Some cards lose the "next command is an application command" of CMD55 when the chip select goes up between the two: the prefix
         * is repeated here, inside the session (the CMD55 the core sent before it is only one more). */
        ret = command(ctx, SD_CMD_APP_CMD, 0, &r1);
        if (ret == 0) ret = send_byte(ctx, 0xFF);
        if (ret != 0)
        {
            return transport_status(ret);
        }
    }
    ret = command(ctx, index, index == SD_ACMD_SD_SEND_OP_COND ? (cmd->argument & SD_OCR_HCS) : cmd->argument, &r1);
    if (ret != 0)
    {
        return transport_status(ret);
    }
    resp->words[0] = r1_status(r1);

    switch (index)
    {
        case SD_CMD_SEND_IF_COND:                           /* R7: R1 and the echo; a v1.x card does not know the command */
        {
            uint8_t echo[4];
            if (r1 & R1_ILLEGAL_COMMAND)
            {
                return dmsdio_status_cmd_timeout;
            }
            if (xfer(ctx, NULL, echo, sizeof(echo)) != 0)
            {
                return dmsdio_status_overrun;
            }
            resp->words[0] = be32(echo);
            return dmsdio_status_ok;
        }
        case SD_CMD_APP_CMD:
            if ((r1 & R1_ERRORS) == 0)
            {
                resp->words[0] |= SD_R1_APP_CMD;
            }
            return dmsdio_status_ok;
        case SD_ACMD_SD_SEND_OP_COND:                       /* the R1 only; the OCR is CMD58 once the card is out of the idle state */
        {
            if (r1 & R1_ERRORS)
            {
                return dmsdio_status_ok;
            }
            if (r1 & R1_IDLE)
            {
                /* The card does its power-up with the clock it gets (a slow one sleeps through the pauses of the polling): keep it running */
                xfer(ctx, NULL, NULL, 512);
                resp->words[0] = SD_OCR_VOLTAGE_WINDOW;     /* not ready yet: no BUSY bit */
                return dmsdio_status_ok;
            }
            uint8_t ocr[4];
            ret = send_byte(ctx, 0xFF);
            if (ret == 0) ret = command(ctx, SPI_CMD58_READ_OCR, 0, &r1);
            if (ret == 0) ret = xfer(ctx, NULL, ocr, sizeof(ocr));
            if (ret != 0)
            {
                return transport_status(ret);
            }
            resp->words[0] = be32(ocr) | SD_OCR_BUSY;
            return dmsdio_status_ok;
        }
        case SD_CMD_SEND_CSD:
        case SD_CMD_SEND_CID:
            return (r1 & R1_ERRORS) ? dmsdio_status_ok : register_block(ctx, ctx->config.read_timeout_ms, resp);
        case SD_CMD_SEND_STATUS:                            /* R2: the second byte is more of the status (ACMD13 has the block after it) */
        {
            uint8_t second = 0;
            if (xfer(ctx, NULL, &second, 1) != 0)
            {
                return dmsdio_status_overrun;
            }
            if (second & 0x02u) resp->words[0] |= SD_R1_WP_ERASE_SKIP;
            if (second & 0x1Cu) resp->words[0] |= SD_R1_ERROR;
            if (second & 0x20u) resp->words[0] |= SD_R1_WP_VIOLATION;
            if (second & 0x40u) resp->words[0] |= SD_R1_ERASE_PARAM;
            if (second & 0x80u) resp->words[0] |= SD_R1_OUT_OF_RANGE;
            break;
        }
        default:
            break;
    }

    if ((r1 & R1_ERRORS) != 0)
    {
        return dmsdio_status_ok;                            /* the errors are in the status word; the data phase does not start */
    }
    if (data != NULL)
    {
        dmsdio_status_t status = data->direction == dmsdio_direction_read ? read_blocks(ctx, data) : write_blocks(ctx, index, data);
        ctx->spi_multi_write = status == dmsdio_status_ok && data->direction == dmsdio_direction_write &&
                               index == SD_CMD_WRITE_MULTIPLE_BLOCK;
        return status;
    }
    if (cmd->response == dmsdio_response_short_busy)        /* R1b: busy until the card is done (erase, the stop of a read) */
    {
        ret = wait_ready(ctx, index == SD_CMD_ERASE ? ctx->config.erase_timeout_ms : ctx->config.write_timeout_ms);
        if (ret != 0)
        {
            return ret == -ETIMEDOUT ? dmsdio_status_data_timeout : dmsdio_status_overrun;
        }
    }
    return dmsdio_status_ok;
}

dmsdio_status_t dmsdio_spi_execute(struct dmdrvi_context* ctx, const dmsdio_command_t* cmd, const dmsdio_data_t* data,
                                   dmsdio_response_t* resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->index = cmd->index;
    if (!dmsdio_spi_friends_there(ctx) || ctx->spi_hz == 0U)
    {
        return dmsdio_status_invalid;
    }
    if (session_begin(ctx, true, ctx->spi_hz) != 0)
    {
        return dmsdio_status_overrun;
    }
    dmsdio_status_t status = run(ctx, cmd, data, resp);
    send_byte(ctx, 0xFF);                                   /* eight clocks more: the card lets go of MISO */
    session_end(ctx);
    return status;
}

/* ---- The power and the clock ---- */

int dmsdio_spi_set_power(struct dmdrvi_context* ctx, bool on)
{
    ctx->spi_multi_write = false;
    if (!on)
    {
        return 0;
    }
    /* At least 74 clocks with the card deselected, at the identification speed: the card goes into its SPI mode at the CMD0 after them */
    int ret = session_begin(ctx, false, SD_CLOCK_IDENTIFICATION_HZ);
    if (ret != 0)
    {
        return ret;
    }
    uint8_t clocks[DUMMY_CLOCK_BYTES];
    memset(clocks, 0xFF, sizeof(clocks));
    ret = xfer(ctx, clocks, NULL, sizeof(clocks));
    session_end(ctx);
    return ret;
}

int dmsdio_spi_set_clock(struct dmdrvi_context* ctx, uint32_t max_hz, uint32_t* actual_hz)
{
    ctx->spi_hz = max_hz;           /* (dmspi puts the nearest speed it has below it on the bus for the length of a command) */
    if (actual_hz != NULL)
    {
        *actual_hz = max_hz;
    }
    return 0;
}
