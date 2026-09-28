# Implementing a dmsdio Port

`dmsdio` is split into two DMOD modules:

* **`dmsdio`** - everything SD specific: identification (CMD0, CMD8,
  ACMD41, CMD2, CMD3, CMD9, CMD7), register decoding (OCR, CID, CSD, SCR,
  SD Status, CMD6 switch status), SDSC byte vs. SDHC/SDXC block
  addressing, 400 kHz / default speed / High Speed clocking, 1-bit/4-bit
  negotiation, single/multi-block transfers, CMD12, CMD13 ready polling,
  erase/discard, retry, recovery, hot-plug and the dmdrvi device model.
* **`dmsdio_port`** - protocol-free host controller primitives selected by
  `DMOD_CPU_FAMILY`.

A port must never decide which command to send, interpret a response or a
card register, or convert byte offsets to card addresses.

## Port API

Declared in [`include/dmsdio_port.h`](../include/dmsdio_port.h):

| Function | Contract |
|----------|----------|
| `_host_init(instance)` | Acquire clocks, pins, DMA and IRQs and reset the controller. Any failed resource acquisition is returned as an error, never silently degraded |
| `_host_deinit(instance)` | Release everything acquired by `_host_init` |
| `_set_power(instance, on)` | Card power and clock output. After power-on the clock must run so the core's wait provides the 74 initialization clocks |
| `_set_clock(instance, max_hz, &actual_hz)` | Program the highest clock not above `max_hz` and report it. The core rejects an identification clock above 400 kHz |
| `_set_bus_width(instance, width)` | Host side data bus width (1 or 4) |
| `_execute(instance, &command, data, &response)` | Send one command, capture the response shape requested by `command.response`, and run the optional data phase. The data path must be armed before the command is sent. Completion and errors are interrupt driven and bounded by the command timeout and `data->timeout_ms` (per block) |
| `_abort(instance)` | Stop an interrupted data phase and return the controller to idle. Called after every failed `_execute`; must not send commands to the card |

### Responses

| `dmsdio_response_type_t` | Capture | CRC |
|--------------------------|---------|-----|
| `dmsdio_response_none` | nothing | - |
| `dmsdio_response_short` | 32-bit payload in `words[0]`, command index in `index` | checked |
| `dmsdio_response_short_busy` | as short; the port may return before DAT0 is released | checked |
| `dmsdio_response_short_no_crc` | as short (R3) | not checked |
| `dmsdio_response_long` | bits 127:0 in `words[0..3]`, most significant word first | checked |

Map controller outcomes to `dmsdio_status_t`: `cmd_timeout`, `cmd_crc`,
`data_timeout`, `data_crc`, `overrun` (FIFO/DMA fault), `invalid`,
`not_supported`. The core converts them to errno values and runs the retry
and recovery policy.

### Data phase

`dmsdio_data_t` describes `block_count` blocks of `block_size` bytes (512
for card data, 8 for SCR, 64 for SD Status / switch status). Buffers passed
for 512-byte transfers are `DMSDIO_TRANSFER_ALIGNMENT` (4) byte aligned;
the core bounces misaligned caller buffers itself. The internal scratch
block is 32-byte aligned so a port can do cache maintenance on it.

## Adding a CPU family

1. Create `src/port/<family>/config.cmake` setting `DMOD_TOOLS_NAME` (it must
   match a directory under `dmod/configs/arch/...`).
2. Create `src/port/<family>/port.c` defining `DMOD_ENABLE_REGISTRATION`,
   implementing every `dmod_dmsdio_port_api_declaration(...)` from
   `include/dmsdio_port.h` plus `dmod_init`/`dmod_deinit` and the
   `DMOD_IRQ_HANDLER(...)`s it needs.
3. Share only register and sequencing logic that is identical between
   families in `src/port/<family>_common/`.
4. Build with `cmake .. -DDMOD_CPU_FAMILY=<family>`. Do not introduce a
   module-specific MCU series variable.

## Existing ports

| Family | Status |
|--------|--------|
| `x86_64` | Simulated SD card for the automated tests (`dmsdio_mock.h`: card types SDSC v1/v2, SDHC, SDXC, fault injection, removal, counters). Never released - CI and the release workflow exclude it from the hardware matrix |
| `stm32f7` | Placeholder: every call reports `-ENOTSUP`, so `dmdrvi_create()` fails loudly. The SDMMC implementation is tracked in choco-technologies/dmod-ecosystem#12 |
