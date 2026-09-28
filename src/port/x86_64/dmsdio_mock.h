#ifndef DMSDIO_MOCK_H
#define DMSDIO_MOCK_H

#include "dmsdio_port.h"

/**
 * @file dmsdio_mock.h
 * @brief Control interface of the x86_64 mock SD card (tests only).
 *
 * The x86_64 dmsdio_port does not drive hardware: it simulates an SD
 * memory card at the command level (card state machine, OCR/CID/CSD/SCR/
 * SD Status registers, CMD6 switch status, sparse block storage) so the
 * complete protocol in the core module can be exercised on a CI runner.
 * These functions exist only in the x86_64 port.
 */

/** Maximum number of blocks the mock stores (sparse, written blocks only). */
#define DMSDIO_MOCK_MAX_STORED_BLOCKS   4096u

/** Injectable transport faults. */
typedef enum
{
    dmsdio_mock_fault_none = 0,
    dmsdio_mock_fault_cmd_timeout,      /**< Command gets no response */
    dmsdio_mock_fault_cmd_crc,          /**< Response CRC error */
    dmsdio_mock_fault_data_crc,         /**< Data block CRC error */
    dmsdio_mock_fault_data_timeout,     /**< Data phase times out */
    dmsdio_mock_fault_bad_index,        /**< Response carries a wrong command index */
} dmsdio_mock_fault_t;

/** Use as cmd_index to match any command (except APP_CMD). */
#define DMSDIO_MOCK_ANY_COMMAND     0xFFu

/** Use as count for a fault that never expires. */
#define DMSDIO_MOCK_FOREVER         0xFFFFFFFFu

/** Counters observed by the mock. */
typedef struct
{
    uint32_t    commands[64];       /**< Plain commands by index */
    uint32_t    app_commands[64];   /**< Application commands by index */
    uint64_t    blocks_read;        /**< 512-byte blocks read */
    uint64_t    blocks_written;     /**< 512-byte blocks written */
    uint32_t    aborts;             /**< _abort() calls */
    uint32_t    faults_injected;    /**< Faults delivered */
    uint32_t    clock_hz;           /**< Current bus clock */
    uint32_t    max_clock_hz;       /**< Highest clock programmed */
    dmsdio_bus_width_t bus_width;   /**< Current host bus width */
    bool        card_bus_4bit;      /**< Card switched to 4-bit (ACMD6) */
    bool        card_high_speed;    /**< Card switched to High Speed (CMD6) */
    bool        powered;            /**< Card power */
    uint64_t    last_address;       /**< Last read/write command argument */
} dmsdio_mock_stats_t;

/** Insert a fresh card of the given type (storage cleared). */
dmod_dmsdio_port_api(1.0, int, _mock_insert, ( dmsdio_instance_t instance, dmsdio_card_type_t type ));

/** Remove the card: every later command times out. */
dmod_dmsdio_port_api(1.0, int, _mock_remove, ( dmsdio_instance_t instance ));

/** Remove the card after `blocks` more data blocks have been transferred. */
dmod_dmsdio_port_api(1.0, int, _mock_remove_after_blocks, ( dmsdio_instance_t instance, uint32_t blocks ));

/** Deliver `fault` to the next `count` matching commands (cmd_index or DMSDIO_MOCK_ANY_COMMAND). */
dmod_dmsdio_port_api(1.0, int, _mock_inject_fault,
    ( dmsdio_instance_t instance, dmsdio_mock_fault_t fault, uint8_t cmd_index, uint32_t count ));

/** Set the CSD temporary write protection bit of the inserted card. */
dmod_dmsdio_port_api(1.0, int, _mock_set_write_protect, ( dmsdio_instance_t instance, bool enabled ));

/** Let the card refuse the CMD6 High Speed switch (switch status reports 0xF). */
dmod_dmsdio_port_api(1.0, int, _mock_refuse_high_speed, ( dmsdio_instance_t instance, bool refuse ));

/** Read and optionally clear the counters. */
dmod_dmsdio_port_api(1.0, int, _mock_get_stats, ( dmsdio_instance_t instance, dmsdio_mock_stats_t* stats, bool reset ));

/**
 * Content of a block that was never written. Deterministic in all 64 bits of
 * the block number so reads far beyond 4 GiB prove no address truncation.
 */
static inline uint8_t dmsdio_mock_pattern(uint64_t lba, uint32_t offset)
{
    uint64_t mix = lba ^ (lba >> 13) ^ (lba >> 27) ^ (lba >> 41);
    return (uint8_t)(mix * 31u + offset * 7u + (lba >> 32));
}

#endif /* DMSDIO_MOCK_H */
