# dmsdio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmsdio/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmsdio/actions/workflows/ci.yml)

SD memory card driver for DMOD: SDSC v1/v2, SDHC and SDXC cards exposed as
a 64-bit block device through dmdrvi 2.0.

## Description

- Complete, hardware-independent SD protocol: identification (CMD0, CMD8,
  ACMD41, CMD2, CMD3, CMD9, CMD7), OCR/CID/CSD/SCR/SD Status decoding,
  400 kHz identification, default speed and CMD6 High Speed, 1-bit/4-bit
  bus negotiation.
- SDSC byte addressing and SDHC/SDXC block addressing; full 64-bit
  capacity and offsets.
- Single/multi-block read and write with CMD12, CMD13 ready polling, erase
  and discard, retry with recovery, precise errors.
- Arbitrary byte offsets: direct full-sector transfers, read-modify-write
  for partial sectors.
- dmdrvi device model: persistent host node `/dev/dmsdio0` plus the
  hot-plugged card node `/dev/dmsdio0/0`; handles are invalidated with a
  card generation identifier.
- Card detect through an optional dmgpio friend; the ISR only enqueues, a
  dmosi worker debounces and attaches/detaches the card.

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

`tests/dmsdio_test.c` runs the complete protocol against the simulated SD
card of the x86_64 port (`src/port/x86_64`): SDSC v1/v2, SDHC and SDXC
identification, 64-bit capacity and offsets, sector and unaligned I/O,
erase, write protection, CRC/timeout/malformed-response errors, retries,
removal during a transfer, stale handles and the presence worker. The test
uses the driver the way dmdevfs does - through the dmdrvi DIF - and
implements the dmdrvi MAL to observe hot-plug notifications.

```bash
# core module and test: built for the host, but DMOD_CPU_FAMILY stays at its
# default (stm32f7) because dmgpio only publishes stm32 headers
mkdir -p build && cd build
cmake .. -DDMOD_TOOLS_NAME=arch/x86_64
cmake --build . --target dmsdio test_dmsdio
cd ..

# simulated card port
mkdir -p build_port_x86_64 && cd build_port_x86_64
cmake .. -DDMOD_CPU_FAMILY=x86_64
cmake --build . --target dmsdio_port
cp dmf/dmsdio_port.dmf dmf/dmsdio_port.dmd ../build/dmf/
cd ..

export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install dmini -y
dmf-get install dmhaman -y
dmod_loader build/dmf/test_dmsdio.dmf
```

## Usage

dmsdio is a dmdrvi driver loaded by dmdevfs from an ini configuration (see
[docs/configuration.md](docs/configuration.md)):

```ini
[dmsdio]
instance=1
bus_width=4
```

Once a card is identified, `/dev/dmsdio0/0` appears and can be used with the
regular file API using 64-bit offsets; `DMDRVI_IOCTL_BLOCK_GET_INFO`
reports the geometry.

## API

| Item | Description |
|------|-------------|
| dmdrvi DIF (`dmdrvi_create`, `_open`, `_read`, `_write`, `_ioctl`, `_flush`, `_stat`, `_friend_changed`, ...) | Device model, see [docs/api-reference.md](docs/api-reference.md) |
| `dmsdio_ioctl_cmd_get_host_info` / `_get_card_info` / `_rescan` | Driver specific ioctls |
| `dmsdio_decode_csd()` / `_cid()` / `_scr()` / `_ssr()` | Register decoders |

See [include/dmsdio.h](include/dmsdio.h) and
[include/dmsdio_types.h](include/dmsdio_types.h) for the declarations and
[docs/api-reference.md](docs/api-reference.md) for the complete reference.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation
- **[configuration.md](docs/configuration.md)** - Configuration keys and card detect
- **[port-implementation.md](docs/port-implementation.md)** - Port API contract

View documentation using `dmf-man dmsdio`.

## Hardware Port

This module ships two DMOD modules: the architecture-independent
`dmsdio` (all SD protocol logic) and `dmsdio_port`, which contains the
protocol-free host controller primitives. The active architecture is
selected via `DMOD_CPU_FAMILY` (default: `stm32f7`): `stm32f4` (SDIO) and
`stm32f7` (SDMMC1) share their implementation in `src/port/stm32_common`;
`x86_64` is the simulated card used by the tests. Board configurations for
STM32F746G-DISCO and STM32F407G-DISC1 are in [configs/](configs/README.md).

```bash
cmake .. -DDMOD_CPU_FAMILY=stm32f7
```

See [docs/port-implementation.md](docs/port-implementation.md) for how to add
another architecture. Port-specific files:

```
├── include/dmsdio_port.h
├── src/port/
│   ├── CMakeLists.txt
│   ├── stm32_common/      # SDIO/SDMMC host shared by F4 and F7
│   │   ├── stm32_common.c # lifecycle, clock, commands, IRQ
│   │   ├── stm32_common.h
│   │   └── stm32_data.c   # data path: DMA2 lease (dmdma), FIFO for small reads
│   ├── stm32f4/
│   │   ├── config.cmake
│   │   └── port.c
│   ├── stm32f7/
│   │   ├── config.cmake
│   │   └── port.c
│   └── x86_64/            # simulated SD card (tests only)
│       ├── config.cmake
│       ├── dmsdio_mock.h
│       ├── mock_card.c/.h
│       ├── mock_registers.c
│       └── port.c
└── dmsdio_port.dmr
```
## Project Structure

```
dmsdio/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   ├── dmsdio.h
│   ├── dmsdio_port.h
│   └── dmsdio_types.h
├── src/
│   ├── dmsdio.c         # dmdrvi device model
│   ├── dmsdio_card.c    # attach/detach, card generation
│   ├── dmsdio_cmd.c     # command layer, errno mapping
│   ├── dmsdio_config.c  # ini configuration
│   ├── dmsdio_decode.c  # CID/CSD/SCR/SD Status decoders
│   ├── dmsdio_detect.c  # card detect + dmosi presence worker
│   ├── dmsdio_ident.c   # identification and bus negotiation
│   ├── dmsdio_io.c      # byte-offset I/O, read-modify-write
│   ├── dmsdio_xfer.c    # block transfers, erase, retry/recovery
│   └── port/
├── tests/
│   ├── CMakeLists.txt
│   └── dmsdio_test.c
├── CMakeLists.txt
├── Makefile
├── dmsdio.dmr
└── manifest.dmm
```

## Author

Patryk Kubiak

## License

MIT
