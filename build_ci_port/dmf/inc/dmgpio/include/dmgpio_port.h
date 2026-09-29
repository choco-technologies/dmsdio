#ifndef DMGPIO_PORT_H
#define DMGPIO_PORT_H

#include "dmod_types.h"
#include "dmgpio_port.h"
#include "dmgpio_port_defs.h"
#include "dmgpio_types.h"

/* --- Interrupt handlers --- */

dmod_dmgpio_port_api(1.0, int,  _add_interrupt_handler,
    ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_port_interrupt_handler_t handler, void *user_ptr ));
dmod_dmgpio_port_api(1.0, int,  _remove_interrupt_handler,
    ( dmgpio_port_t port, void *user_ptr ));

/* --- Configuration session --- */

dmod_dmgpio_port_api(1.0, int,  _begin_configuration,  ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, int,  _finish_configuration, ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));

/* --- Clock / power --- */

dmod_dmgpio_port_api(1.0, int,  _set_power, ( dmgpio_port_t port, int power_on ));

/* --- Pin protection --- */

dmod_dmgpio_port_api(1.0, bool, _are_pins_protected, ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, int,  _unlock_protection,  ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_protection_t protection ));
dmod_dmgpio_port_api(1.0, int,  _lock_protection,    ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));

/* --- Configuration parameters (require begin/finish_configuration) --- */

dmod_dmgpio_port_api(1.0, int,  _set_speed,             ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_speed_t speed ));
dmod_dmgpio_port_api(1.0, int,  _read_speed,            ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_speed_t *out_speed ));
dmod_dmgpio_port_api(1.0, int,  _set_current,           ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_current_t current ));
dmod_dmgpio_port_api(1.0, int,  _read_current,          ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_current_t *out_current ));
dmod_dmgpio_port_api(1.0, int,  _set_mode,              ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_mode_t mode ));
dmod_dmgpio_port_api(1.0, int,  _read_mode,             ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_mode_t *out_mode ));
dmod_dmgpio_port_api(1.0, int,  _set_pull,              ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pull_t pull ));
dmod_dmgpio_port_api(1.0, int,  _read_pull,             ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pull_t *out_pull ));
dmod_dmgpio_port_api(1.0, int,  _set_output_circuit,    ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_output_circuit_t oc ));
dmod_dmgpio_port_api(1.0, int,  _read_output_circuit,   ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_output_circuit_t *out_oc ));
dmod_dmgpio_port_api(1.0, int,  _set_alternate_function, ( dmgpio_port_t port, dmgpio_pins_mask_t pins, uint8_t af ));
dmod_dmgpio_port_api(1.0, int,  _read_alternate_function,( dmgpio_port_t port, dmgpio_pins_mask_t pins, uint8_t *out_af ));
dmod_dmgpio_port_api(1.0, int,  _set_interrupt_trigger, ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_int_trigger_t trigger ));
dmod_dmgpio_port_api(1.0, int,  _read_interrupt_trigger,( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_int_trigger_t *out_trigger ));

/* --- Pin usage tracking --- */

dmod_dmgpio_port_api(1.0, int,  _set_pins_used,    ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, int,  _set_pins_unused,  ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, int,  _check_is_pin_used,( dmgpio_port_t port, dmgpio_pins_mask_t pins, int *out_used ));

/* --- Data read/write --- */

dmod_dmgpio_port_api(1.0, int,  _write_data,          ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_mask_t data ));
dmod_dmgpio_port_api(1.0, int,  _read_data,           ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_mask_t *out_data ));

/* --- Pin state operations (no argument checking, must ensure correct pins) --- */

dmod_dmgpio_port_api(1.0, dmgpio_pins_mask_t, _get_high_state_pins, ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, dmgpio_pins_mask_t, _get_low_state_pins,  ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));
dmod_dmgpio_port_api(1.0, void, _set_pins_state,      ( dmgpio_port_t port, dmgpio_pins_mask_t pins, dmgpio_pins_state_t state ));
dmod_dmgpio_port_api(1.0, void, _toggle_pins_state,   ( dmgpio_port_t port, dmgpio_pins_mask_t pins ));

#endif // DMGPIO_PORT_H