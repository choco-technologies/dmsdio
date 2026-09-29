#include "dmsdio_internal.h"
#include "dmsdio_sd.h"

/*
 * Block transfers with retry and recovery. Runs with the context lock held
 * and a card attached. Callers pass already validated block ranges.
 *
 * Error policy:
 *   - CRC errors, timeouts and FIFO/DMA faults are transient: the card is
 *     brought back to the transfer state and the whole operation retried.
 *     Once `retries` extra attempts failed the result is -EIO; with
 *     retries=0 the precise cause (-EBADMSG, -ETIMEDOUT, -EIO) is returned.
 *   - Malformed responses (-EPROTO) and card status errors are returned
 *     immediately.
 *   - A card that stops answering CMD13 during recovery is treated as
 *     removed: it is detached and the operation fails with -ENODEV.
 */

#define ERASE_CHUNK_BLOCKS      8192u   /* 4 MiB per CMD38 */
#define RECOVERY_STATUS_TRIES   2

typedef int (*xfer_op_t)(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count);

static uint32_t card_address(const struct dmdrvi_context* ctx, uint64_t lba)
{
    return ctx->card.block_addressing ? (uint32_t)lba : (uint32_t)(lba * DMSDIO_BLOCK_SIZE);
}

static int read_op(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count)
{
    dmsdio_data_t data = {
        .buffer      = buf,
        .block_size  = DMSDIO_BLOCK_SIZE,
        .block_count = count,
        .direction   = dmsdio_direction_read,
        .timeout_ms  = ctx->config.read_timeout_ms,
    };
    uint8_t index = (count > 1) ? SD_CMD_READ_MULTIPLE_BLOCK : SD_CMD_READ_SINGLE_BLOCK;
    int ret = dmsdio_cmd_data(ctx, false, index, card_address(ctx, lba), &data, NULL);
    if (count > 1)
    {
        int stop = dmsdio_cmd_stop(ctx);
        ret = (ret != 0) ? ret : stop;
    }
    return ret;
}

static int write_op(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count)
{
    dmsdio_data_t data = {
        .buffer      = buf,
        .block_size  = DMSDIO_BLOCK_SIZE,
        .block_count = count,
        .direction   = dmsdio_direction_write,
        .timeout_ms  = ctx->config.write_timeout_ms,
    };
    uint8_t index = (count > 1) ? SD_CMD_WRITE_MULTIPLE_BLOCK : SD_CMD_WRITE_BLOCK;
    int ret = dmsdio_cmd_data(ctx, false, index, card_address(ctx, lba), &data, NULL);
    if (count > 1)
    {
        int stop = dmsdio_cmd_stop(ctx);
        ret = (ret != 0) ? ret : stop;
    }
    return (ret != 0) ? ret : dmsdio_cmd_wait_ready(ctx, ctx->config.write_timeout_ms);
}

/* Erase busy limit: SD Status based estimate, never below the configured floor. */
static uint32_t erase_timeout_ms(const struct dmdrvi_context* ctx, uint32_t count)
{
    const dmsdio_ssr_t* ssr = &ctx->card.ssr;
    uint32_t timeout = ctx->config.erase_timeout_ms;
    if (ssr->erase_size != 0 && ssr->erase_timeout_s != 0 && ssr->au_size_bytes != 0)
    {
        uint64_t bytes = (uint64_t)count * DMSDIO_BLOCK_SIZE;
        uint64_t aus   = (bytes + ssr->au_size_bytes - 1u) / ssr->au_size_bytes;
        uint64_t ms    = aus * ssr->erase_timeout_s * 1000u / ssr->erase_size
                       + (uint64_t)ssr->erase_offset_s * 1000u;
        timeout = (ms > timeout) ? (uint32_t)ms : timeout;
    }
    return timeout;
}

