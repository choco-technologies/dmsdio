# DMINI(3)

## NAME

dmini - DMOD INI File Parser Module

## SYNOPSIS

```c
#include "dmini.h"

dmini_context_t dmini_create(void);
dmini_context_t dmini_create_with_token(unsigned int owner_token);
void dmini_destroy(dmini_context_t ctx);

int dmini_parse_string(dmini_context_t ctx, const char* data);
int dmini_parse_file(dmini_context_t ctx, const char* filename);

int dmini_generate_string(dmini_context_t ctx, char* buffer, size_t buffer_size);
int dmini_generate_file(dmini_context_t ctx, const char* filename);

const char* dmini_get_string(dmini_context_t ctx, const char* section, 
                              const char* key, const char* default_value);
int dmini_get_int(dmini_context_t ctx, const char* section, 
                  const char* key, int default_value);

int dmini_set_string(dmini_context_t ctx, const char* section, 
                     const char* key, const char* value);
int dmini_set_int(dmini_context_t ctx, const char* section, 
                  const char* key, int value);

int dmini_has_section(dmini_context_t ctx, const char* section);
int dmini_has_key(dmini_context_t ctx, const char* section, const char* key);

int dmini_remove_section(dmini_context_t ctx, const char* section);
int dmini_remove_key(dmini_context_t ctx, const char* section, const char* key);

int dmini_set_active_section(dmini_context_t ctx, const char* section,
                              unsigned int owner_token);
int dmini_clear_active_section(dmini_context_t ctx, unsigned int owner_token);

int dmini_section_count(dmini_context_t ctx);
const char* dmini_section_name(dmini_context_t ctx, int index);
int dmini_key_count(dmini_context_t ctx, const char* section);
const char* dmini_key_name(dmini_context_t ctx, const char* section, int index);
```

## DESCRIPTION

The **dmini** module provides a lightweight INI file parser and generator 
optimized for embedded systems. It uses only DMOD SAL (System Abstraction 
Layer) functions and implements memory-efficient line-by-line file I/O 
operations.

### INI File Format

INI files consist of sections, key-value pairs, and comments:

* Sections are defined by `[section_name]`
* Key-value pairs are defined by `key=value`
* Comments start with `;` or `#`
* Whitespace is automatically trimmed from keys and values
* Keys without section headers belong to the global section

### Context Management

**dmini_create()** creates a new INI context for storing sections and 
key-value pairs. Returns a context pointer or NULL on error.

**dmini_create_with_token()** creates a new INI context protected by a magic 
number token. The token must be supplied to **dmini_set_active_section()** and 
**dmini_clear_active_section()** when a non-zero token is used. Passing token 
value 0 is equivalent to calling **dmini_create()**.

**dmini_destroy()** frees all memory associated with an INI context.

### Parsing

**dmini_parse_string()** parses an INI file from a null-terminated string. 
Returns DMINI_OK on success or an error code on failure.

**dmini_parse_file()** parses an INI file from a file path using SAL file 
functions. Uses line-by-line reading with 256-byte buffers. Returns DMINI_OK 
on success or an error code on failure.

### Generation

**dmini_generate_string()** generates an INI file string from the context. 
If buffer is NULL, returns the required buffer size. If buffer is not NULL, 
fills it with the INI data. Returns required buffer size or a negative error 
code.

**dmini_generate_file()** generates an INI file from the context and writes 
it directly to a file using line-by-line writing. Returns DMINI_OK on success 
or an error code on failure.

### Data Access

**dmini_get_string()** retrieves a string value for the given section and key. 
Pass NULL for section to access the global section. Returns the value string 
or default_value if not found.

**dmini_get_int()** retrieves an integer value for the given section and key. 
Pass NULL for section to access the global section. Returns the integer value 
or default_value if not found.

**dmini_set_string()** sets a string value for the given section and key. 
Creates the section if it doesn't exist. Pass NULL for section to access the 
global section. Returns DMINI_OK on success or an error code on failure.

**dmini_set_int()** sets an integer value for the given section and key. 
Creates the section if it doesn't exist. Pass NULL for section to access the 
global section. Returns DMINI_OK on success or an error code on failure.

### Queries

