/**
 * @file port.c
 * @brief dmsdio_port for the x86_64 host: a simulated SD card.
 *
 * There is no SD host controller on a CI runner, so this port answers the
 * core module's commands with a command-level SD card model (see
 * mock_card.c). It implements the regular port API plus the test-only
 * control API from dmsdio_mock.h (card insertion/removal, fault injection,
 * counters). Nothing here decides protocol behavior - it only reacts to the
 * commands the core sends, the way a real card would.
 */
#define DMOD_ENABLE_REGISTRATION ON
#include "dmsdio_mock.h"
#include "mock_card.h"
#include "dmod.h"
#include <errno.h>
#include <string.h>

#define MOCK_MAX_INSTANCES  2u

static mock_host_t g_hosts[MOCK_MAX_INSTANCES];

static mock_host_t* get_host(dmsdio_instance_t instance)
{
    if (instance < 1 || instance > MOCK_MAX_INSTANCES)
    {
        return NULL;
    }
    return &g_hosts[instance - 1];
}

static mock_host_t* get_ready_host(dmsdio_instance_t instance)
{
    mock_host_t* host = get_host(instance);
    return (host != NULL && host->initialized) ? host : NULL;
}

int dmod_init(const Dmod_Config_t *Config)
{
    (void)Config;
    memset(g_hosts, 0, sizeof(g_hosts));
    for (unsigned i = 0; i < MOCK_MAX_INSTANCES; i++)
    {
        g_hosts[i].remove_after = -1;
    }
    return 0;
}

int dmod_deinit(void)
{
    for (unsigned i = 0; i < MOCK_MAX_INSTANCES; i++)
    {
        mock_store_clear(&g_hosts[i]);
    }
    return 0;
}

/* ---- port API ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _host_init, ( dmsdio_instance_t instance ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -ENODEV;
    }
    if (host->initialized)
    {
        return -EBUSY;
    }
    host->initialized = true;
    host->host_width  = dmsdio_bus_width_1bit;
    host->clock_hz    = 0;
    host->powered     = false;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _host_deinit, ( dmsdio_instance_t instance ))
{
    mock_host_t* host = get_ready_host(instance);
    if (host == NULL)
    {
        return -ENODEV;
    }
    host->initialized = false;
    host->powered     = false;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_power, ( dmsdio_instance_t instance, bool on ))
{
    mock_host_t* host = get_ready_host(instance);
    if (host == NULL)
    {
        return -ENODEV;
    }
    if (!on || !host->powered)
    {
        mock_card_reset(host);      /* power loss resets the card */
    }
    host->powered = on;
    host->stats.powered = on;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_clock, ( dmsdio_instance_t instance, uint32_t max_hz, uint32_t* actual_hz ))
{
    mock_host_t* host = get_ready_host(instance);
    if (host == NULL || max_hz == 0)
    {
        return (host == NULL) ? -ENODEV : -EINVAL;
    }
    /* Integer divider from a 48 MHz source, like SDMMC with CLK48. */
    uint32_t divider = (MOCK_SOURCE_CLOCK_HZ + max_hz - 1u) / max_hz;
    host->clock_hz = MOCK_SOURCE_CLOCK_HZ / divider;
    host->stats.clock_hz = host->clock_hz;
    if (host->clock_hz > host->stats.max_clock_hz)
    {
        host->stats.max_clock_hz = host->clock_hz;
    }
    if (actual_hz != NULL)
    {
        *actual_hz = host->clock_hz;
    }
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ))
{
    mock_host_t* host = get_ready_host(instance);
    if (host == NULL)
    {
        return -ENODEV;
    }
    if (width != dmsdio_bus_width_1bit && width != dmsdio_bus_width_4bit)
    {
        return -EINVAL;
    }
    host->host_width = width;
    host->stats.bus_width = width;
    return 0;
}

static bool fault_matches(const mock_host_t* host, const dmsdio_command_t* cmd, const dmsdio_data_t* data)
{
    if (host->fault == dmsdio_mock_fault_none || host->fault_count == 0 || host->app_cmd)
    {
        return false;       /* never split an APP_CMD pair from its command */
    }
    bool index_ok = (host->fault_cmd == DMSDIO_MOCK_ANY_COMMAND) ? (cmd->index != 55)
                                                                : (host->fault_cmd == cmd->index);
    bool data_fault = (host->fault == dmsdio_mock_fault_data_crc ||
                       host->fault == dmsdio_mock_fault_data_timeout);
    return index_ok && (!data_fault || data != NULL);
}

static dmsdio_status_t execute_with_fault(mock_host_t* host, const dmsdio_command_t* cmd,
                                          const dmsdio_data_t* data, dmsdio_response_t* resp)
{
    dmsdio_mock_fault_t fault = host->fault;
    if (host->fault_count != DMSDIO_MOCK_FOREVER)
    {
        host->fault_count--;
    }
    host->stats.faults_injected++;
    switch (fault)
    {
        case dmsdio_mock_fault_cmd_timeout:
            memset(resp, 0, sizeof(*resp));
            return dmsdio_status_cmd_timeout;
        case dmsdio_mock_fault_cmd_crc:
            memset(resp, 0, sizeof(*resp));
            return dmsdio_status_cmd_crc;
        case dmsdio_mock_fault_bad_index:
        {
            dmsdio_status_t st = mock_card_execute(host, cmd, data, resp);
            resp->index = (uint8_t)((cmd->index + 1u) & 0x3Fu);
            return st;
        }
        default:
            host->data_fault = fault;
            return mock_card_execute(host, cmd, data, resp);
    }
}

