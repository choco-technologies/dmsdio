#ifndef DMSDIO_PORT_H
#define DMSDIO_PORT_H

#include "dmod_types.h"
#include "dmsdio_port_defs.h"
#include "dmsdio_types.h"

/**
 * @file dmsdio_port.h
 * @brief Hardware port of the SD host controller.
 *
 * Every function here is a protocol-free hardware primitive. The port knows
 * how to power the bus, program the clock divider and bus width, clock one
 * command out, capture the requested response shape and run the attached
 * data phase. It never decides which command to send, never interprets a
 * response or card register, and never converts byte offsets to block
 * addresses - all of that lives in the core dmsdio module.
 *
 * Whether a port moves data with an internal IDMA engine, a dmdma lease or
 * the FIFO is an implementation detail invisible from here. All calls are
 * made from thread context and serialized per instance by the core module.
 */

/* --- Lifecycle --- */

/**
 * @brief Acquire and reset the controller (clocks, pins, DMA, IRQ).
 *
 * @return 0 on success or a negative errno value; a failure to acquire any
 * required resource must be reported, never silently degraded.
 */
dmod_dmsdio_port_api(1.0, int, _host_init,   ( dmsdio_instance_t instance ));

/** @brief Release everything acquired by _host_init(). */
dmod_dmsdio_port_api(1.0, int, _host_deinit, ( dmsdio_instance_t instance ));

/* --- Bus configuration --- */

/**
 * @brief Switch card power (and the bus clock output) on or off.
 *
 * After power on the port must keep the clock running so the core can
 * provide the mandatory 74 initialization clocks by waiting.
 */
dmod_dmsdio_port_api(1.0, int, _set_power, ( dmsdio_instance_t instance, bool on ));

/**
 * @brief Program the highest bus clock not exceeding max_hz.
 *
 * @param actual_hz Receives the programmed frequency (may be NULL)
 *
 * @return 0 on success, negative errno when no divider can satisfy the
 * request (e.g. the source clock is unknown).
 */
dmod_dmsdio_port_api(1.0, int, _set_clock, ( dmsdio_instance_t instance, uint32_t max_hz, uint32_t* actual_hz ));

/** @brief Select the host side data bus width. */
dmod_dmsdio_port_api(1.0, int, _set_bus_width, ( dmsdio_instance_t instance, dmsdio_bus_width_t width ));

/* --- Transport --- */

/**
 * @brief Execute one command and, if data != NULL, its data phase.
 *
 * The data path must be armed before the command is sent. The call returns
 * once the response was captured and the data phase (if any) completed or
 * failed; completion and errors must be interrupt driven and bounded by
 * the command timeout and data->timeout_ms respectively.
 *
 * CRC checking applies to every response type except
 * dmsdio_response_short_no_crc. For dmsdio_response_short_busy the port may
 * return before DAT0 is released - the core polls card status itself.
 *
 * @param response Receives the captured response (may be NULL when
 * command->response is dmsdio_response_none)
 */
dmod_dmsdio_port_api(1.0, dmsdio_status_t, _execute,
    ( dmsdio_instance_t instance, const dmsdio_command_t* command,
      const dmsdio_data_t* data, dmsdio_response_t* response ));

/**
 * @brief Abort an interrupted data phase and return the controller to idle.
 *
 * Called by the core after any failed _execute() before recovery commands
 * are sent. Must not touch the card - the core issues STOP itself.
 */
dmod_dmsdio_port_api(1.0, void, _abort, ( dmsdio_instance_t instance ));

#endif // DMSDIO_PORT_H
