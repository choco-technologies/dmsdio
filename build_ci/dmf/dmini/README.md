# dmini - DMOD INI File Parser Module

A DMOD module library for parsing and generating INI configuration files, optimized for embedded systems.

## Overview

dmini is a lightweight INI file parser/generator module designed for embedded systems with limited RAM. It uses only SAL (System Abstraction Layer) functions from DMOD and implements memory-efficient line-by-line file I/O operations.

## Features

- **INI File Parsing**: Read and parse INI files with sections, key-value pairs, and comments
- **INI File Generation**: Create INI files from in-memory data structures
- **Memory Efficient**: Line-by-line file I/O with 256-byte buffers (no large allocations)
- **User-Controlled Buffers**: Generate functions accept user-provided buffers to prevent memory leaks
- **SAL-Only**: Uses only DMOD SAL functions (Dmod_Malloc, Dmod_Free, Dmod_StrDup, etc.)
- **Global Section Support**: Handle keys without section headers
- **Comment Support**: Parse comments starting with `;` or `#`
- **Whitespace Trimming**: Automatic trimming of keys and values
- **Section Visibility Restriction**: Limit the visible scope of a context to a single section, with optional token-based protection

## API

### Context Management
- `dmini_create()` - Create INI context
- `dmini_create_with_token(owner_token)` - Create INI context protected by an owner token
- `dmini_destroy()` - Free INI context

### Parsing
- `dmini_parse_string(ctx, data)` - Parse INI from string
- `dmini_parse_file(ctx, filename)` - Parse INI from file (line-by-line)

### Generation
- `dmini_generate_string(ctx, buffer, size)` - Generate INI to buffer (returns required size if buffer is NULL)
- `dmini_generate_file(ctx, filename)` - Generate INI directly to file (line-by-line)

### Data Access
- `dmini_get_string(ctx, section, key, default)` - Get string value
- `dmini_get_int(ctx, section, key, default)` - Get integer value
- `dmini_set_string(ctx, section, key, value)` - Set string value
- `dmini_set_int(ctx, section, key, value)` - Set integer value

### Queries
- `dmini_has_section(ctx, section)` - Check if section exists
- `dmini_has_key(ctx, section, key)` - Check if key exists

### Iteration
- `dmini_section_count(ctx)` - Get number of sections (including global)
- `dmini_section_name(ctx, index)` - Get section name at index (NULL for global section)
- `dmini_key_count(ctx, section)` - Get number of keys in a section
- `dmini_key_name(ctx, section, index)` - Get key name at index within a section

### Section Visibility Restriction
- `dmini_set_active_section(ctx, section, owner_token)` - Restrict the context to a single section
- `dmini_clear_active_section(ctx, owner_token)` - Remove the section restriction

### Removal
- `dmini_remove_section(ctx, section)` - Remove entire section
- `dmini_remove_key(ctx, section, key)` - Remove single key

## Usage Example

```c
#include "dmini.h"

// Create context
dmini_context_t ctx = dmini_create();

// Parse INI file
dmini_parse_file(ctx, "config.ini");

// Read values
const char* host = dmini_get_string(ctx, "database", "host", "localhost");
int port = dmini_get_int(ctx, "database", "port", 5432);

// Modify values
dmini_set_string(ctx, "cache", "enabled", "true");
dmini_set_int(ctx, "cache", "size", 1024);

// Generate to buffer (query size first)
int size = dmini_generate_string(ctx, NULL, 0);
char* buffer = malloc(size);
dmini_generate_string(ctx, buffer, size);

// Or generate directly to file
dmini_generate_file(ctx, "output.ini");

// Cleanup
dmini_destroy(ctx);
```

## Section Visibility Restriction

The section visibility restriction feature allows a context to be locked to a
single section. While the restriction is active all API calls treat
`section == NULL` as a reference to the active section, and any attempt to
access a different section returns not-found / the default value.

This is useful when you want to pass a context to a component that should only
see one section of a larger configuration file.

An optional **owner token** can be set at creation time so that only the
creator can change or clear the restriction.

```c
// Create a context protected by a token
dmini_context_t ctx = dmini_create_with_token(0xDEADBEEF);

dmini_parse_file(ctx, "config.ini");

// Restrict visibility to the "network" section
dmini_set_active_section(ctx, "network", 0xDEADBEEF);

// All NULL-section calls now resolve to "network"
const char* host = dmini_get_string(ctx, NULL, "host", "localhost");
int port         = dmini_get_int(ctx, NULL, "port", 80);

// Accessing any other section returns not-found / default
const char* val = dmini_get_string(ctx, "database", "host", "n/a");
// val == "n/a"

// Remove the restriction (requires the correct token)
dmini_clear_active_section(ctx, 0xDEADBEEF);

dmini_destroy(ctx);
```

If a wrong token is supplied to `dmini_set_active_section()` or
`dmini_clear_active_section()` the function returns `DMINI_ERR_LOCKED (-6)`.
A token value of `0` disables token protection (any caller may change the
active section).

## Building

```bash
mkdir build
cd build
cmake .. -DDMOD_MODE=DMOD_MODULE
cmake --build .
```

This generates:
- `dmf/dmini.dmf` - The INI parser library module (536B RAM, 5KB ROM)
- `dmf/test_dmini.dmf` - Test application (432B RAM, 7KB ROM)

## Testing

The test application (`test_dmini.dmf`) runs comprehensive tests covering all API functions:

```bash
dmod_loader dmf/dmini.dmf dmf/test_dmini.dmf
```

Test coverage includes:
- Context creation/destruction
- String parsing with sections and keys
- Setting and getting values
- Section/key existence checks
- Removal operations
- Buffer generation with size queries
- File I/O operations
- Comments and whitespace handling
- Section visibility restriction (active section with and without token protection)

## INI File Format

```ini
; Comment line
global_key=global_value

[section1]
key1=value1
key2=value2

[section2]
number=42
name=test
```

- Sections are defined by `[section_name]`
- Key-value pairs: `key=value`
- Comments start with `;` or `#`
- Whitespace is automatically trimmed
- Global section for keys without section headers

## Memory Footprint

- **dmini library**: 536B RAM, 5KB ROM
- **test_dmini application**: 432B RAM, 7KB ROM

## License

MIT License - see LICENSE file for details