/* ---- buffers ---- */

static bool in_port_buffer(const mock_host_t* host, const void* buffer, size_t length)
{
    const uint8_t* start = buffer;
    return host->port_buffer != NULL && start >= host->port_buffer &&
           length <= host->port_buffer_size - (size_t)(start - host->port_buffer);
}

static bool buffer_is_direct(const mock_host_t* host, const void* buffer, size_t length,
                             dmsdio_direction_t direction)
{
    bool aligned = ((uintptr_t)buffer % DMSDIO_TRANSFER_ALIGNMENT) == 0;
    bool fast    = direction == dmsdio_direction_read || !host->slow_writes ||
                   in_port_buffer(host, buffer, length);
    return buffer != NULL && aligned && fast;
}

dmod_dmsdio_port_api_declaration(1.0, bool, _buffer_is_direct,
    ( dmsdio_instance_t instance, const void* buffer, size_t length, dmsdio_direction_t direction ))
{
    mock_host_t* host = get_host(instance);
    return host != NULL && buffer_is_direct(host, buffer, length, direction);
}

dmod_dmsdio_port_api_declaration(1.0, void*, _buffer_alloc, ( dmsdio_instance_t instance, size_t size ))
{
    mock_host_t* host = get_host(instance);
    void* buffer = (host != NULL) ? Dmod_AlignedMalloc(size, DMSDIO_TRANSFER_ALIGNMENT) : NULL;
    if (buffer != NULL)
    {
        host->port_buffer      = buffer;
        host->port_buffer_size = size;
    }
    return buffer;
}

dmod_dmsdio_port_api_declaration(1.0, void, _buffer_free, ( dmsdio_instance_t instance, void* buffer ))
{
    mock_host_t* host = get_host(instance);
    if (host != NULL && buffer != NULL && buffer == (void*)host->port_buffer)
    {
        host->port_buffer      = NULL;
        host->port_buffer_size = 0;
    }
    Dmod_Free(buffer);
}

dmod_dmsdio_port_api_declaration(1.0, dmsdio_status_t, _execute,
    ( dmsdio_instance_t instance, const dmsdio_command_t* command,
      const dmsdio_data_t* data, dmsdio_response_t* response ))
{
    dmsdio_response_t scratch;
    mock_host_t* host = get_ready_host(instance);
    if (host == NULL || command == NULL || command->index > 63)
    {
        return dmsdio_status_invalid;
    }
    response = (response != NULL) ? response : &scratch;
    if (data != NULL && data->direction == dmsdio_direction_write &&
        !buffer_is_direct(host, data->buffer, (size_t)data->block_size * data->block_count, data->direction))
    {
        host->stats.indirect_writes++;
    }
    dmsdio_status_t st = fault_matches(host, command, data)
                       ? execute_with_fault(host, command, data, response)
                       : mock_card_execute(host, command, data, response);
    host->data_fault = dmsdio_mock_fault_none;
    return st;
}

dmod_dmsdio_port_api_declaration(1.0, void, _abort, ( dmsdio_instance_t instance ))
{
    mock_host_t* host = get_ready_host(instance);
    if (host != NULL)
    {
        host->stats.aborts++;
    }
}

/* ---- mock control API ---- */

dmod_dmsdio_port_api_declaration(1.0, int, _mock_insert, ( dmsdio_instance_t instance, dmsdio_card_type_t type ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL || mock_card_capacity_blocks(type) == 0)
    {
        return -EINVAL;
    }
    mock_store_clear(host);
    host->type = type;
    host->inserted = true;
    host->write_protect = false;
    host->refuse_high_speed = false;
    host->remove_after = -1;
    mock_card_reset(host);
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_remove, ( dmsdio_instance_t instance ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->inserted = false;
    host->remove_after = -1;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_remove_after_blocks, ( dmsdio_instance_t instance, uint32_t blocks ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->remove_after = blocks;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_inject_fault,
    ( dmsdio_instance_t instance, dmsdio_mock_fault_t fault, uint8_t cmd_index, uint32_t count ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->fault       = fault;
    host->fault_cmd   = cmd_index;
    host->fault_count = (fault == dmsdio_mock_fault_none) ? 0u : count;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_set_write_protect, ( dmsdio_instance_t instance, bool enabled ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->write_protect = enabled;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_refuse_high_speed, ( dmsdio_instance_t instance, bool refuse ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->refuse_high_speed = refuse;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_set_slow_writes, ( dmsdio_instance_t instance, bool enabled ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL)
    {
        return -EINVAL;
    }
    host->slow_writes = enabled;
    return 0;
}

dmod_dmsdio_port_api_declaration(1.0, int, _mock_get_stats, ( dmsdio_instance_t instance, dmsdio_mock_stats_t* stats, bool reset ))
{
    mock_host_t* host = get_host(instance);
    if (host == NULL || stats == NULL)
    {
        return -EINVAL;
    }
    *stats = host->stats;
    if (reset)
    {
        memset(&host->stats, 0, sizeof(host->stats));
        host->stats.clock_hz  = host->clock_hz;
        host->stats.bus_width = host->host_width;
        host->stats.powered   = host->powered;
        host->stats.card_bus_4bit   = host->card_4bit;
        host->stats.card_high_speed = host->high_speed;
    }
    return 0;
}
