#include "dmsdio_internal.h"

/*
 * The transport of the commands. The core (commands, identification, transfers) speaks the protocol of the SD bus; below it is either the
 * port of the host controller (SDIO/SDMMC: dmsdio_port.h) or, with bus=spi, dmsdio_spi.c, which carries the same commands over an SPI bus.
 */

int dmsdio_host_set_power(struct dmdrvi_context* ctx, bool on)
{
    return ctx->config.spi_mode ? dmsdio_spi_set_power(ctx, on) : dmsdio_port_set_power(ctx->config.instance, on);
}

int dmsdio_host_set_bus_width(struct dmdrvi_context* ctx, dmsdio_bus_width_t width)
{
    if (ctx->config.spi_mode)
    {
        return width == dmsdio_bus_width_1bit ? 0 : -ENOTSUP;
    }
    return dmsdio_port_set_bus_width(ctx->config.instance, width);
}

int dmsdio_host_set_clock(struct dmdrvi_context* ctx, uint32_t max_hz, uint32_t* actual_hz)
{
    return ctx->config.spi_mode ? dmsdio_spi_set_clock(ctx, max_hz, actual_hz)
                                : dmsdio_port_set_clock(ctx->config.instance, max_hz, actual_hz);
}

dmsdio_status_t dmsdio_host_execute(struct dmdrvi_context* ctx, const dmsdio_command_t* cmd,
                                    const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    return ctx->config.spi_mode ? dmsdio_spi_execute(ctx, cmd, data, resp)
                                : dmsdio_port_execute(ctx->config.instance, cmd, data, resp);
}

void dmsdio_host_abort(struct dmdrvi_context* ctx)
{
    if (!ctx->config.spi_mode)
    {
        dmsdio_port_abort(ctx->config.instance);
    }
    /* On SPI every command ends with the chip select released: nothing is left running */
}

void* dmsdio_host_buffer_alloc(struct dmdrvi_context* ctx, size_t size)
{
    return ctx->config.spi_mode ? Dmod_AlignedMalloc(size, 32u) : dmsdio_port_buffer_alloc(ctx->config.instance, size);
}

void dmsdio_host_buffer_free(struct dmdrvi_context* ctx, void* buffer)
{
    if (ctx->config.spi_mode)
    {
        Dmod_Free(buffer);
        return;
    }
    dmsdio_port_buffer_free(ctx->config.instance, buffer);
}

bool dmsdio_host_buffer_is_direct(const struct dmdrvi_context* ctx, const void* buffer, size_t length, dmsdio_direction_t direction)
{
    /* dmspi moves any buffer itself (a bounce of its own where the DMA cannot reach it) */
    return ctx->config.spi_mode ? true : dmsdio_port_buffer_is_direct(ctx->config.instance, buffer, length, direction);
}
