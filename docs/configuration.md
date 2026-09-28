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
card_detect_active_level=low
monitor_event_handler=sd_card_detect
poll_interval_ms=0
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
| `monitor_event_handler` | none | dmhaman handler name the card-detect GPIO's `interrupt_handler` points to (at most 31 characters) |
| `monitor_settle_ms` | `50` | Quiet time after the last card detect edge before the card is re-checked |
| `poll_interval_ms` | `1000` | Periodic presence re-check; `0` disables it |

The last three keys are the dmdrvi monitor policy, under the names dmdrvi
prescribes for every driver: dmsdio does not act on them itself, it hands
them out through `DMDRVI_IOCTL_MONITOR_GET_POLICY` (see below).

## Card detect

Card detect is optional. It is a dmgpio device in the same `friends_group`
as the dmsdio device, with `friend_role=card_detect`, interrupts on both
edges and its `interrupt_handler` set to the name used for
`monitor_event_handler`. For example (board file with one section per
device):

```ini
[sd_card]
driver_name=dmsdio
friends_group=sd0
instance=1
bus_width=4
card_detect_active_level=low
monitor_event_handler=sd0_card_detect
poll_interval_ms=0

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

## Presence monitoring

The driver starts no threads and does not touch the card in
`dmdrvi_create()`. Everything that happens over time is driven through the
dmdrvi monitor contract on the host node by dmdevfs' generic
[dmdevmon](https://github.com/choco-technologies/dmdevfs/blob/main/services/dmdevmon/README.md) service, one instance per host:

1. dmdevfs reports the host node `/dev/dmsdio<major>`, which answers
   `DMDRVI_IOCTL_MONITOR_GET_POLICY`, to libsystemd as a `monitor` device
   named `dmsdio<major>`.
2. The `[class=monitor]` rule from `dmdevmon.rules` starts
   `dmdevmon@dmsdio<major>` from the `dmdevmon@.ini` template, with the node
   path as its argument.
3. dmdevmon reads the policy (`monitor_event_handler`, `monitor_settle_ms`,
   `poll_interval_ms`), registers the dmhaman handler - it runs in interrupt
   context and only wakes dmdevmon - and calls `DMDRVI_IOCTL_MONITOR_REFRESH`
   once, which identifies a card already in the slot.
4. On an edge dmdevmon calls `DMDRVI_IOCTL_MONITOR_EVENT` - lock-free, so a
   transfer running on a removed card is aborted with `-ENODEV` at once -
   and, once the pin has been quiet for `monitor_settle_ms`, `REFRESH`,
   which identifies, verifies (CMD13) or detaches the card under the driver
   lock and announces or withdraws `/dev/dmsdio<major>/0`.
5. Without a card detect pin, `REFRESH` runs every `poll_interval_ms`. With
   neither, dmdevmon refreshes once and exits.

Install `dmdevmon.rules` into libsystemd's rules directory and
`dmdevmon@.ini` into its units directory (both ship in the dmdevmon package
under `configs/`). Without a monitor no card is ever identified.

Use a distinct `monitor_event_handler` name per host.
