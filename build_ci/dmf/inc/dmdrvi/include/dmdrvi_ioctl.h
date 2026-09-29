#ifndef DMDRVI_IOCTL_H
#define DMDRVI_IOCTL_H

#include <stdint.h>
#include "dmdrvi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Length in bytes of an Ethernet MAC address
 */
#define DMDRVI_NET_MAC_ADDR_LEN    6

/**
 * @brief MAC address type
 */
typedef struct
{
    uint8_t addr[DMDRVI_NET_MAC_ADDR_LEN];    ///< MAC address bytes
} dmdrvi_net_mac_addr_t;

/**
 * @brief Network link status
 */
typedef enum
{
    DMDRVI_NET_LINK_DOWN = 0,    ///< Link is down (disconnected)
    DMDRVI_NET_LINK_UP   = 1,    ///< Link is up (connected)
} dmdrvi_net_link_status_t;

/**
 * @brief Set the device MAC address
 *
 * arg: const dmdrvi_net_mac_addr_t* - MAC address to set (input)
 */
#define DMDRVI_IOCTL_NET_SET_MAC_ADDR       0x01

/**
 * @brief Get the device MAC address
 *
 * arg: dmdrvi_net_mac_addr_t* - buffer to receive the MAC address (output)
 */
#define DMDRVI_IOCTL_NET_GET_MAC_ADDR       0x02

/**
 * @brief Get the current link status
 *
 * arg: dmdrvi_net_link_status_t* - buffer to receive the link status (output)
 */
#define DMDRVI_IOCTL_NET_GET_LINK_STATUS    0x03

/**
 * @brief Start the network interface (begin packet reception/transmission)
 *
 * arg: none
 */
#define DMDRVI_IOCTL_NET_START               0x04

/**
 * @brief Stop the network interface
 *
 * arg: none
 */
#define DMDRVI_IOCTL_NET_STOP                0x05

/** Block device is read-only. */
#define DMDRVI_BLOCK_FLAG_READ_ONLY          (1u << 0)

/** Block device can be removed while the system is running. */
#define DMDRVI_BLOCK_FLAG_REMOVABLE          (1u << 1)

/** Block device implements DMDRVI_IOCTL_BLOCK_ERASE. */
#define DMDRVI_BLOCK_FLAG_ERASE_SUPPORTED    (1u << 2)

/** Block device implements DMDRVI_IOCTL_BLOCK_DISCARD. */
#define DMDRVI_BLOCK_FLAG_DISCARD_SUPPORTED  (1u << 3)

/** Standard geometry and capabilities returned by a block device. */
typedef struct
{
    uint32_t logical_block_size;  /**< Addressable block size in bytes. */
    uint32_t erase_block_size;    /**< Minimum erase unit in bytes, or zero. */
    dmdrvi_size_t block_count;    /**< Number of logical blocks. */
    uint32_t flags;               /**< DMDRVI_BLOCK_FLAG_* capability bits. */
} dmdrvi_block_info_t;

/** Byte range used by erase and discard controls. */
typedef struct
{
    dmdrvi_offset_t offset;  /**< Non-negative byte offset. */
    dmdrvi_size_t length;    /**< Range length in bytes. */
} dmdrvi_block_range_t;

/**
 * Read block geometry and capabilities.
 *
 * arg: dmdrvi_block_info_t* - output buffer
 */
#define DMDRVI_IOCTL_BLOCK_GET_INFO          0x100

/**
 * Physically erase an aligned byte range. Successful completion means the
 * operation has completed on the medium. The post-erase byte value is
 * device-specific.
 *
 * arg: const dmdrvi_block_range_t* - input range
 */
#define DMDRVI_IOCTL_BLOCK_ERASE             0x101

/**
 * Inform the device that an aligned byte range is no longer in use. Reads of
 * discarded data are unspecified until it is written again.
 *
 * arg: const dmdrvi_block_range_t* - input range
 */
#define DMDRVI_IOCTL_BLOCK_DISCARD           0x102

/*
 * Monitor commands - class-independent (a USB host controller node is not a
 * block device). They let a driver have work done over time - presence
 * detection, hot-plug, media or link polling - without creating threads of
 * its own: the driver declares what should trigger it (GET_POLICY), a
 * monitor service waits for that and calls EVENT (urgent, non-blocking) and
 * REFRESH (settles the state). See docs/dmdrvi.md, "Monitor Ioctl Commands",
 * for when each command is called and what a driver may do in it.
 *
 * A driver that needs no monitoring does not implement them (-ENOTTY).
 * All three are called from thread context only, never from an ISR.
 */

/** Longest event handler name in dmdrvi_monitor_policy_t, including the terminator. */
#define DMDRVI_MONITOR_HANDLER_NAME_MAX      32u

/** What should trigger the monitor for a node. */
typedef struct
{
    /** dmhaman handler whose calls are the node's events ("" = no events).
     *  Fired from interrupt context by another driver (e.g. a dmgpio edge
     *  interrupt) or by the driver's own ISR. */
    char     event_handler[DMDRVI_MONITOR_HANDLER_NAME_MAX];
    uint32_t settle_ms;          /**< Quiet time after the last event before REFRESH */
    uint32_t poll_interval_ms;   /**< Periodic REFRESH interval, 0 = no polling */
} dmdrvi_monitor_policy_t;

/**
 * Read the monitoring policy of a node. Called once when the monitor starts
 * (and again after it restarts). -ENOTTY means the node is not monitored.
 *
 * arg: dmdrvi_monitor_policy_t* - output buffer
 */
#define DMDRVI_IOCTL_MONITOR_GET_POLICY      0x200

/**
 * An event arrived. Called right after every event, before the settle time.
 * Must return quickly and must not wait for in-flight I/O - it may run
 * concurrently with read/write on the same context. It may only record
 * state or set flags (e.g. make a transfer on a pulled card fail at once);
 * nodes are announced or withdrawn only by REFRESH. Return 0 when there is
 * nothing to do.
 *
 * arg: NULL
 */
#define DMDRVI_IOCTL_MONITOR_EVENT           0x201

/**
 * Settle the state behind the node. Called once when the monitor starts,
 * after events once settle_ms passed without a new one, and every
 * poll_interval_ms if non-zero. May block; serialized with I/O. Announces or
 * withdraws nodes through dmdrvi_device_available() /
 * dmdrvi_device_unavailable().
 *
 * Returns 0 when something is attached behind the node, -ENODEV when nothing
 * is, another negative errno value on failure.
 *
 * arg: NULL
 */
#define DMDRVI_IOCTL_MONITOR_REFRESH         0x202

/**
 * @brief Start of the reserved range for driver-specific custom ioctl commands
 *
 * A driver built on dmdrvi (e.g. a network driver needing something beyond
 * DMDRVI_IOCTL_NET_*) should number its own private commands starting from
 * this base, not from "last standard command + 1" - the standard command
 * set above is expected to grow over time, and a driver numbering its own
 * commands relative to whichever one happens to be last today would silently
 * collide with a new standard command added later. Standard categories are
 * spaced 0x100 apart (network 0x01, block 0x100, monitor 0x200), which
 * leaves generous headroom before reaching this base.
 */
#define DMDRVI_IOCTL_CUSTOM_BASE              0x1000

#ifdef __cplusplus
}
#endif

#endif // DMDRVI_IOCTL_H
