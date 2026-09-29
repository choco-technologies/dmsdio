#ifndef DMDRVI_TYPES_H
#define DMDRVI_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Signed byte offset used by dmdrvi 2.x. */
typedef int64_t dmdrvi_offset_t;

/** Unsigned device or range size used by dmdrvi 2.x. */
typedef uint64_t dmdrvi_size_t;

/**
 * Signed I/O result. Non-negative values are transferred byte counts and
 * negative values are errno-compatible failures.
 */
typedef int64_t dmdrvi_ssize_t;

#ifdef __cplusplus
}
#endif

#endif /* DMDRVI_TYPES_H */
