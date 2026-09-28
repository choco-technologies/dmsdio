# dmsdio API Reference

dmsdio is an SD memory card driver (SDSC v1/v2, SDHC, SDXC) for the dmdrvi
2.0 device model. All SD protocol handling lives in the architecture
independent `dmsdio` module; `dmsdio_port` only provides raw host controller
primitives (see [port-implementation.md](port-implementation.md)).

## Device nodes

| Node | dmdrvi numbering | Lifetime | Purpose |
|------|------------------|----------|---------|
| `/dev/dmsdioN` | `DMDRVI_NUM_MAJOR`, major = N | Persistent, created by `dmdrvi_create()` | Host status and control ioctls |
| `/dev/dmsdioN/0` | `DMDRVI_NUM_MAJOR \| DMDRVI_NUM_MINOR`, minor 0 | Announced with `dmdrvi_device_available()` once a card is identified *and* dmdevfs has reported the host node through `dmdrvi_path_ready()`; withdrawn with `dmdrvi_device_unavailable()` when the card goes away | 64-bit byte-addressed block device |

`N` defaults to `instance - 1` (SDMMC1 → `/dev/dmsdio0`), see
[configuration.md](configuration.md).

Identification starts as soon as `dmdrvi_create()` returns, on the driver's
own worker thread. The card node is only announced after
`dmdrvi_path_ready()` for the host node because dmdevfs ignores hot-plug
notices for a context it has not registered yet.

## Card node (`/dev/dmsdioN/0`)

### read / write

```c
dmdrvi_ssize_t dmdrvi_read (ctx, handle, void* buf,       size_t size, dmdrvi_offset_t offset);
dmdrvi_ssize_t dmdrvi_write(ctx, handle, const void* buf, size_t size, dmdrvi_offset_t offset);
```

* `offset` is a 64-bit byte offset; any offset and length are accepted.
  Whole, 4-byte aligned blocks are transferred directly between the caller's
  buffer and the card (CMD17/CMD18, CMD24/CMD25 + CMD12, split into at most
  `max_blocks_per_transfer` blocks per command). A partial block at either
  end of the range - or a misaligned buffer - goes through a one-block
  bounce buffer; writes then read-modify-write that block so neighbouring
  bytes are preserved.
* Reads return the byte count, clipped at the end of the card, and `0` at
  or past the end. Writes starting at or past the end return `-ENOSPC`,
  writes crossing the end are clipped.
* A write returns once the card reports `READY_FOR_DATA` in the transfer
  state again (CMD13 polling), i.e. once the data is programmed.
* Operations of one host are serialized; one request is never interleaved
  with another.

### flush

Waits until the card is back in the transfer state and ready for data.

### ioctl

| Command | `arg` | Result |
|---------|-------|--------|
| `DMDRVI_IOCTL_BLOCK_GET_INFO` | `dmdrvi_block_info_t*` | `logical_block_size` = 512, `block_count` (64-bit), `erase_block_size` = 512 when the card supports erase, flags `REMOVABLE`, `ERASE_SUPPORTED`, `DISCARD_SUPPORTED` (SD Status `DISCARD_SUPPORT`), `READ_ONLY` (CSD write protection) |
| `DMDRVI_IOCTL_BLOCK_ERASE` | `const dmdrvi_block_range_t*` | CMD32/CMD33/CMD38, waits for completion. Range must be 512-byte aligned and inside the card |
| `DMDRVI_IOCTL_BLOCK_DISCARD` | `const dmdrvi_block_range_t*` | CMD38 with the discard argument; `-ENOTSUP` unless advertised |
| `dmsdio_ioctl_cmd_get_host_info` | `dmsdio_host_info_t*` | See below |
| `dmsdio_ioctl_cmd_get_card_info` | `dmsdio_card_info_t*` | Raw and decoded CID/CSD/SCR/SD Status, capacity, bus width, clock, High Speed |

### stat

`dmdrvi_stat()` on the card node reports the full 64-bit capacity as
`size` (`0444` when the card is write protected, `0666` otherwise).

## Host node (`/dev/dmsdioN`)

