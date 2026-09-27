#ifndef DMSDIO_MOCK_CONTROL_H
#define DMSDIO_MOCK_CONTROL_H

#include "dmsdio_port_defs.h"
#include "dmsdio_types.h"

/**
 * @brief Test-only control surface for the x86_64 mock dmsdio_port
 *
 * Declared under the same "dmsdio_port" module identity as dmsdio_port.h
 * (same dmod_dmsdio_port_api(...) macro) so it patches/registers exactly
 * the same way - real hardware ports never see this header, only
 * tests/dmsdio_test.c and this mock's own port.c do.
 */

/**
 * @brief Reset the mock to a fresh "just powered on" card of the given
 *        shape and clear every injected fault
 *
 * capacity_blocks only needs to be *reported* accurately via CSD - the
 * mock's backing store is a small fixed-size buffer (see port.c), so tests
 * exercising real data must stay within it regardless of what capacity
 * they configure here.
 */
dmod_dmsdio_port_api(1.0, void, _mock_reset, ( dmsdio_card_type_t card_type, uint64_t capacity_blocks ) );

/**
 * @brief Simulate the card being physically absent - every command and
 *        data-phase call starts failing with dmsdio_error_removed until
 *        the next _mock_reset()
 */
dmod_dmsdio_port_api(1.0, void, _mock_set_removed, ( bool removed ) );

/**
 * @brief Whether the mock card should accept the CMD6 High Speed switch
 *        (mock_reset() defaults this to true)
 */
dmod_dmsdio_port_api(1.0, void, _mock_set_high_speed_supported, ( bool supported ) );

/**
 * @brief Whether the mock card should report SCR SD_BUS_WIDTHS 4-bit
 *        support (mock_reset() defaults this to true)
 */
dmod_dmsdio_port_api(1.0, void, _mock_set_4bit_supported, ( bool supported ) );

/**
 * @brief Make the next `count` dmsdio_port_send_command()/_read_blocks()/
 *        _write_blocks() calls fail with `error`, then resume normal
 *        behaviour - lets a test exercise a transient fault that resolves
 *        within dmsdio.c's own retry budget, or one that exhausts it,
 *        with the same knob (compare `count` against the config's
 *        max_retries the test itself constructed).
 */
dmod_dmsdio_port_api(1.0, void, _mock_inject_error, ( dmsdio_error_t error, uint32_t count ) );

/**
 * @brief Direct access to the mock's backing store, for a test to seed
 *        data before a read or verify it after a write without going
 *        through the driver under test
 */
dmod_dmsdio_port_api(1.0, uint8_t*, _mock_get_backing_store, ( void ) );

#endif /* DMSDIO_MOCK_CONTROL_H */
