#ifndef DMSDIO_H
#define DMSDIO_H

#include "dmsdio_defs.h"
#include "dmsdio_types.h"

/**
 * @brief SD host controller configuration
 */
typedef struct
{
    dmsdio_instance_t   instance;          /**< Host controller instance number (0-based) */
    dmsdio_bus_width_t   max_bus_width;     /**< Never negotiate wider than this, even if the card and board wiring support it */
    bool                 allow_high_speed;  /**< Attempt CMD6 High Speed switch after identification */
    uint32_t             command_timeout_ms;/**< Per-command response deadline */
    uint32_t             data_timeout_ms;   /**< Per-block-transfer deadline */
    uint32_t             max_retries;       /**< Retries for a CRC/timeout error before giving up */
} dmsdio_config_t;

/**
 * @brief Validate a configuration structure without touching hardware.
 *
 * Does not require the dmsdio_port module to be loaded, so it is safe to
 * call from any context, including off-target unit tests.
 *
 * @return true if the configuration is self-consistent, false otherwise.
 */
dmod_dmsdio_api(1.0, bool, _validate_config, ( const dmsdio_config_t *config ));

#endif // DMSDIO_H
