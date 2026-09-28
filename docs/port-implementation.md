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
| `stm32f4` | SDIO (RM0090 section 31), one instance. Thin wrapper over `stm32_common` |
| `stm32f7` | SDMMC1, RM0385/RM0410 section 35. Thin wrapper over `stm32_common`. SDMMC2 (F76x/F77x only) is not registered: packages are per family and its IRQ 103 does not exist on F74x/F75x |
| `x86_64` | Simulated SD card for the automated tests (`dmsdio_mock.h`: card types SDSC v1/v2, SDHC, SDXC, fault injection, removal, counters). Never released - CI and the release workflow exclude it from the hardware matrix |

### STM32 (`src/port/stm32_common`)

STM32F4 SDIO and STM32F7 SDMMC are the same host IP: identical register
offsets and bit positions for everything the port uses. The shared code in
`stm32_common/` (`stm32_common.c`: lifecycle, clock, commands, IRQ;
`stm32_data.c`: data path) implements the whole port API; each family's
`port.c` only provides:

* `stm32_sdio_instances[]` - base address, `RCC_APB2ENR`/`RCC_APB2RSTR` bit, NVIC IRQ and DMA controller/streams/channel per instance,
* `stm32_sdio_family_error_flags` - `STBITERR` on F4 (the bit is reserved on F7),
* `stm32_sdio_family_dma_reachable()` - memory DMA2 cannot access (F4 CCM),
* `dmod_init`/`dmod_deinit` and the `DMOD_IRQ_HANDLER`s.

Implementation notes:

* The kernel clock is read from `dmclk_port_get_domain_frequency(dmclk_domain_sdio)`
  (48 MHz CLK48/PLL48CLK). `_host_init` fails with `-EIO` when it is not running.
  `SDIO_CK = SDIOCLK / (CLKDIV + 2)`, or `SDIOCLK` itself with `BYPASS` when the
  requested maximum is at least the source (High Speed: 48 MHz).
* Commands complete on `CMDREND`/`CMDSENT`/`CCRCFAIL`/`CTIMEOUT` interrupts.
  `CCRCFAIL` is ignored for R3 (`dmsdio_response_short_no_crc`).
* Block data moves by DMA (`stm32_data.c`). `_host_init` leases DMA2
  stream 3 - or stream 6 when 3 is taken - on channel 4 from dmdma
  (RM0090 table 43 / RM0385 table 27) and fails with `-EBUSY` when neither
  is free; there is no silent PIO fallback. The stream runs in FIFO mode
  with a full threshold, 4-beat word bursts on both sides and the SDIO as
  flow controller (`dmdma_lease_start_ex()`, RM0090 31.3.2 / RM0385 35.3.2),
  so interrupt latency can no longer overrun the SDIO FIFO. For reads the
  DMA stream and the DPSM are armed before the command; for writes the DMA
  stream is started before the command and the DPSM only after a valid
  response. A transfer completes when both `DATAEND` and the DMA completion
  have been seen. DMA buffers must be 16-byte aligned
  (`DMSDIO_TRANSFER_ALIGNMENT`, the core bounces other buffers) and
  reachable by DMA2 (not F4 CCM RAM). The D-cache is not enabled by
  dmod-boot, so no cache maintenance is done.
* Reads that fit the 32-word FIFO (SCR, SD Status, CMD6 switch status) do
  not use DMA: the FIFO holds the whole transfer and is read at `DATAEND`.
* The DMA2 controller has to be configured through dmdma before dmsdio is
  created (dmdma's board `dma.ini` with `driver_order=2`, dmsdio with 3).
  Requires dmdma with `dmdma_lease_start_ex()` (stream options).
* Pins (AF12, pull-ups on CMD/D0-D3) are configured by dmgpio from the board
  files in [`configs/`](../configs/README.md), not by the port.
