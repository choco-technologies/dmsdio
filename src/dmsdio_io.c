#include "dmsdio_internal.h"
#include <string.h>

/*
 * Byte-offset I/O on top of 512-byte blocks. Runs with the context lock
 * held and a card attached.
 *
 * Every request is cut into steps, each one command:
 *  - direct: whole blocks moved straight between the caller's buffer and
 *    the card (up to max_blocks_per_transfer), when the port accepts that
 *    buffer for the direction (dmsdio_port_buffer_is_direct(): aligned,
 *    reachable, and fast enough - on STM32F7 a write from external SDRAM is
 *    not, it underruns the host FIFO at 48 MHz);
 *  - bounced: whole blocks the port does not accept go through the context's
 *    bounce buffer (up to bounce_blocks per command) - memory the port
 *    allocated, so the data phase itself always runs at full speed;
 *  - partial: a partial block at either end goes through the first block of
 *    the bounce buffer; writes read-modify-write it so bytes outside the
 *    requested range are preserved.
 */

#define BLOCK   ((uint64_t)DMSDIO_BLOCK_SIZE)

typedef enum
{
    step_direct,
    step_bounced,
    step_partial,
} step_kind_t;

typedef struct
{
    step_kind_t kind;
    uint64_t    lba;        /* first block touched by this step */
    uint32_t    skip;       /* bytes to skip inside that block (partial only) */
    uint32_t    blocks;     /* blocks moved by the command */
    size_t      length;     /* bytes consumed from the caller's buffer */
} io_step_t;

static uint64_t capacity(const struct dmdrvi_context* ctx)
{
    return ctx->card.block_count * BLOCK;
}

static uint32_t min_blocks(uint64_t blocks, uint32_t limit)
{
    return (blocks > limit) ? limit : (uint32_t)blocks;
}

/* Plan the next piece of a request starting at `offset` with `remaining` bytes. */
static io_step_t plan_step(const struct dmdrvi_context* ctx, const void* buf, uint64_t offset,
                           size_t remaining, dmsdio_direction_t direction)
{
    io_step_t step;
    step.lba  = offset / BLOCK;
    step.skip = (uint32_t)(offset % BLOCK);

    if (step.skip != 0 || remaining < BLOCK)
    {
        size_t in_block = (size_t)(BLOCK - step.skip);
        step.kind   = step_partial;
        step.blocks = 1;
        step.length = (remaining < in_block) ? remaining : in_block;
        return step;
    }
    step.blocks = min_blocks(remaining / BLOCK, ctx->config.max_blocks_per_transfer);
    step.kind   = step_direct;
    if (!dmsdio_port_buffer_is_direct(ctx->config.instance, buf, step.blocks * BLOCK, direction))
    {
        step.kind   = step_bounced;
        step.blocks = min_blocks(step.blocks, ctx->config.bounce_blocks);
    }
    step.length = (size_t)(step.blocks * BLOCK);
    return step;
}

static int read_step(struct dmdrvi_context* ctx, const io_step_t* step, uint8_t* dst)
{
    if (step->kind == step_direct)
    {
        return dmsdio_xfer_read(ctx, step->lba, dst, step->blocks);
    }
    int ret = dmsdio_xfer_read(ctx, step->lba, ctx->bounce, step->blocks);
    if (ret == 0)
    {
        memcpy(dst, ctx->bounce + step->skip, step->length);
    }
    return ret;
}

static int write_step(struct dmdrvi_context* ctx, const io_step_t* step, const uint8_t* src)
{
    if (step->kind == step_direct)
    {
        return dmsdio_xfer_write(ctx, step->lba, src, step->blocks);
    }
    int ret = 0;
    if (step->kind == step_partial && step->length != DMSDIO_BLOCK_SIZE)
    {
        ret = dmsdio_xfer_read(ctx, step->lba, ctx->bounce, 1);
    }
    if (ret == 0)
    {
        memcpy(ctx->bounce + step->skip, src, step->length);
        ret = dmsdio_xfer_write(ctx, step->lba, ctx->bounce, step->blocks);
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
        io_step_t step = plan_step(ctx, buf + done, offset + done, total - done, dmsdio_direction_read);
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
        io_step_t step = plan_step(ctx, buf + done, offset + done, total - done, dmsdio_direction_write);
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
