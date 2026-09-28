/*
 * Command processing of the mock SD card.
 */
#include "mock_card.h"
#include "dmod.h"
#include <string.h>

#define R1_OUT_OF_RANGE     (1u << 31)
#define R1_ADDRESS_ERROR    (1u << 30)
#define R1_BLOCK_LEN_ERROR  (1u << 29)
#define R1_ERASE_SEQ_ERROR  (1u << 28)
#define R1_WP_VIOLATION     (1u << 26)
#define R1_ILLEGAL_COMMAND  (1u << 22)
#define R1_READY_FOR_DATA   (1u << 8)
#define R1_APP_CMD          (1u << 5)

enum { ST_IDLE, ST_READY, ST_IDENT, ST_STBY, ST_TRAN, ST_DATA, ST_RCV, ST_PRG };

#define OCR_WINDOW          0x00FF8000u
#define OCR_CCS             0x40000000u
#define OCR_BUSY            0x80000000u
#define ERASE_MAX_BLOCKS    256u

static bool is_high_capacity(const mock_host_t* host)
{
    return host->type == dmsdio_card_type_sdhc || host->type == dmsdio_card_type_sdxc;
}

void mock_card_reset(mock_host_t* host)
{
    host->state        = ST_IDLE;
    host->rca          = 0;
    host->app_cmd      = false;
    host->hcs_seen     = false;
    host->acmd41_polls = 0;
    host->card_4bit    = false;
    host->high_speed   = false;
    host->busy_polls   = 0;
    host->status_errors = 0;
    host->stats.card_bus_4bit   = false;
    host->stats.card_high_speed = false;
}

static uint32_t r1(const mock_host_t* host, uint32_t state)
{
    bool ready = (state == ST_TRAN && host->busy_polls == 0);
    uint32_t reported = (state == ST_TRAN && host->busy_polls != 0) ? ST_PRG : state;
    return host->status_errors | (reported << 9) | (ready ? R1_READY_FOR_DATA : 0u) |
           (host->app_cmd ? R1_APP_CMD : 0u);
}

static bool addressed(const mock_host_t* host, uint32_t arg)
{
    return (arg >> 16) == host->rca;
}

/* ---- data phase helpers ---- */

