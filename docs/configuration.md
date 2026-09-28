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
| `poll_interval_ms` | `1000` | Presence polling period; `0` disables polling |
| `card_detect_handler` | none | dmhaman handler name the card-detect GPIO's `interrupt_handler` points to |
| `card_detect_active_level` | `low` | Card detect level meaning "card inserted" (`low` or `high`) |
| `card_detect_debounce_ms` | `50` | Settle time for card detect edges |

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

dmdevfs reports the GPIO node to dmsdio through `dmdrvi_friend_changed()`.
The interrupt only enqueues an event (dmhaman handler, ISR context); a dmosi
worker thread debounces the pin, then identifies or detaches the card. A
running transfer on a removed card is aborted with `-ENODEV`.

Without a card detect pin, the worker re-checks presence every
`poll_interval_ms` (CMD13 for an attached card, an identification attempt
for an empty slot). `dmsdio_ioctl_cmd_rescan` on the host node triggers the
same check synchronously.

Use a distinct `card_detect_handler` name per host: it is unregistered from
dmhaman when the dmsdio context is freed.