Data transfers return `-ENOTSUP`; `stat` reports size 0.

| Command | `arg` | Result |
|---------|-------|--------|
| `dmsdio_ioctl_cmd_get_host_info` | `dmsdio_host_info_t*` | `card_attached`, `generation`, `scan_count`, `last_error`, `retry_count` |
| `dmsdio_ioctl_cmd_get_card_info` | `dmsdio_card_info_t*` | Card snapshot, `-ENODEV` without a card |
| `dmsdio_ioctl_cmd_rescan` | `NULL` | Synchronously re-checks presence: verifies an attached card (CMD13) or identifies a new one. `0` when a card is attached afterwards, `-ENODEV` when the slot is empty |

## Card generation and stale handles

Every attach and every detach increments the host's card generation
(`dmsdio_host_info_t.generation`). A card node handle remembers the
generation it was opened for:

* no card attached any more → `-ENODEV`
* a different card (or the same card re-inserted) attached → `-ESTALE`

Close the old handle and open the card node again after a new
`dmdrvi_device_available()` notification.

## Errors

| Error | Meaning |
|-------|---------|
| `-EBADMSG` | CRC error on a response or data block (with `retries=0`) |
| `-ETIMEDOUT` | Command, data or busy timeout (with `retries=0`) |
| `-EIO` | A transient error (CRC, timeout, FIFO/DMA fault) persisted through all `retries` extra attempts, or a card-internal error (ECC, CC_ERROR, ...) |
| `-EPROTO` | Malformed response: wrong command index, bad CMD8 echo, inconsistent registers, illegal command. Never retried |
| `-ENODEV` | No card, or the card disappeared (also during a transfer). The card node is withdrawn |
| `-ESTALE` | Handle belongs to a card generation that is no longer attached |
| `-EROFS` | Card is write protected |
| `-EINVAL` | Invalid argument or range (negative offset, misaligned erase range, card reported OUT_OF_RANGE/ADDRESS_ERROR) |
| `-EOVERFLOW` | `size` larger than `INT64_MAX` |
| `-EBADF` | Write on a read-only handle or read on a write-only handle |
| `-ENOSPC` | Write starting at or beyond the end of the card |
| `-ENOTSUP` | Operation not available on this node / card |
| `-ENOTTY` | Unknown ioctl |

Transient errors are retried after bringing the card back to the transfer
state (port abort, CMD12 when the card is still sending/receiving, CMD13
until ready). A card that stops answering CMD13 during that recovery is
treated as removed.

## Types

Declared in `include/dmsdio_types.h`:

* `dmsdio_card_type_t` - `sdsc_v1`, `sdsc_v2`, `sdhc`, `sdxc`
* `dmsdio_card_info_t` - `type`, `generation`, `rca`, `ocr`, `cid_raw[4]`,
  `csd_raw[4]` (most significant word first), decoded `cid`, `csd`, `scr`,
  `ssr`, `capacity_bytes`, `block_count`, `block_addressing`, `high_speed`,
  `write_protected`, `bus_width`, `clock_hz`
* `dmsdio_host_info_t` - `instance`, `card_attached`, `generation`,
  `scan_count`, `last_error`, `retry_count`
* `dmsdio_cid_t`, `dmsdio_csd_t`, `dmsdio_scr_t`, `dmsdio_ssr_t` - decoded registers

## Functions (Built-in API)

Pure register decoders, also used by the driver itself:

```c
int dmsdio_decode_csd(const uint32_t raw[4], dmsdio_csd_t* csd);
int dmsdio_decode_cid(const uint32_t raw[4], dmsdio_cid_t* cid);
int dmsdio_decode_scr(const uint8_t raw[8],  dmsdio_scr_t* scr);
int dmsdio_decode_ssr(const uint8_t raw[64], dmsdio_ssr_t* ssr);
```

`dmsdio_decode_csd()` supports CSD v1.0 (SDSC) and v2.0 (SDHC/SDXC) and
returns `-ENOTSUP` for CSD v3.0 (SDUC). All return `-EINVAL` for NULL
arguments and `-EPROTO` for inconsistent contents.
