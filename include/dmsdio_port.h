#ifndef DMSDIO_PORT_H
#define DMSDIO_PORT_H

#include "dmod_types.h"
#include "dmsdio_port_defs.h"
#include "dmsdio_types.h"

/**
 * @brief Architecture-independent SD host controller port
 *
 * Every function here is a raw hardware primitive - no SD command sequence,
 * register field, or card-state logic belongs in a port implementation
 * (src/port/<arch>/port.c). dmsdio.c owns 100% of the protocol: which
 * commands to send in which order, how to parse OCR/CID/CSD/SCR/card
 * status, retry/timeout policy, and byte<->block address translation. A
 * port only needs to know how to clock a command out, capture whatever
 * response the caller says to expect, and move already-block-aligned data
 * in or out - see dmsdio_response_type_t's own comment for exactly how
 * little a port needs to understand about response *content*.
 *
 * Whether a port uses the host controller's own internal DMA engine (e.g.
 * STM32F7 SDMMC's IDMA) or leases a stream from dmdma (e.g. STM32F4 SDIO,
 * which has no IDMA of its own and needs a real DMA2 stream) is entirely a
 * port implementation detail, invisible from here.
 */

/* --- Lifecycle --- */

dmod_dmsdio_port_api(1.0, int, _init,   ( dmsdio_instance_t instance ) );
dmod_dmsdio_port_api(1.0, int, _deinit, ( dmsdio_instance_t instance ) );

/* --- Bus configuration ---
 *
 * dmsdio.c always calls _set_speed_mode(dmsdio_speed_mode_identification)
 * (<= 400 kHz) before the very first command of a fresh identification
 * sequence, and never raises the bus width above 1-bit until after CMD7
 * (card selected) - matching the mandatory power-up sequencing in the SD
 * Physical Layer spec, section "Bus Protocol".
 */

dmod_dmsdio_port_api(1.0, int, _set_bus_width,  ( dmsdio_instance_t instance, dmsdio_bus_width_t width ) );
dmod_dmsdio_port_api(1.0, int, _set_speed_mode, ( dmsdio_instance_t instance, dmsdio_speed_mode_t mode ) );

/* --- Commands ---
 *
 * Sends command index `cmd_index` (already including the CMD55 prefix for
 * an ACMD, if any - dmsdio.c issues CMD55 as its own, separate
 * _send_command() call) with `argument`, then waits for and captures
 * whichever response shape `response_type` says to expect - dmsdio_response_none
 * returns immediately once the command itself has been clocked out.
 *
 * A response's CRC/index-check bits must be verified by the port
 * (dmsdio_error_command_crc on mismatch) except for dmsdio_response_r3,
 * whose CRC field is defined by the spec to be all 1s and is not checked.
 *
 * dmsdio_response_r1b is captured identically to dmsdio_response_r1 - the
 * port does not need to (and must not try to) wait out the card's DAT0
 * busy signal itself. Whenever dmsdio.c actually needs to know a card has
 * finished an internal operation (post-erase, post-write), it polls
 * CMD13/SEND_STATUS's READY_FOR_DATA bit for that, entirely at the
 * protocol layer - this is deliberate: not every controller exposes a raw
 * DAT0 level to software, and CMD13 polling works identically everywhere.
 */

dmod_dmsdio_port_api(1.0, dmsdio_error_t, _send_command,
    ( dmsdio_instance_t instance, uint8_t cmd_index, uint32_t argument,
      dmsdio_response_type_t response_type, dmsdio_response_t *response ) );

/* --- Data transfer ---
 *
 * dmsdio.c has already issued the matching command via _send_command()
 * before calling either of these - CMD17/18 for a read, CMD24/25 for a
 * write, ACMD51 for an SCR read (block_size=8, block_count=1), ACMD13 for
 * an SD Status read (block_size=64, block_count=1) - and, for the
 * multi-block case, will issue CMD12 itself afterward; these calls only
 * drive the data phase itself. block_size is *not* fixed at
 * DMSDIO_BLOCK_SIZE: SCR and SD Status are transferred as a single
 * narrower block each, same as on the wire. Both block_size and
 * block_count must be >= 1 (validated by dmsdio.c before the call).
 *
 * Neither call waits for the card to finish internally programming what
 * was written - only that the bytes were clocked onto the bus. dmsdio.c
 * polls CMD13/SEND_STATUS itself afterward to know when a write has
 * actually landed (see dmsdio_response_r1b's own comment above).
 */

dmod_dmsdio_port_api(1.0, dmsdio_error_t, _read_blocks,
    ( dmsdio_instance_t instance, void *buffer, uint32_t block_size, uint32_t block_count ) );
dmod_dmsdio_port_api(1.0, dmsdio_error_t, _write_blocks,
    ( dmsdio_instance_t instance, const void *buffer, uint32_t block_size, uint32_t block_count ) );

#endif // DMSDIO_PORT_H
