#ifndef DMSDIO_MOCK_CARD_H
#define DMSDIO_MOCK_CARD_H

#include "dmsdio_mock.h"

/*
 * Command-level model of an SD memory card used by the x86_64 port. It
 * follows the SD Physical Layer Simplified Specification closely enough to
 * catch protocol mistakes in the core module: wrong ordering, wrong
 * argument encoding, raising the clock before High Speed is active,
 * mismatched bus widths or byte-vs-block addressing errors all fail.
 */

#define MOCK_SOURCE_CLOCK_HZ    48000000u
#define MOCK_IDENT_CLOCK_HZ     400000u
#define MOCK_DEFAULT_SPEED_HZ   25000000u
#define MOCK_BLOCK_SIZE         512u
#define MOCK_ACMD41_BUSY_POLLS  2u

typedef struct
{
    uint64_t    lba;
    uint8_t     data[MOCK_BLOCK_SIZE];
} mock_block_t;

typedef struct
{
    /* host side */
    bool                initialized;
    uint32_t            clock_hz;
    dmsdio_bus_width_t  host_width;
    bool                powered;

    /* card side */
    bool                inserted;
    dmsdio_card_type_t  type;
    uint32_t            state;
    uint16_t            rca;
    bool                app_cmd;
    bool                hcs_seen;
    uint32_t            acmd41_polls;
    bool                card_4bit;
    bool                high_speed;
    bool                refuse_high_speed;
    bool                write_protect;
    uint32_t            busy_polls;
    uint32_t            status_errors;
    uint64_t            erase_start;
    uint64_t            erase_end;

    /* faults */
    dmsdio_mock_fault_t fault;
    uint8_t             fault_cmd;
    uint32_t            fault_count;
    int64_t             remove_after;   /* -1 = disabled */
    dmsdio_mock_fault_t data_fault;     /* armed for the next data phase */

    /* storage (sparse) */
    mock_block_t*       blocks;
    uint32_t            block_used;

    dmsdio_mock_stats_t stats;
} mock_host_t;

/* registers */
uint64_t mock_card_capacity_blocks(dmsdio_card_type_t type);
void     mock_card_cid(const mock_host_t* host, uint32_t out[4]);
void     mock_card_csd(const mock_host_t* host, uint32_t out[4]);
void     mock_card_scr(const mock_host_t* host, uint8_t out[8]);
void     mock_card_ssr(const mock_host_t* host, uint8_t out[64]);
void     mock_card_switch_status(mock_host_t* host, uint32_t arg, uint8_t out[64]);

/* storage */
void     mock_store_clear(mock_host_t* host);
void     mock_store_read(const mock_host_t* host, uint64_t lba, uint8_t* out);
int      mock_store_write(mock_host_t* host, uint64_t lba, const uint8_t* in);

/* command processing */
dmsdio_status_t mock_card_execute(mock_host_t* host, const dmsdio_command_t* cmd,
                                  const dmsdio_data_t* data, dmsdio_response_t* resp);
void     mock_card_reset(mock_host_t* host);

#endif /* DMSDIO_MOCK_CARD_H */
