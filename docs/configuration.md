# dmsdio Configuration

dmsdio is configured through dmdevfs with an ini file. The settings are read
from the section dmdevfs activates for the device (board files describing
several devices), otherwise from a `[dmsdio]` section, otherwise from the
first section containing an `instance` key.

```ini
[dmsdio]
instance=1
bus_width=4
max_clock_hz=50000000
high_speed=true
card_detect_handler=sd_card_detect
card_detect_active_level=low
```

| Key | Default | Description |
|-----|---------|-------------|
| `instance` | `1` | Host controller instance (1 = SDMMC1/SDIO) |
| `major` | `instance - 1` | Device number: `/dev/dmsdio<major>` and `/dev/dmsdio<major>/0` |
| `bus_width` | `4` | Widest bus wired on the board (`1` or `4`). 4-bit is used only when the card's SCR supports it |
| `max_clock_hz` | `50000000` | Upper clock limit. High Speed (50 MHz class) is only attempted above 25 MHz |
| `high_speed` | `true` | Allow the CMD6 High Speed switch. The clock is raised only after the card's switch status confirms it |
| `retries` | `3` | Extra attempts for transient errors (0-16). With `0`, errors are reported precisely instead of as `-EIO` |
| `init_timeout_ms` | `1000` | ACMD41 power-up limit |
| `read_timeout_ms` | `250` | Per-block read access limit |
| `write_timeout_ms` | `500` | Per-block write busy limit, also used for CMD13 ready polling |
| `erase_timeout_ms` | `3000` | Minimum erase busy limit per 4 MiB chunk (raised from SD Status `ERASE_TIMEOUT` when larger) |
| `max_blocks_per_transfer` | `128` | Split larger requests into several multi-block commands |
| `card_detect_active_level` | `low` | Card detect level meaning "card inserted" (`low` or `high`) |
| `card_detect_handler` | none | dmhaman handler name the card-detect GPIO's `interrupt_handler` points to (at most 31 characters). Used by dmsdiod |
| `card_detect_debounce_ms` | `50` | Settle time for card detect edges. Used by dmsdiod |
| `poll_interval_ms` | `1000` | Periodic presence re-check by dmsdiod; `0` disables it |

The last three keys are presence monitoring policy: the driver does not act
on them itself, it hands them to the dmsdiod service through
`dmsdio_ioctl_cmd_get_detect_config` (see below).

## Card detect

Card detect is optional. It is a dmgpio device in the same `friends_group`
as the dmsdio device, with `friend_role=card_detect`, interrupts on both
edges and its `interrupt_handler` set to the name used for
`card_detect_handler`. For example (board file with one section per
device):

```ini
[sd_card]
driver_name=dmsdio
friends_group=sd0
instance=1
bus_width=4
card_detect_handler=sd0_card_detect
card_detect_active_level=low

[sd_card_detect]
driver_name=dmgpio
friends_group=sd0
friend_role=card_detect
pin=PD3
mode=input
pull=up
interrupt_trigger=both_edges
interrupt_handler=sd0_card_detect
```

The SD bus pins themselves (CK, CMD, D0-D3: AF12, pull-ups on CMD and
D0-D3) are dmgpio devices of the same group - see the complete board files
in [../configs/](../configs/README.md) for STM32F746G-DISCO and
STM32F407G-DISC1.

dmdevfs reports the GPIO node to dmsdio through `dmdrvi_friend_changed()`;
the driver samples it on every scan.

## Presence monitoring (dmsdiod)

The driver starts no threads. A card present at boot is identified inside
`dmdrvi_create()`; everything that happens over time - card detect edges,
debouncing, periodic re-checks - is done by the **dmsdiod** service
([`services/dmsdiod`](../services/dmsdiod/README.md)), one instance per host:

1. dmsdio reports its host node from `dmdrvi_path_ready()` with
   `libsystemd_notify_device_added("sdio", "dmsdio<major>", "/dev/dmsdio<major>")`
   (and `libsystemd_notify_device_removed()` from `dmdrvi_free()`).
2. The `[class=sdio]` rule from `dmsdiod.rules` starts `dmsdiod@dmsdio<major>`
   from the `dmsdiod@.ini` template, with the node path as its argument.
3. dmsdiod reads `card_detect_handler`, `card_detect_debounce_ms` and
   `poll_interval_ms` through `dmsdio_ioctl_cmd_get_detect_config` and
   registers the dmhaman handler. The handler (interrupt context) only posts
   a semaphore.
4. On an edge dmsdiod calls `dmsdio_ioctl_cmd_check_removal` - lock-free, so
   a transfer running on a removed card is aborted with `-ENODEV` at once -
   waits until the pin has been quiet for a whole debounce window, then
   calls `dmsdio_ioctl_cmd_rescan`, which identifies, verifies (CMD13) or
   detaches the card under the driver lock.
5. Without a card detect pin, `dmsdio_ioctl_cmd_rescan` runs every
   `poll_interval_ms`. With neither, dmsdiod rescans once and exits.

Install `dmsdiod.rules` into libsystemd's rules directory and
`dmsdiod@.ini` into its units directory (both ship in the dmsdiod package
under `configs/`). Without dmsdiod the card present at boot still works;
insertions and removals are then only noticed through a transfer error or a
manual `dmsdio_ioctl_cmd_rescan`.

Use a distinct `card_detect_handler` name per host: dmsdiod registers it with
dmhaman and unregisters it when the unit is stopped.
