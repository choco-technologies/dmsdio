#ifndef DMGPIO_TYPES_H
#define DMGPIO_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "dmdrvi_ioctl.h"

/**
 * @brief GPIO port index type (0=GPIOA, 1=GPIOB, ...)
 */
typedef uint8_t dmgpio_port_t;

/**
 * @brief GPIO pin number type (0-15)
 */
typedef uint8_t dmgpio_pin_t;

/**
 * @brief Bitmask of pins within a single port (bit N = pin N)
 */
typedef uint16_t dmgpio_pins_mask_t;

/**
 * @brief Protection for special pins (e.g. JTAG, NMI)
 */
typedef enum
{
    dmgpio_protection_dont_unlock_protected_pins,   /**< Do not configure special pins */
    dmgpio_protection_unlock_protected_pins         /**< Configure special pins */
} dmgpio_protection_t;

/**
 * @brief GPIO output speed
 */
typedef enum
{
    dmgpio_speed_default = 0,       /**< Default speed (not changed) */
    dmgpio_speed_minimum,           /**< Minimum speed */
    dmgpio_speed_medium,            /**< Medium speed */
    dmgpio_speed_maximum,           /**< Maximum speed */
    dmgpio_speed_number_of_elements /**< Number of elements */
} dmgpio_speed_t;

/**
 * @brief GPIO output current
 */
typedef enum
{
    dmgpio_current_default = 0,     /**< Default current (not changed) */
    dmgpio_current_minimum,         /**< Minimum current */
    dmgpio_current_medium,          /**< Medium current */
    dmgpio_current_maximum          /**< Maximum current */
} dmgpio_current_t;

/**
 * @brief GPIO pin mode
 */
typedef enum
{
    dmgpio_mode_default = 0,        /**< Default mode (not changed) */
    dmgpio_mode_input,              /**< Input mode */
    dmgpio_mode_output,             /**< Output mode */
    dmgpio_mode_alternate           /**< Alternate function mode */
} dmgpio_mode_t;

/**
 * @brief GPIO pull resistor configuration
 */
typedef enum
{
    dmgpio_pull_default = 0,        /**< Default pull (not changed) */
    dmgpio_pull_up,                 /**< Pull-up resistor */
    dmgpio_pull_down                /**< Pull-down resistor */
} dmgpio_pull_t;

/**
 * @brief GPIO output circuit type
 */
typedef enum
{
    dmgpio_output_circuit_default = 0,  /**< Default (not changed) */
    dmgpio_output_circuit_open_drain,   /**< Open-drain output */
    dmgpio_output_circuit_push_pull     /**< Push-pull output */
} dmgpio_output_circuit_t;

/**
 * @brief GPIO interrupt trigger source
 */
typedef enum
{
    dmgpio_int_trigger_default      = 0,                                                            /**< Default (not changed) */
    dmgpio_int_trigger_off          = 0,                                                            /**< Interrupts disabled */
    dmgpio_int_trigger_rising_edge  = (1 << 0),                                                     /**< Rising edge */
    dmgpio_int_trigger_falling_edge = (1 << 1),                                                     /**< Falling edge */
    dmgpio_int_trigger_both_edges   = dmgpio_int_trigger_rising_edge | dmgpio_int_trigger_falling_edge,  /**< Both edges */
    dmgpio_int_trigger_high_level   = (1 << 2),                                                     /**< High level */
    dmgpio_int_trigger_low_level    = (1 << 3),                                                     /**< Low level */
    dmgpio_int_trigger_both_levels  = dmgpio_int_trigger_high_level | dmgpio_int_trigger_low_level  /**< Both levels */
} dmgpio_int_trigger_t;

/**
 * @brief GPIO pins state (all low / all high)
 */
typedef enum
{
    dmgpio_pins_state_all_low  = 0,     /**< All selected pins set to low */
    dmgpio_pins_state_all_high          /**< All selected pins set to high */
} dmgpio_pins_state_t;

/**
 * @brief IOCTL commands for DMGPIO device
 *
 * Numbered from DMDRVI_IOCTL_CUSTOM_BASE, the range dmdrvi reserves for
 * driver-specific commands: below it are the standard commands (network
 * 0x01.., block 0x100.., monitor 0x200..) that dmdevfs and other generic
 * code may send to any node, and which dmgpio must answer with -ENOTTY.
 */
typedef enum
{
    dmgpio_ioctl_cmd_toggle_pins = DMDRVI_IOCTL_CUSTOM_BASE,    /**< Toggle pins state */
    dmgpio_ioctl_cmd_set_pins_state,            /**< Set new pins state */
    dmgpio_ioctl_cmd_get_high_pins_state,       /**< Read pins that are in high state */
    dmgpio_ioctl_cmd_get_low_pins_state,        /**< Read pins that are in low state */
    dmgpio_ioctl_cmd_set_interrupt_handler      /**< Add an interrupt handler; arg = dmgpio_interrupt_handler_t* */
} dmgpio_ioctl_cmd_t;

/**
 * @brief Opaque driver context type (forward declaration)
 *
 * The concrete definition is private to the dmgpio driver.
 */
struct dmdrvi_context;
typedef struct dmdrvi_context *dmdrvi_context_t;

/**
 * @brief Parameters passed to a dmhaman-registered interrupt handler.
 *
 * Packed into a single struct so it fits the dmhaman handler API
 * (void *parameters) and the caller does not need to track whether
 * the module that registered the handler is still loaded.
 */
typedef struct
{
    dmgpio_port_t      port;   /**< Port on which the interrupt occurred */
    dmgpio_pins_mask_t pins;   /**< Bitmask of pins that caused the interrupt */
    dmgpio_pins_mask_t state;  /**< Current pin state bitmask (bit N high = pin N is high) */
} dmgpio_interrupt_params_t;

/**
 * @brief GPIO interrupt handler function type
 *
 * Called when an interrupt occurs on a GPIO pin.
 *
 * @param context   Context of the driver instance
 * @param port      Port on which the interrupt occurred
 * @param pins      Bitmask of pins that caused the interrupt
 * @param state     Current pin state bitmask (bit N high = pin N is currently high)
 */
typedef void (*dmgpio_interrupt_handler_t)(dmdrvi_context_t context, dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_mask_t state);

/**
 * @brief GPIO port interrupt handler function type
 *
 * Called by the port layer when an interrupt occurs on a GPIO pin.
 * Registered by the dmgpio driver in the dmgpio_port layer.
 *
 * @param user_ptr  User pointer supplied at registration time (e.g. driver context)
 * @param port      Port on which the interrupt occurred
 * @param pins      Bitmask of pins that caused the interrupt
 * @param state     Current pin state bitmask (bit N high = pin N is currently high)
 */
typedef void (*dmgpio_port_interrupt_handler_t)(void *user_ptr, dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_mask_t state);

#endif /* DMGPIO_TYPES_H */