static int erase_common(struct dmdrvi_context* ctx, uint64_t lba, uint32_t count, uint32_t arg)
{
    int ret = dmsdio_cmd_data(ctx, false, SD_CMD_ERASE_WR_BLK_START, card_address(ctx, lba), NULL, NULL);
    if (ret == 0)
    {
        ret = dmsdio_cmd_data(ctx, false, SD_CMD_ERASE_WR_BLK_END,
                              card_address(ctx, lba + count - 1u), NULL, NULL);
    }
    if (ret == 0)
    {
        dmsdio_response_t resp;
        ret = dmsdio_cmd_send(ctx, SD_CMD_ERASE, arg, dmsdio_response_short_busy, &resp);
    }
    return (ret != 0) ? ret : dmsdio_cmd_wait_ready(ctx, erase_timeout_ms(ctx, count));
}

static int erase_op(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count)
{
    (void)buf;
    return erase_common(ctx, lba, count, SD_ERASE_ARG);
}

static int discard_op(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count)
{
    (void)buf;
    return erase_common(ctx, lba, count, SD_DISCARD_ARG);
}

/*
 * Bring the card back to the transfer state after a failed operation.
 * Returns -ENODEV when the card no longer answers at all.
 */
static int recover(struct dmdrvi_context* ctx)
{
    uint32_t status = 0;
    int ret = -ETIMEDOUT;
    for (int i = 0; i < RECOVERY_STATUS_TRIES && ret != 0; i++)
    {
        ret = dmsdio_cmd_status(ctx, &status);
        if (ret == -ETIMEDOUT && ctx->removal_pending)
        {
            break;
        }
    }
    if (ret == -ETIMEDOUT)
    {
        return -ENODEV;
    }
    if (ret != 0)
    {
        return ret;
    }
    uint32_t state = SD_R1_STATE(status);
    if (state == SD_STATE_DATA || state == SD_STATE_RCV)
    {
        dmsdio_cmd_stop(ctx);
    }
    return dmsdio_cmd_wait_ready(ctx, ctx->config.write_timeout_ms);
}

static bool is_transient(int error)
{
    return error == -ETIMEDOUT || error == -EBADMSG || error == -EIO;
}

static int run(struct dmdrvi_context* ctx, xfer_op_t op, uint64_t lba, void* buf, uint32_t count)
{
    int last = 0;
    for (uint32_t attempt = 0; attempt <= ctx->config.retries; attempt++)
    {
        if (ctx->removal_pending)
        {
            return dmsdio_card_lost(ctx, -ENODEV);
        }
        last = op(ctx, lba, buf, count);
        if (last == 0)
        {
            ctx->retry_count += (attempt > 0) ? 1u : 0u;
            return 0;
        }
        int rec = recover(ctx);
        if (rec == -ENODEV || ctx->removal_pending)
        {
            return dmsdio_card_lost(ctx, -ENODEV);
        }
        if (!is_transient(last) || rec != 0)
        {
            return last;
        }
        DMOD_LOG_WARN("dmsdio: transfer at block %lu failed (%d), attempt %u\n",
                      (unsigned long)lba, last, (unsigned)(attempt + 1u));
    }
    return (ctx->config.retries == 0) ? last : -EIO;
}

int dmsdio_xfer_read(struct dmdrvi_context* ctx, uint64_t lba, void* buf, uint32_t count)
{
    return run(ctx, read_op, lba, buf, count);
}

int dmsdio_xfer_write(struct dmdrvi_context* ctx, uint64_t lba, const void* buf, uint32_t count)
{
    /* The port only reads from buf in the write direction. */
    return run(ctx, write_op, lba, (void*)buf, count);
}

int dmsdio_xfer_erase(struct dmdrvi_context* ctx, uint64_t lba, uint64_t count, bool discard)
{
    xfer_op_t op = discard ? discard_op : erase_op;
    while (count > 0)
    {
        uint32_t chunk = (count > ERASE_CHUNK_BLOCKS) ? ERASE_CHUNK_BLOCKS : (uint32_t)count;
        int ret = run(ctx, op, lba, NULL, chunk);
        if (ret != 0)
        {
            return ret;
        }
        lba   += chunk;
        count -= chunk;
    }
    return 0;
}
