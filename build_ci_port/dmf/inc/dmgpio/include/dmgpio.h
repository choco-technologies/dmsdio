#ifndef DMGPIO_H
#define DMGPIO_H

#include "dmgpio_defs.h"
#include "dmgpio_types.h"

/**
 * @brief GPIO driver configuration structure
 */
typedef struct
{
    dmgpio_port_t               port;               /**< GPIO port index (0=A, 1=B, ...) */
    dmgpio_pins_mask_t          pins;               /**< GPIO pin mask (bit N = pin N) */
    dmgpio_protection_t         protection;         /**< Protection for special pins */
    dmgpio_speed_t              speed;              /**< Maximum switching speed */
    dmgpio_current_t            current;            /**< Maximum output current */
    dmgpio_mode_t               mode;               /**< Pin direction mode */
    dmgpio_pull_t               pull;               /**< Pull-up/pull-down selection */
    dmgpio_output_circuit_t     output_circuit;     /**< Output circuit type */
    uint8_t                     alternate_function; /**< Alternate function number (0-15) */
    dmgpio_int_trigger_t        interrupt_trigger;  /**< Interrupt trigger source */
    dmgpio_interrupt_handler_t  interrupt_handler;  /**< Interrupt handler (NULL = not used) */
} dmgpio_config_t;

#endif // DMGPIO_H