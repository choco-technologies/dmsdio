# dmsdio Documentation

SD memory card driver (SDSC v1/v2, SDHC, SDXC) exposing a 64-bit block
device through dmdrvi 2.0, monitored through the dmdrvi 2.1 monitor contract.

## Contents

- **[api-reference.md](api-reference.md)** - device nodes, I/O semantics, ioctls, errors, types
- **[configuration.md](configuration.md)** - ini keys, card detect, presence monitoring
- **[port-implementation.md](port-implementation.md)** - port API contract and adding a CPU family

## Quick Reference

```c
#include "dmsdio.h"     /* register decoders, dmsdio_types.h */
```

```text
/dev/dmsdio0      host node  (status, monitor ioctls)
/dev/dmsdio0/0    card node  (block device, hot-plugged)
```

View documentation using `dmf-man`:

```bash
dmf-man dmsdio          # Main documentation
dmf-man dmsdio api      # API reference
```
