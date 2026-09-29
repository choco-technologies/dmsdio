#ifndef DMSDIO_H
#define DMSDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmod_types.h"
#include "dmsdio_defs.h"
#include "dmsdio_types.h"

/**
 * @file dmsdio.h
 * @brief Public API of the dmsdio SD memory card driver.
 *
 * The device itself is used through the dmdrvi 2.0 interface (a persistent
 * host node /dev/dmsdioN plus a hot-plugged card node /dev/dmsdioN/0), see
 * docs/api-reference.md. The functions below are pure register decoders,
 * shared with the driver itself, that consumers can use to interpret the
 * raw registers returned by dmsdio_ioctl_cmd_get_card_info.
 */

/**
 * @brief Decode a raw CSD register.
 *
 * @param raw Raw register, most significant word first
 * @param csd Output
 *
 * @return 0 on success, -EINVAL on NULL arguments, -ENOTSUP for an
 * unsupported CSD structure version (e.g. SDUC), -EPROTO for a CSD whose
 * fields are inconsistent.
 */
dmod_dmsdio_api(1.0, int, _decode_csd, ( const uint32_t raw[4], dmsdio_csd_t* csd ));

/**
 * @brief Decode a raw CID register.
 *
 * @return 0 on success, -EINVAL on NULL arguments.
 */
dmod_dmsdio_api(1.0, int, _decode_cid, ( const uint32_t raw[4], dmsdio_cid_t* cid ));

/**
 * @brief Decode the 8-byte SCR as received on the data lines.
 *
 * @return 0 on success, -EINVAL on NULL arguments, -EPROTO when the SCR
 * structure version is unknown or no supported bus width is reported.
 */
dmod_dmsdio_api(1.0, int, _decode_scr, ( const uint8_t raw[8], dmsdio_scr_t* scr ));

/**
 * @brief Decode the 64-byte SD Status as received on the data lines.
 *
 * @return 0 on success, -EINVAL on NULL arguments.
 */
dmod_dmsdio_api(1.0, int, _decode_ssr, ( const uint8_t raw[64], dmsdio_ssr_t* ssr ));

#endif // DMSDIO_H
