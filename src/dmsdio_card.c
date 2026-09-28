#include "dmsdio_internal.h"
#include <string.h>

/*
 * Card attach/detach bookkeeping. Every function here runs with the
 * context lock held.
 *
 * Each attach and each detach bumps ctx->generation. Handles remember the
 * generation they were opened for, so a handle opened for a card that has
 * since been removed (or replaced by another card) is rejected with -ESTALE
 * instead of silently touching the wrong medium.
 */

static const char* type_name(dmsdio_card_type_t type)
{
    switch (type)
    {
        case dmsdio_card_type_sdsc_v1: return "SDSC v1";
        case dmsdio_card_type_sdsc_v2: return "SDSC v2";
        case dmsdio_card_type_sdhc:    return "SDHC";
        case dmsdio_card_type_sdxc:    return "SDXC";
        default:                       return "none";
    }
}

/*
 * dmdevfs drops hot-plug notices for a context it has not registered yet, so
 * the card node is announced only after dmdrvi_path_ready() reported the
 * host node (see dmsdio_card_host_ready()).
 */
static void notify(struct dmdrvi_context* ctx, bool available)
{
    dmdrvi_dev_num_t num;
    memset(&num, 0, sizeof(num));
    num.flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_MINOR;
    num.major = ctx->config.major;
    num.minor = DMSDIO_CARD_MINOR;

    if (available && ctx->host_ready && !ctx->card_announced)
    {
        dmdrvi_device_available(ctx, &num);
        ctx->card_announced = true;
    }
    else if (!available && ctx->card_announced)
    {
        dmdrvi_device_unavailable(ctx, &num);
        ctx->card_announced = false;
    }
}

void dmsdio_card_host_ready(struct dmdrvi_context* ctx)
{
    ctx->host_ready = true;
    if (dmsdio_card_attached(ctx))
    {
        notify(ctx, true);
    }
}

bool dmsdio_card_attached(const struct dmdrvi_context* ctx)
{
    return ctx->card.type != dmsdio_card_type_none;
}

static bool retry_identification(int error)
{
    return error == -ETIMEDOUT || error == -EBADMSG || error == -EIO;
}

static int attach(struct dmdrvi_context* ctx)
{
    dmsdio_card_info_t card;
    int previous_error = ctx->last_error;
    int ret = dmsdio_ident_run(ctx, &card);
    for (uint32_t i = 0; i < ctx->config.retries && retry_identification(ret); i++)
    {
        ret = dmsdio_ident_run(ctx, &card);
    }
    if (ret != 0)
    {
        dmsdio_port_set_power(ctx->config.instance, false);
        /* An empty slot is not an error - keep the last real one. */
        ctx->last_error = (ret == -ENODEV) ? previous_error : ret;
        if (ret != -ENODEV)
        {
            DMOD_LOG_ERROR("dmsdio%u: card identification failed (%d)\n",
                           (unsigned)ctx->config.major, ret);
        }
        return ret;
    }
    ctx->generation++;
    card.generation = ctx->generation;
    ctx->card = card;
    ctx->last_error = 0;
    DMOD_LOG_INFO("dmsdio%u: %s card, %lu blocks, %u-bit, %u Hz%s\n",
                  (unsigned)ctx->config.major, type_name(card.type),
                  (unsigned long)card.block_count, (unsigned)card.bus_width,
                  (unsigned)card.clock_hz, card.high_speed ? " (High Speed)" : "");
    notify(ctx, true);
    return 0;
}

void dmsdio_card_detach(struct dmdrvi_context* ctx)
{
    if (!dmsdio_card_attached(ctx))
    {
        return;
    }
    memset(&ctx->card, 0, sizeof(ctx->card));
    ctx->generation++;
    dmsdio_port_set_power(ctx->config.instance, false);
    DMOD_LOG_INFO("dmsdio%u: card removed\n", (unsigned)ctx->config.major);
    notify(ctx, false);
}

int dmsdio_card_lost(struct dmdrvi_context* ctx, int error)
{
    dmsdio_card_detach(ctx);
    ctx->last_error = error;
    return error;
}

/* A card that still answers CMD13 with our RCA is the same card. */
static int verify(struct dmdrvi_context* ctx)
{
    uint32_t status = 0;
    int ret = dmsdio_cmd_status(ctx, &status);
    if (ret != 0)
    {
        ret = dmsdio_cmd_status(ctx, &status);
    }
    if (ret != 0)
    {
        dmsdio_card_lost(ctx, -ENODEV);
        return -ENODEV;
    }
    return 0;
}

int dmsdio_card_scan(struct dmdrvi_context* ctx)
{
    bool present = true;
    int cd = dmsdio_detect_read_cd(ctx, &present);
    if (cd == 0 && !present)
    {
        dmsdio_card_detach(ctx);
        ctx->scan_count++;
        return -ENODEV;
    }

    bool was_attached = dmsdio_card_attached(ctx);
    int ret = was_attached ? verify(ctx) : attach(ctx);
    if (ret == -ENODEV && was_attached && cd == 0)
    {
        /* Card detect says present but the old card vanished: a new one? */
        ret = attach(ctx);
    }
    ctx->scan_count++;
    return ret;
}
