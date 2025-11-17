#ifndef __CHAIN_INTERNAL_H__
#define __CHAIN_INTERNAL_H__

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h> /* for ssize_t */

#include "../include/chain.h"

typedef enum { BASE, PATCH } kind_t;

struct chain {
    kind_t kind;
    size_t refcount;

    char*  cached_flat;
    size_t cached_len;

    union {
        struct {
            char*  data;
            size_t len;
        } base;

        struct {
            chain* parent;
            size_t idx;
            char*  data;
            size_t len;
            size_t replaced_len;
            ssize_t delta;
            bool is_delete;
        } patch;
    };
};

/* internal helpers (implemented in src/chain.c) */
static size_t   __str_len(const char* str);
static void     __chain_invalidate_cache(chain* s);
static char*    __chain_alloc_copy(const char* src, size_t n);
static chain*   __chain_new_patch(chain* parent, size_t i, const char* data, size_t len, size_t replaced_len, bool is_delete);

#endif // __CHAIN_INTERNAL_H__
