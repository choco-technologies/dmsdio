# dmsdiod

SD card presence service for [dmsdio](../../README.md) - one instance per
host node (`/dev/dmsdioN`).

The dmsdio driver starts no threads of its own. It identifies a card
present at boot inside `dmdrvi_create()` and otherwise only reacts to
requests. dmsdiod is what notices insertions and removals afterwards:

* with a card detect GPIO it registers the host's `card_detect_handler` with
  dmhaman; the handler runs in interrupt context and only posts a semaphore.
  On an edge dmsdiod asks the driver to check for a removal right away
  (`dmsdio_ioctl_cmd_check_removal`, lock-free - a transfer running on a
  pulled card is aborted with `-ENODEV`), waits until the pin has been quiet
  for `card_detect_debounce_ms`, then calls `dmsdio_ioctl_cmd_rescan`;
* without one it calls `dmsdio_ioctl_cmd_rescan` every `poll_interval_ms`;
* with neither it rescans once and exits with 0.

The policy (`card_detect_handler`, `card_detect_debounce_ms`,
`poll_interval_ms`) stays in the dmsdio device's own ini section and is read
through `dmsdio_ioctl_cmd_get_detect_config` - see
[../../docs/configuration.md](../../docs/configuration.md).

## Usage

```
dmsdiod <host_node>
```

It is not meant to be started by hand. dmsdio reports each host node to
libsystemd from `dmdrvi_path_ready()`:

```c
libsystemd_notify_device_added("sdio", "dmsdio0", "/dev/dmsdio0");
```

and the files in [`configs/`](configs) turn that into a running unit:

| File | Install into | Purpose |
|------|--------------|---------|
| `dmsdiod.rules` | libsystemd rules directory | `[class=sdio] start=dmsdiod@%name` |
| `dmsdiod@.ini` | libsystemd units directory | `exec=dmsdiod`, `args=%v` (the node path), `restart=on-failure` |

`service status dmsdiod@dmsdio0` / `service stop dmsdiod@dmsdio0` manage
it like any other unit; when dmdevfs frees the dmsdio context the driver
reports `libsystemd_notify_device_removed("sdio", "dmsdio0")` and the unit is
stopped. Stopping the unit unregisters the dmhaman handler (process exit
callback).

Every request opens the host node, issues one ioctl and closes it again, so
a stopped unit never leaves a handle behind.
