#include "dmsdio_internal.h"
#include <string.h>

#define DEFAULT_SECTION             "dmsdio"
#define DEFAULT_MAX_CLOCK_HZ        50000000
#define DEFAULT_RETRIES             3
#define DEFAULT_INIT_TIMEOUT_MS     1000
#define DEFAULT_READ_TIMEOUT_MS     250
#define DEFAULT_WRITE_TIMEOUT_MS    500
#define DEFAULT_ERASE_TIMEOUT_MS    3000
#define DEFAULT_MAX_BLOCKS          128
#define DEFAULT_DEBOUNCE_MS         50
#define DEFAULT_POLL_INTERVAL_MS    1000
#define MAX_RETRIES                 16

/*
 * Where the settings live:
 *   1. the section dmdevfs made active for this device (multi-device board
 *      files) - addressed as the NULL section,
 *   2. a [dmsdio] section,
 *   3. the first section with an `instance` key (device-named section).
 */
static const char* find_section(dmini_context_t ini)
{
    if (dmini_has_key(ini, NULL, "instance"))
    {
        return NULL;
    }
    if (dmini_has_section(ini, DEFAULT_SECTION))
    {
        return DEFAULT_SECTION;
    }
    int count = dmini_section_count(ini);
    for (int i = 0; i < count; i++)
    {
        const char* name = dmini_section_name(ini, i);
        if (name != NULL && dmini_has_key(ini, name, "instance"))
        {
            return name;
        }
    }
    return DEFAULT_SECTION;
}

static const char* label(const char* section)
{
    return (section != NULL) ? section : "active section";
}

static bool read_bool(dmini_context_t ini, const char* section, const char* key, bool def)
{
    const char* value = dmini_get_string(ini, section, key, NULL);
    if (value == NULL)
    {
        return def;
    }
    return strcmp(value, "true") == 0 || strcmp(value, "1") == 0 ||
           strcmp(value, "yes") == 0 || strcmp(value, "on") == 0;
}

static uint32_t read_u32(dmini_context_t ini, const char* section, const char* key, uint32_t def)
{
    int value = dmini_get_int(ini, section, key, (int)def);
    return value < 0 ? def : (uint32_t)value;
}

static int read_timeouts(dmini_context_t ini, const char* section, dmsdio_config_t* c)
{
    c->retries          = read_u32(ini, section, "retries", DEFAULT_RETRIES);
    c->init_timeout_ms  = read_u32(ini, section, "init_timeout_ms", DEFAULT_INIT_TIMEOUT_MS);
    c->read_timeout_ms  = read_u32(ini, section, "read_timeout_ms", DEFAULT_READ_TIMEOUT_MS);
    c->write_timeout_ms = read_u32(ini, section, "write_timeout_ms", DEFAULT_WRITE_TIMEOUT_MS);
    c->erase_timeout_ms = read_u32(ini, section, "erase_timeout_ms", DEFAULT_ERASE_TIMEOUT_MS);
    c->debounce_ms      = read_u32(ini, section, "card_detect_debounce_ms", DEFAULT_DEBOUNCE_MS);
    c->poll_interval_ms = read_u32(ini, section, "poll_interval_ms", DEFAULT_POLL_INTERVAL_MS);

    if (c->retries > MAX_RETRIES || c->init_timeout_ms == 0 || c->read_timeout_ms == 0 ||
        c->write_timeout_ms == 0 || c->erase_timeout_ms == 0)
    {
        DMOD_LOG_ERROR("dmsdio: invalid retry/timeout configuration in [%s]\n", label(section));
        return -EINVAL;
    }
    return 0;
}

static int read_bus(dmini_context_t ini, const char* section, dmsdio_config_t* c)
{
    int instance = dmini_get_int(ini, section, "instance", 1);
    int width    = dmini_get_int(ini, section, "bus_width", 4);

    c->max_clock_hz = read_u32(ini, section, "max_clock_hz", DEFAULT_MAX_CLOCK_HZ);
    c->high_speed   = read_bool(ini, section, "high_speed", true);
    c->max_blocks_per_transfer = read_u32(ini, section, "max_blocks_per_transfer", DEFAULT_MAX_BLOCKS);

    if (instance < 1 || instance > 255 || (width != 1 && width != 4) ||
        c->max_clock_hz < 400000u || c->max_blocks_per_transfer == 0)
    {
        DMOD_LOG_ERROR("dmsdio: invalid instance/bus configuration in [%s]\n", label(section));
        return -EINVAL;
    }
    c->instance      = (dmsdio_instance_t)instance;
    c->max_bus_width = (width == 4) ? dmsdio_bus_width_4bit : dmsdio_bus_width_1bit;

    int major = dmini_get_int(ini, section, "major", instance - 1);
    if (major < 0 || major > 255)
    {
        DMOD_LOG_ERROR("dmsdio: invalid major number in [%s]\n", label(section));
        return -EINVAL;
    }
    c->major = (uint8_t)major;
    return 0;
}

static int read_card_detect(dmini_context_t ini, const char* section, dmsdio_config_t* c)
{
    const char* level   = dmini_get_string(ini, section, "card_detect_active_level", "low");
    const char* handler = dmini_get_string(ini, section, "card_detect_handler", NULL);

    if (strcmp(level, "low") != 0 && strcmp(level, "high") != 0)
    {
        DMOD_LOG_ERROR("dmsdio: card_detect_active_level must be low or high\n");
        return -EINVAL;
    }
    c->cd_active_high  = strcmp(level, "high") == 0;
    c->cd_handler_name = NULL;
    if (handler != NULL && handler[0] != '\0')
    {
        c->cd_handler_name = Dmod_StrDup(handler);
        if (c->cd_handler_name == NULL)
        {
            return -ENOMEM;
        }
    }
    return 0;
}

int dmsdio_config_read(dmini_context_t ini, dmsdio_config_t* config)
{
    memset(config, 0, sizeof(*config));
    const char* section = find_section(ini);

    int ret = read_bus(ini, section, config);
    if (ret == 0)
    {
        ret = read_timeouts(ini, section, config);
    }
    if (ret == 0)
    {
        ret = read_card_detect(ini, section, config);
    }
    return ret;
}

void dmsdio_config_release(dmsdio_config_t* config)
{
    Dmod_Free(config->cd_handler_name);
    config->cd_handler_name = NULL;
}
