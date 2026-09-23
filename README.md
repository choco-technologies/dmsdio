# dmsdio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmsdio/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmsdio/actions/workflows/ci.yml)

dmsdio DMOD library module.

## Description

TODO: describe what this module does.

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

Tests are built automatically alongside the module (see `tests/`). Once built,
run them with `ctest`:

```bash
cd build
ctest --output-on-failure
```

`ctest` installs the test module's dependencies with `dmf-get` and then runs
it through `dmod_loader`. To run it manually instead:

```bash
export DMOD_DMF_DIR=$(pwd)/build/dmf
dmf-get install -d ${DMOD_DMF_DIR}/test_dmsdio-local.dmd -y
dmod_loader build/dmf/test_dmsdio.dmf
```

## Usage

<TBD>

This library module provides functions that can be used by other modules:

```c
#include "dmsdio.h"
```

## API

| Function | Description |
|----------|-------------|
| `dmsdio_create()` | Create a new `dmsdio_t` instance. |
| `dmsdio_destroy()` | Destroy an instance created by `_create()`. |
| `dmsdio_is_valid()` | Check whether a handle is a valid instance. |

See [include/dmsdio.h](include/dmsdio.h) for the full
declarations and [docs/api-reference.md](docs/api-reference.md) for the
complete reference.

## Documentation

See the `docs/` directory:

- **[api-reference.md](docs/api-reference.md)** - Complete API documentation

View documentation using `dmf-man dmsdio`.

## Hardware Port

This module ships two DMOD modules: the architecture-independent
`dmsdio` and `dmsdio_port`, which contains the
architecture-specific implementation. The active architecture is selected via
`DMOD_CPU_FAMILY` (default: `stm32f7`):

```bash
cmake .. -DDMOD_CPU_FAMILY=stm32f7
```

See [docs/port-implementation.md](docs/port-implementation.md) for how to add
another architecture. Port-specific files:

```
├── include/dmsdio_port.h
├── src/port/
│   ├── CMakeLists.txt
│   └── stm32f7/
│       ├── config.cmake
│       └── port.c
└── dmsdio_port.dmr
```
## Project Structure

```
dmsdio/
├── docs/              # Documentation (markdown format)
├── include/           # Public headers
│   └── dmsdio.h
├── src/
│   └── dmsdio.c
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