**dmini_has_section()** checks if a section exists in the context. Returns 1 
if the section exists, 0 otherwise.

**dmini_has_key()** checks if a key exists in the specified section. Pass NULL 
for section to check the global section. Returns 1 if the key exists, 0 
otherwise.

### Removal

**dmini_remove_section()** removes an entire section and all its keys from the 
context. Returns DMINI_OK on success or an error code on failure.

**dmini_remove_key()** removes a single key from the specified section. Pass 
NULL for section to remove from the global section. Returns DMINI_OK on 
success or an error code on failure.

### Iteration

**dmini_section_count()** returns the total number of sections in the context,
including the global (unnamed) section. Respects the active-section restriction
when it is in effect. Returns DMINI_ERR_INVALID if ctx is NULL.

**dmini_section_name()** returns the name of the section at the given
zero-based index. The global (unnamed) section is represented by NULL. Returns
NULL when the index is out of range. Use dmini_section_count() to determine
the valid range. Respects the active-section restriction when it is in effect.

**dmini_key_count()** returns the number of key-value pairs in the specified
section. Pass NULL for section to query the global section. Returns
DMINI_ERR_INVALID if ctx is NULL, or DMINI_ERR_NOT_FOUND if the section does
not exist. Respects the active-section restriction when it is in effect.

**dmini_key_name()** returns the name of the key at the given zero-based index
within the specified section. Returns NULL when the index is out of range or
the section does not exist. Use dmini_key_count() to determine the valid range.
Respects the active-section restriction when it is in effect.

### Section Visibility Restriction

**dmini_set_active_section()** restricts the context so that only the named 
section is visible to consumers of this context. While the restriction is 
active, all API calls treat `section == NULL` as a reference to the active 
section, and any attempt to access a different section returns not-found / the 
default value. Pass NULL as section to restrict to the global (unnamed) 
section. If the context was created with a non-zero token, the matching token 
must be supplied; otherwise DMINI_ERR_LOCKED is returned.

**dmini_clear_active_section()** removes the active-section restriction so 
that the full content of the context becomes visible again. Requires the 
correct token when the context was created with a non-zero token.

## RETURN VALUES

Functions return the following error codes:

* **DMINI_OK** (0) - Success
* **DMINI_ERR_GENERAL** (-1) - General error
* **DMINI_ERR_MEMORY** (-2) - Memory allocation error
* **DMINI_ERR_INVALID** (-3) - Invalid parameter
* **DMINI_ERR_NOT_FOUND** (-4) - Section or key not found
* **DMINI_ERR_FILE** (-5) - File I/O error
* **DMINI_ERR_LOCKED** (-6) - Wrong owner token supplied to set/clear active section

## EXAMPLES

### Basic Usage

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

// Generate to file
dmini_generate_file(ctx, "output.ini");

// Cleanup
dmini_destroy(ctx);
```

### Querying Buffer Size

```c
// Query required buffer size
int size = dmini_generate_string(ctx, NULL, 0);

// Allocate buffer
char* buffer = Dmod_Malloc(size);

// Generate INI to buffer
dmini_generate_string(ctx, buffer, size);

// Use buffer...

// Free buffer
Dmod_Free(buffer);
```

### Working with Global Section

```c
// Set global key (no section)
dmini_set_string(ctx, NULL, "global_key", "global_value");

// Get global key
const char* val = dmini_get_string(ctx, NULL, "global_key", "default");
```

### Section Visibility Restriction

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
const char* db = dmini_get_string(ctx, "database", "host", "n/a");
// db == "n/a"

// Wrong token returns DMINI_ERR_LOCKED
int rc = dmini_clear_active_section(ctx, 0x12345678);
// rc == DMINI_ERR_LOCKED

// Correct token clears the restriction
dmini_clear_active_section(ctx, 0xDEADBEEF);

dmini_destroy(ctx);
```

## MEMORY FOOTPRINT

* **dmini library**: 536B RAM, 5KB ROM
* **Line buffer**: 256 bytes (temporary, not persistent)

## SEE ALSO

dmod(3), dmod_loader(1)

## AUTHOR

Patryk Kubiak

## LICENSE

MIT License - Copyright (c) 2025 Choco-Technologies
