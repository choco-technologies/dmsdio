#include "dmsdio_internal.h"
#include <string.h>

/*
 * Byte-offset I/O on top of 512-byte blocks. Runs with the context lock
 * held and a card attached.
 *
 * Whole, suitably aligned blocks are transferred directly between the
 * caller's buffer and the card (up to max_blocks_per_transfer per command).
 * A partial block at either end - or a caller buffer that is not
 * DMSDIO_TRANSFER_ALIGNMENT aligned - goes through the context's scratch
 * block; writes then read-modify-write that single block so bytes outside
 * the requested range are preserved.
 */

#define BLOCK   ((uint64_t)DMSDIO_BLOCK_SIZE)

typedef struct
{
    uint64_t    lba;        /* first block touched by this step */
    uint32_t    skip;       /* bytes to skip inside that block */
    uint32_t    blocks;     /* direct blocks (0 = bounce one block) */
    size_t      length;     /* bytes consumed from the caller's buffer */
} io_step_t;

static bool is_aligned(const void* ptr)
{
    return ((uintptr_t)ptr % DMSDIO_TRANSFER_ALIGNMENT) == 0;
}

static uint64_t capacity(const struct dmdrvi_context* ctx)
{
    return ctx->card.block_count * BLOCK;
}

/* Plan the next piece of a request starting at `offset` with `remaining` bytes. */
static io_step_t plan_step(const struct dmdrvi_context* ctx, const void* buf,
                           uint64_t offset, size_t remaining)
{
    io_step_t step;
    step.lba  = offset / BLOCK;
    step.skip = (uint32_t)(offset % BLOCK);

    if (step.skip != 0 || remaining < BLOCK || !is_aligned(buf))
    {
        size_t in_block = (size_t)(BLOCK - step.skip);
        step.blocks = 0;
        step.length = (remaining < in_block) ? remaining : in_block;
        return step;
    }
    uint64_t blocks = remaining / BLOCK;
    if (blocks > ctx->config.max_blocks_per_transfer)
    {
        blocks = ctx->config.max_blocks_per_transfer;
    }
    step.blocks = (uint32_t)blocks;
    step.length = (size_t)(blocks * BLOCK);
    return step;
}

static int read_step(struct dmdrvi_context* ctx, const io_step_t* step, uint8_t* dst)
{
    if (step->blocks != 0)
    {
        return dmsdio_xfer_read(ctx, step->lba, dst, step->blocks);
    }
    int ret = dmsdio_xfer_read(ctx, step->lba, ctx->scratch, 1);
    if (ret == 0)
    {
        memcpy(dst, ctx->scratch + step->skip, step->length);
    }
    return ret;
}

static int write_step(struct dmdrvi_context* ctx, const io_step_t* step, const uint8_t* src)
{
    if (step->blocks != 0)
    {
        return dmsdio_xfer_write(ctx, step->lba, src, step->blocks);
    }
    int ret = 0;
    if (step->length != DMSDIO_BLOCK_SIZE)
    {
        ret = dmsdio_xfer_read(ctx, step->lba, ctx->scratch, 1);
    }
    if (ret == 0)
    {
        memcpy(ctx->scratch + step->skip, src, step->length);
        ret = dmsdio_xfer_write(ctx, step->lba, ctx->scratch, 1);
    }
    return ret;
}

/* Clamp a request to the card; returns the byte count to transfer. */
static size_t clamp_request(const struct dmdrvi_context* ctx, size_t size, uint64_t offset)
{
    uint64_t available = capacity(ctx) - offset;
    return ((uint64_t)size > available) ? (size_t)available : size;
}

dmdrvi_ssize_t dmsdio_io_read(struct dmdrvi_context* ctx, uint8_t* buf, size_t size, uint64_t offset)
{
    if (offset >= capacity(ctx))
    {
        return 0;
    }
    size_t total = clamp_request(ctx, size, offset);
    size_t done  = 0;
    while (done < total)
    {
        io_step_t step = plan_step(ctx, buf + done, offset + done, total - done);
        int ret = read_step(ctx, &step, buf + done);
        if (ret != 0)
        {
            return ret;
        }
        done += step.length;
    }
    return (dmdrvi_ssize_t)done;
}

dmdrvi_ssize_t dmsdio_io_write(struct dmdrvi_context* ctx, const uint8_t* buf, size_t size, uint64_t offset)
{
    if (ctx->card.write_protected)
    {
        return -EROFS;
    }
    if (offset >= capacity(ctx))
    {
        return -ENOSPC;
    }
    size_t total = clamp_request(ctx, size, offset);
    size_t done  = 0;
    while (done < total)
    {
        io_step_t step = plan_step(ctx, buf + done, offset + done, total - done);
        int ret = write_step(ctx, &step, buf + done);
        if (ret != 0)
        {
            return ret;
        }
        done += step.length;
    }
    return (dmdrvi_ssize_t)done;
}

int dmsdio_io_erase_range(struct dmdrvi_context* ctx, const dmdrvi_block_range_t* range, bool discard)
{
    if (range->offset < 0 || (range->offset % (dmdrvi_offset_t)BLOCK) != 0 ||
        (range->length % BLOCK) != 0)
    {
        return -EINVAL;
    }
    uint64_t offset = (uint64_t)range->offset;
    if (offset > capacity(ctx) || range->length > capacity(ctx) - offset)
    {
        return -EINVAL;
    }
    if (ctx->card.write_protected)
    {
        return -EROFS;
    }
    if (range->length == 0)
    {
        return 0;
    }
    return dmsdio_xfer_erase(ctx, offset / BLOCK, range->length / BLOCK, discard);
}