static dmsdio_status_t data_check(mock_host_t* host, const dmsdio_data_t* data,
                                  uint32_t block_size, uint32_t min_count)
{
    if (data == NULL || data->buffer == NULL || data->block_size != block_size ||
        data->block_count < min_count || data->timeout_ms == 0)
    {
        return dmsdio_status_invalid;
    }
    if (host->data_fault != dmsdio_mock_fault_none)
    {
        dmsdio_mock_fault_t fault = host->data_fault;
        host->data_fault = dmsdio_mock_fault_none;
        return (fault == dmsdio_mock_fault_data_crc) ? dmsdio_status_data_crc : dmsdio_status_data_timeout;
    }
    bool card_wide = host->card_4bit;
    bool host_wide = (host->host_width == dmsdio_bus_width_4bit);
    if (card_wide != host_wide)
    {
        return dmsdio_status_data_crc;      /* lanes disagree: garbage on the bus */
    }
    if (host->clock_hz > MOCK_DEFAULT_SPEED_HZ && !host->high_speed)
    {
        return dmsdio_status_data_crc;      /* overclocked default-speed card */
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t send_register(mock_host_t* host, const dmsdio_data_t* data,
                                     const uint8_t* reg, uint32_t size)
{
    dmsdio_status_t st = data_check(host, data, size, 1);
    if (st == dmsdio_status_ok)
    {
        memcpy(data->buffer, reg, size);
    }
    return st;
}

/* Count a transferred block; returns false once the card was pulled. */
static bool consume_block(mock_host_t* host)
{
    if (host->remove_after == 0)
    {
        host->inserted = false;
        host->remove_after = -1;
        return false;
    }
    if (host->remove_after > 0)
    {
        host->remove_after--;
    }
    return true;
}

static bool to_lba(mock_host_t* host, uint32_t arg, uint32_t count, uint64_t* lba, uint32_t* error)
{
    host->stats.last_address = arg;
    if (!is_high_capacity(host) && (arg % MOCK_BLOCK_SIZE) != 0)
    {
        *error = R1_ADDRESS_ERROR;
        return false;
    }
    *lba = is_high_capacity(host) ? arg : (uint64_t)arg / MOCK_BLOCK_SIZE;
    if (*lba + count > mock_card_capacity_blocks(host->type))
    {
        *error = R1_OUT_OF_RANGE;
        return false;
    }
    return true;
}

/* ---- block commands ---- */

static dmsdio_status_t cmd_read(mock_host_t* host, const dmsdio_command_t* cmd,
                                const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    bool multi = (cmd->index == 18);
    uint64_t lba = 0;
    uint32_t error = 0;
    resp->words[0] = r1(host, host->state);
    host->state = multi ? ST_DATA : ST_TRAN;
    dmsdio_status_t st = data_check(host, data, MOCK_BLOCK_SIZE, 1);
    if (st != dmsdio_status_ok || (!multi && data->block_count != 1))
    {
        return (st != dmsdio_status_ok) ? st : dmsdio_status_invalid;
    }
    if (!to_lba(host, cmd->argument, data->block_count, &lba, &error))
    {
        resp->words[0] |= error;
        return dmsdio_status_data_timeout;
    }
    for (uint32_t i = 0; i < data->block_count; i++)
    {
        if (!consume_block(host))
        {
            return dmsdio_status_data_timeout;
        }
        mock_store_read(host, lba + i, (uint8_t*)data->buffer + (size_t)i * MOCK_BLOCK_SIZE);
        host->stats.blocks_read++;
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t cmd_write(mock_host_t* host, const dmsdio_command_t* cmd,
                                 const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    bool multi = (cmd->index == 25);
    uint64_t lba = 0;
    uint32_t error = host->write_protect ? R1_WP_VIOLATION : 0u;
    resp->words[0] = r1(host, host->state);
    host->state = multi ? ST_RCV : ST_TRAN;
    dmsdio_status_t st = data_check(host, data, MOCK_BLOCK_SIZE, 1);
    if (st != dmsdio_status_ok || (!multi && data->block_count != 1))
    {
        return (st != dmsdio_status_ok) ? st : dmsdio_status_invalid;
    }
    if (error != 0 || !to_lba(host, cmd->argument, data->block_count, &lba, &error))
    {
        resp->words[0] |= error;
        return dmsdio_status_data_timeout;
    }
    host->busy_polls = 1;
    for (uint32_t i = 0; i < data->block_count; i++)
    {
        if (!consume_block(host))
        {
            return dmsdio_status_data_timeout;
        }
        const uint8_t* src = (const uint8_t*)data->buffer + (size_t)i * MOCK_BLOCK_SIZE;
        if (mock_store_write(host, lba + i, src) != 0)
        {
            return dmsdio_status_data_crc;
        }
        host->stats.blocks_written++;
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t cmd_stop(mock_host_t* host, dmsdio_response_t* resp)
{
    if (host->state != ST_DATA && host->state != ST_RCV)
    {
        host->status_errors |= R1_ILLEGAL_COMMAND;
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state);
    host->busy_polls = (host->state == ST_RCV) ? 1u : 0u;
    host->state = ST_TRAN;
    return dmsdio_status_ok;
}

static dmsdio_status_t cmd_erase(mock_host_t* host, const dmsdio_command_t* cmd, dmsdio_response_t* resp)
{
    uint64_t lba = 0;
    uint32_t error = 0;
    resp->words[0] = r1(host, host->state);
    if (cmd->index != 38)
    {
        uint64_t* target = (cmd->index == 32) ? &host->erase_start : &host->erase_end;
        if (!to_lba(host, cmd->argument, 1, &lba, &error))
        {
            resp->words[0] |= error;
            return dmsdio_status_ok;
        }
        *target = lba;
        return dmsdio_status_ok;
    }
    if (host->erase_end < host->erase_start || host->erase_end - host->erase_start >= ERASE_MAX_BLOCKS)
    {
        resp->words[0] |= R1_ERASE_SEQ_ERROR;
        return dmsdio_status_ok;
    }
    uint8_t zero[MOCK_BLOCK_SIZE];
    memset(zero, 0, sizeof(zero));
    for (lba = host->erase_start; lba <= host->erase_end; lba++)
    {
        mock_store_write(host, lba, zero);
    }
    host->busy_polls = 1;
    return dmsdio_status_ok;
}

/* ---- identification commands ---- */

static dmsdio_status_t cmd_if_cond(mock_host_t* host, uint32_t arg, dmsdio_response_t* resp)
{
    if (host->type == dmsdio_card_type_sdsc_v1 || host->state != ST_IDLE || ((arg >> 8) & 0xFu) != 1u)
    {
        return dmsdio_status_cmd_timeout;       /* v1 cards ignore CMD8 */
    }
    resp->words[0] = arg & 0xFFFu;
    return dmsdio_status_ok;
}

static dmsdio_status_t acmd_op_cond(mock_host_t* host, uint32_t arg, dmsdio_response_t* resp)
{
    if (host->state != ST_IDLE && host->state != ST_READY)
    {
        return dmsdio_status_cmd_timeout;
    }
    host->hcs_seen = host->hcs_seen || (arg & OCR_CCS) != 0;
    bool hc = is_high_capacity(host);
    bool done = (++host->acmd41_polls > MOCK_ACMD41_BUSY_POLLS) && (!hc || host->hcs_seen);
    resp->index = 0x3F;
    resp->words[0] = OCR_WINDOW;
    if (done)
    {
        host->state = ST_READY;
        resp->words[0] |= OCR_BUSY | ((hc && host->hcs_seen) ? OCR_CCS : 0u);
    }
    return dmsdio_status_ok;
}

static dmsdio_status_t cmd_ident(mock_host_t* host, const dmsdio_command_t* cmd, dmsdio_response_t* resp)
{
    if (cmd->index == 2 && host->state == ST_READY)
    {
        host->state = ST_IDENT;
        resp->index = 0x3F;
        mock_card_cid(host, resp->words);
        return dmsdio_status_ok;
    }
    if (cmd->index == 3 && (host->state == ST_IDENT || host->state == ST_STBY))
    {
        host->state = ST_STBY;
        host->rca = (uint16_t)(0x1234u + host->type);
        resp->words[0] = ((uint32_t)host->rca << 16) | (ST_STBY << 9);
        return dmsdio_status_ok;
    }
    if (cmd->index == 9 && host->state == ST_STBY && addressed(host, cmd->argument))
    {
        resp->index = 0x3F;
        mock_card_csd(host, resp->words);
        return dmsdio_status_ok;
    }
    return dmsdio_status_cmd_timeout;
}

static dmsdio_status_t cmd_select(mock_host_t* host, uint32_t arg, dmsdio_response_t* resp)
{
    if (!addressed(host, arg) || (host->state != ST_STBY && host->state != ST_TRAN))
    {
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state);
    host->state = ST_TRAN;
    return dmsdio_status_ok;
}

static dmsdio_status_t cmd_status(mock_host_t* host, uint32_t arg, dmsdio_response_t* resp)
{
    if (!addressed(host, arg) || host->state < ST_STBY)
    {
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state);
    host->status_errors = 0;
    if (host->busy_polls > 0)
    {
        host->busy_polls--;
    }
    return dmsdio_status_ok;
}

/* ---- transfer-state configuration commands ---- */

static dmsdio_status_t cmd_switch(mock_host_t* host, const dmsdio_command_t* cmd,
                                  const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    uint8_t status[64];
    if (host->state != ST_TRAN || host->type == dmsdio_card_type_sdsc_v1)
    {
        host->status_errors |= R1_ILLEGAL_COMMAND;
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state);
    mock_card_switch_status(host, cmd->argument, status);
    host->stats.card_high_speed = host->high_speed;
    return send_register(host, data, status, sizeof(status));
}

static dmsdio_status_t cmd_blocklen(mock_host_t* host, uint32_t arg, dmsdio_response_t* resp)
{
    if (host->state != ST_TRAN)
    {
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state) | ((arg != MOCK_BLOCK_SIZE) ? R1_BLOCK_LEN_ERROR : 0u);
    return dmsdio_status_ok;
}

static dmsdio_status_t app_command(mock_host_t* host, const dmsdio_command_t* cmd,
                                   const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    uint8_t reg[64];
    if (cmd->index == 41)
    {
        return acmd_op_cond(host, cmd->argument, resp);
    }
    if (host->state != ST_TRAN)
    {
        return dmsdio_status_cmd_timeout;
    }
    resp->words[0] = r1(host, host->state) | R1_APP_CMD;
    switch (cmd->index)
    {
        case 6:
            host->card_4bit = (cmd->argument & 0x3u) == 2u;
            host->stats.card_bus_4bit = host->card_4bit;
            return dmsdio_status_ok;
        case 13:
            mock_card_ssr(host, reg);
            return send_register(host, data, reg, 64);
        case 42:
            return dmsdio_status_ok;
        case 51:
            mock_card_scr(host, reg);
            return send_register(host, data, reg, 8);
        default:
            return dmsdio_status_cmd_timeout;
    }
}

static dmsdio_status_t command(mock_host_t* host, const dmsdio_command_t* cmd,
                               const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    switch (cmd->index)
    {
        case 0:  mock_card_reset(host); return dmsdio_status_ok;
        case 2: case 3: case 9: return cmd_ident(host, cmd, resp);
        case 6:  return cmd_switch(host, cmd, data, resp);
        case 7:  return cmd_select(host, cmd->argument, resp);
        case 8:  return cmd_if_cond(host, cmd->argument, resp);
        case 12: return cmd_stop(host, resp);
        case 13: return cmd_status(host, cmd->argument, resp);
        case 16: return cmd_blocklen(host, cmd->argument, resp);
        case 17: case 18: return (host->state == ST_TRAN) ? cmd_read(host, cmd, data, resp) : dmsdio_status_cmd_timeout;
        case 24: case 25: return (host->state == ST_TRAN) ? cmd_write(host, cmd, data, resp) : dmsdio_status_cmd_timeout;
        case 32: case 33: case 38: return (host->state == ST_TRAN) ? cmd_erase(host, cmd, resp) : dmsdio_status_cmd_timeout;
        case 55:
            if (host->state >= ST_STBY && !addressed(host, cmd->argument))
            {
                return dmsdio_status_cmd_timeout;
            }
            host->app_cmd = true;
            resp->words[0] = r1(host, host->state);
            return dmsdio_status_ok;
        default:
            host->status_errors |= R1_ILLEGAL_COMMAND;
            return dmsdio_status_cmd_timeout;
    }
}

dmsdio_status_t mock_card_execute(mock_host_t* host, const dmsdio_command_t* cmd,
                                  const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->index = cmd->index;
    if (!host->inserted || !host->powered)
    {
        /* Nothing answers - which only shows for commands expecting a response. */
        return (cmd->response == dmsdio_response_none) ? dmsdio_status_ok : dmsdio_status_cmd_timeout;
    }
    if (host->state <= ST_IDENT && host->clock_hz > MOCK_IDENT_CLOCK_HZ)
    {
        return dmsdio_status_cmd_crc;       /* identification must run at <= 400 kHz */
    }
    bool app = host->app_cmd;
    host->app_cmd = false;
    if (app)
    {
        host->stats.app_commands[cmd->index & 63u]++;
        return app_command(host, cmd, data, resp);
    }
    host->stats.commands[cmd->index & 63u]++;
    return command(host, cmd, data, resp);
}
