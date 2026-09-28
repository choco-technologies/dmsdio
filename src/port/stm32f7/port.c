/**
 * @file port.c
 * @brief dmsdio_port for STM32F7 (SDMMC) - not implemented yet.
 *
 * The SDMMC register-level implementation (command/response path, IDMA
 * data path, interrupts, clock divider from dmclk_port's sdio domain,
 * pin configuration) is delivered by choco-technologies/dmod-ecosystem#12.
 * Until then every entry point reports -ENOTSUP / dmsdio_status_not_supported,
 * so dmsdio's dmdrvi_create() fails loudly instead of pretending a card is
 * reachable.
 */
#define DMOD_ENABLE_REGISTRATION    ON
#include "dmsdio_port.h"
#include "dmod.h"
#include <errno.h>

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    return 0;
}

int dmod_deinit(void)
{
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_init, ( dmsdio_instance_t instance ))
{
    DMOD_LOG_ERROR("dmsdio_port: SDMMC%u host is not supported by this port yet\n", (unsigned)instance);
    return -ENOTSUP;
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_deinit, ( dmsdio_instance_t instance ))
{
    (void)instance;
    return -ENOTSUP;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_power, ( dmsdio_instance_t instance, bool on ))
{
    (void)instance;
    (void)on;
    return -ENOTSUP;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_clock, ( dmsdio_instance_t instance, uint32_t max_hz, uint32_t* actual_hz ))
{
    (void)instance;
    (void)max_hz;
    (void)actual_hz;
    return -ENOTSUP;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    (void)instance;
    (void)width;
    return -ENOTSUP;
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_status_t, _execute,
    ( dmsdio_instance_t instance, const dmsdio_command_t* command,
      const dmsdio_data_t* data, dmsdio_response_t* response ))
{
    (void)instance;
    (void)command;
    (void)data;
    (void)response;
    return dmsdio_status_not_supported;
}

dmod_dmsdio_port_api_declaration(1.0, void, _abort, ( dmsdio_instance_t instance ))
{
    (void)instance;
}
