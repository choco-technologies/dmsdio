#include "dmsdio_internal.h"
#include "dmsdio_sd.h"

/*
 * Command layer: one SD command at a time, mapped to errno values.
 *
 * Every transport failure is followed by dmsdio_port_abort() so the
 * controller is idle before the caller decides how to recover.
 */

int dmsdio_cmd_errno(dmsdio_status_t status)
{
    switch (status)
    {
        case dmsdio_status_ok:              return 0;
        case dmsdio_status_cmd_timeout:     return -ETIMEDOUT;
        case dmsdio_status_data_timeout:    return -ETIMEDOUT;
        case dmsdio_status_cmd_crc:         return -EBADMSG;
        case dmsdio_status_data_crc:        return -EBADMSG;
        case dmsdio_status_invalid:         return -EINVAL;
        case dmsdio_status_not_supported:   return -ENOTSUP;
        case dmsdio_status_overrun:
        default:                            return -EIO;
    }
}

/* Map error bits of an R1 card status to the most specific errno. */
static int r1_errno(uint32_t status)
{
    if ((status & SD_R1_ERRORS) == 0)
    {
        return 0;
    }
    if (status & (SD_R1_OUT_OF_RANGE | SD_R1_ADDRESS_ERROR | SD_R1_BLOCK_LEN_ERROR |
                  SD_R1_ERASE_SEQ_ERROR | SD_R1_ERASE_PARAM))
    {
        return -EINVAL;
    }
    if (status & (SD_R1_WP_VIOLATION | SD_R1_WP_ERASE_SKIP))
    {
        return -EROFS;
    }
    if (status & SD_R1_COM_CRC_ERROR)
    {
        return -EBADMSG;
    }
    if (status & SD_R1_ILLEGAL_COMMAND)
    {
        return -EPROTO;
    }
    if (status & SD_R1_LOCK_UNLOCK_FAILED)
    {
        return -EACCES;
    }
    return -EIO;
}

static bool has_index_field(dmsdio_response_type_t type)
{
    return type == dmsdio_response_short || type == dmsdio_response_short_busy;
}

static int execute(struct dmdrvi_context* ctx, const dmsdio_command_t* cmd,
                   const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    dmsdio_status_t status = dmsdio_port_execute(ctx->config.instance, cmd, data, resp);
    int ret = dmsdio_cmd_errno(status);
    if (ret == 0 && has_index_field(cmd->response) && resp->index != cmd->index)
    {
        DMOD_LOG_ERROR("dmsdio: CMD%u answered with index %u\n", cmd->index, resp->index);
        ret = -EPROTO;
    }
    if (ret != 0)
    {
        dmsdio_port_abort(ctx->config.instance);
        ctx->last_error = ret;
    }
    return ret;
}

int dmsdio_cmd_send(struct dmdrvi_context* ctx, uint8_t index, uint32_t arg,
                    dmsdio_response_type_t type, dmsdio_response_t* resp)
{
    dmsdio_response_t local = { { 0 }, 0 };
    dmsdio_command_t cmd = { index, arg, type };
    return execute(ctx, &cmd, NULL, resp != NULL ? resp : &local);
}

static int app_prefix(struct dmdrvi_context* ctx)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_APP_CMD, (uint32_t)ctx->card.rca << 16,
                              dmsdio_response_short, &resp);
    if (ret == 0 && (resp.words[0] & SD_R1_APP_CMD) == 0)
    {
        DMOD_LOG_ERROR("dmsdio: card did not accept APP_CMD\n");
        ret = -EPROTO;
    }
    return ret;
}

int dmsdio_cmd_app(struct dmdrvi_context* ctx, uint8_t index, uint32_t arg,
                   dmsdio_response_type_t type, dmsdio_response_t* resp)
{
    int ret = app_prefix(ctx);
    return ret != 0 ? ret : dmsdio_cmd_send(ctx, index, arg, type, resp);
}

int dmsdio_cmd_data(struct dmdrvi_context* ctx, bool app, uint8_t index, uint32_t arg,
                    const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    dmsdio_response_t local = { { 0 }, 0 };
    dmsdio_command_t cmd = { index, arg, dmsdio_response_short };
    resp = (resp != NULL) ? resp : &local;

    int ret = app ? app_prefix(ctx) : 0;
    if (ret == 0)
    {
        ret = execute(ctx, &cmd, data, resp);
    }
    return ret != 0 ? ret : r1_errno(resp->words[0]);
}

int dmsdio_cmd_status(struct dmdrvi_context* ctx, uint32_t* status)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_SEND_STATUS, (uint32_t)ctx->card.rca << 16,
                              dmsdio_response_short, &resp);
    if (ret == 0)
    {
        *status = resp.words[0];
    }
    return ret;
}

int dmsdio_cmd_stop(struct dmdrvi_context* ctx)
{
    dmsdio_response_t resp;
    int ret = dmsdio_cmd_send(ctx, SD_CMD_STOP_TRANSMISSION, 0, dmsdio_response_short_busy, &resp);
    /* OUT_OF_RANGE is legal here when a multi-block read ended at the last block. */
    return ret != 0 ? ret : r1_errno(resp.words[0] & ~SD_R1_OUT_OF_RANGE);
}

static bool deadline_passed(Dmod_Timestamp_t start, uint32_t waited_ms, uint32_t timeout_ms)
{
    Dmod_Timestamp_t now = Dmod_GetUptime();
    return waited_ms >= timeout_ms || (now > start && now - start >= timeout_ms);
}

int dmsdio_cmd_wait_ready(struct dmdrvi_context* ctx, uint32_t timeout_ms)
{
    Dmod_Timestamp_t start = Dmod_GetUptime();
    uint32_t waited_ms = 0;
    for (;;)
    {
        uint32_t status = 0;
        int ret = dmsdio_cmd_status(ctx, &status);
        if (ret == 0)
        {
            ret = r1_errno(status);
        }
        if (ret != 0)
        {
            return ret;
        }
        if ((status & SD_R1_READY_FOR_DATA) && SD_R1_STATE(status) == SD_STATE_TRAN)
        {
            return 0;
        }
        if (deadline_passed(start, waited_ms, timeout_ms))
        {
            ctx->last_error = -ETIMEDOUT;
            return -ETIMEDOUT;
        }
        dmosi_thread_sleep(1);
        waited_ms++;
    }
}
