#include "chain_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> /* ssize_t */

/* =============================================================
 * Internal helpers
 * ============================================================= */

static size_t __str_len(const char* str) {
    return str ? strlen(str) : 0;
}

/* Always alloc n+1 bytes and null-terminate.
 * If n == 0 we still return a valid allocated empty string ("").
 * Returns NULL on malloc failure.
 */
static char* __chain_alloc_copy(const char* src, size_t n) {
    char* p = malloc(n + 1);
    if (!p) return NULL;
    if (n && src) memcpy(p, src, n);
    p[n] = '\0';
    return p;
}

static void __chain_invalidate_cache(chain* s) {
    if (!s) return;
    if (s->cached_flat) {
        free(s->cached_flat);
        s->cached_flat = NULL;
        s->cached_len = 0;
    }
}

/* Create new patch node. Parent's refcount is incremented on success.
 * Returns NULL on allocation failure.
 */
static chain* __chain_new_patch(chain* parent, size_t idx,
                                const char* data, size_t len,
                                size_t replaced_len, bool is_delete)
{
    if (!parent) return NULL;
    __chain_invalidate_cache(parent);

    chain* c = calloc(1, sizeof(chain));
    if (!c) return NULL;

    size_t parent_len = chain_len(parent);

    c->kind = PATCH;
    c->refcount = 1;
    chain_ref(parent);                  /* increment parent's refcount for this node */
    c->patch.parent = parent;
    c->patch.idx = idx;
    c->patch.len = len;
    c->patch.replaced_len = replaced_len;

    /* clamp idx */
    if (c->patch.idx > parent_len)
        c->patch.idx = parent_len;

    /* clamp replaced_len to available bytes at idx */
    if (c->patch.replaced_len > parent_len - c->patch.idx)
        c->patch.replaced_len = parent_len - c->patch.idx;

    c->patch.delta = (ssize_t)c->patch.len - (ssize_t)c->patch.replaced_len;
    c->patch.is_delete = is_delete;
    if (!is_delete && len > 0)
        c->patch.data = __chain_alloc_copy(data, len);
    else
        c->patch.data = NULL;

    return c;
}

/* =============================================================
 * Public API
 * ============================================================= */

chain* to_chain(const char* cstr) {
    size_t len = __str_len(cstr);

    chain* s = calloc(1, sizeof(chain));
    if (!s) return NULL;

    s->kind = BASE;
    s->refcount = 1;
    s->base.len = len;
    s->base.data = __chain_alloc_copy(cstr, len);
    s->cached_flat = NULL;
    s->cached_len = 0;

    return s;
}

chain* chain_ref(chain* s) {
    if (s) s->refcount++;
    return s;
}

void chain_drop(chain** s) {
    if (!s || !*s) return;
    chain* c = *s;
    if (c->refcount == 0) {
        /* defensive: shouldn't happen, but avoid underflow */
        *s = NULL;
        return;
    }
    if (--c->refcount == 0) {
        __chain_invalidate_cache(c);
        if (c->kind == BASE) {
            free(c->base.data);
        } else {
            free(c->patch.data);
            /* drop parent's reference held by this patch */
            chain_drop(&c->patch.parent);
        }
        free(c);
    }
    *s = NULL;
}

size_t chain_len(const chain* s) {
    if (!s) return 0;
    if (s->cached_flat) return s->cached_len;

    ssize_t len = 0;
    const chain* cur = s;
    while (cur) {
        if (cur->kind == BASE) {
            len += (ssize_t)cur->base.len;
            break;
        }
        len += cur->patch.delta;
        cur = cur->patch.parent;
    }
    return (size_t)(len < 0 ? 0 : len);
}

/* Build a flattened, newly-allocated C string representing chain 's'.
 * Caller must free returned pointer. Returns NULL on failure.
 */
char* chain_read(const chain* s) {
    if (!s) return NULL;

    /* fast cache path */
    if (s->cached_flat) {
        size_t len = s->cached_len;
        char* copy = malloc(len + 1);
        if (!copy) return NULL;
        memcpy(copy, s->cached_flat, len + 1);
        return copy;
    }

    /* find base */
    const chain* cur = s;
    while (cur && cur->kind == PATCH) cur = cur->patch.parent;
    if (!cur || cur->kind != BASE) return NULL;
    const chain* base = cur;

    /* collect patches from s down to (but not including) base */
    int np = 0;
    cur = s;
    while (cur && cur->kind == PATCH) {
        np++;
        cur = cur->patch.parent;
    }

    /* allocate array of patch pointers (oldest -> newest) */
    chain** patches = NULL;
    if (np > 0) {
        patches = malloc((size_t)np * sizeof(chain*));
        if (!patches) return NULL;
        cur = s;
        for (int i = np - 1; i >= 0; i--) {
            patches[i] = (chain*)cur; /* cast away const: we won't modify node */
            cur = cur->patch.parent;
        }
    }

    /* compute required buffer sizes: current length and maximum intermediate length */
    size_t cur_len = base->base.len;
    size_t max_len = cur_len;
    ssize_t running = (ssize_t)cur_len;
    for (int i = 0; i < np; i++) {
        running += patches[i]->patch.delta;
        if (running < 0) running = 0;
        if ((size_t)running > max_len) max_len = (size_t)running;
    }

    /* allocate working buffer of max_len + 1 */
    char* buf = malloc(max_len + 1);
    if (!buf) { free(patches); return NULL; }

    /* start with base content */
    if (base->base.len)
        memcpy(buf, base->base.data, base->base.len);
    cur_len = base->base.len;

    /* apply patches oldest -> newest */
    for (int i = 0; i < np; i++) {
        chain* p = patches[i];
        size_t start = p->patch.idx;
        if (start > cur_len) start = cur_len;
        size_t old_len = p->patch.replaced_len;
        if (old_len > cur_len - start) old_len = cur_len - start;
        size_t new_len = p->patch.len;

        size_t tail_start = start + old_len;
        size_t tail_len = (tail_start <= cur_len) ? (cur_len - tail_start) : 0;

        /* move tail to its new position (may overlap -> use memmove) */
        if (tail_len > 0) {
            memmove(buf + start + new_len, buf + tail_start, tail_len);
        }

        /* copy inserted data (if any and not a delete) */
        if (!p->patch.is_delete && new_len > 0 && p->patch.data) {
            memcpy(buf + start, p->patch.data, new_len);
        }

        cur_len = cur_len - old_len + new_len;
    }

    /* free patches array */
    free(patches);

    /* null terminate at true length */
    buf[cur_len] = '\0';
    size_t final_len = cur_len;

    /* if desirable, cache flattened string on the node 's' (if shared or short chain) */
    if (((chain*)s)->refcount >= 2 || np <= 4) {
        char* ret = malloc(final_len + 1);
        if (!ret) { free(buf); return NULL; }
        memcpy(ret, buf, final_len + 1);

        /* try to allocate cache; if it fails, we still return the ret buffer */
        char* cache_buf = malloc(final_len + 1);
        if (cache_buf) {
            memcpy(cache_buf, ret, final_len + 1);
            /* store cache on node s (we must cast away const) */
            ((chain*)s)->cached_flat = cache_buf;
            ((chain*)s)->cached_len = final_len;
        }

        free(buf);
        return ret;
    }

    /* no caching — return exactly-sized buffer */
    char* ret = malloc(final_len + 1);
    if (!ret) { free(buf); return NULL; }
    memcpy(ret, buf, final_len + 1);
    free(buf);
    return ret;
}

/* full replace */
chain* chain_mod(chain* s, const char* new_content) {
    if (!s) return to_chain(new_content);
    size_t new_len = __str_len(new_content);
    size_t old_len = chain_len(s);
    return __chain_new_patch(s, 0, new_content, new_len, old_len, false);
}

/* general insert/delete/replace */
chain* chain_fmod(chain* s, cmod mode, const char* content, size_t pos, size_t len) {
    if (!s) {
        if (mode == DELETE) return to_chain("");
        return to_chain(content ? content : "");
    }

    size_t parent_len = chain_len(s);
    if (pos > parent_len) pos = parent_len;

    size_t insert_len = content ? strlen(content) : 0;
    size_t replaced_len = 0;
    const char* insert_data = content;
    bool is_delete = false;

    switch (mode) {
        case INSERT:
            replaced_len = 0;
            break;
        case REPLACE:
            replaced_len = len;
            if (pos + replaced_len > parent_len)
                replaced_len = parent_len - pos;
            break;
        case DELETE:
            replaced_len = len;
            if (pos + replaced_len > parent_len)
                replaced_len = parent_len - pos;
            insert_len = 0;
            insert_data = "";          /* pass non-NULL so new patch can be created with empty data */
            is_delete = true;
            break;
        default:
            return chain_ref(s);
    }

    return __chain_new_patch(s, pos, insert_data, insert_len, replaced_len, is_delete);
}

chain* chain_copy(const chain* s) {
    char* flat = chain_read(s);
    if (!flat) return NULL;
    chain* c = to_chain(flat);
    free(flat);
    return c;
}

bool chain_ccpy(const chain* s, const char* cstr) {
    if (!s || !cstr) return false;
    char* a = chain_read(s);
    if (!a) return false;
    bool eq = (strcmp(a, cstr) == 0);
    free(a);
    return eq;
}

bool chain_cpy(const chain* a, const chain* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (chain_len(a) != chain_len(b)) return false;
    char* sa = chain_read(a);
    char* sb = chain_read(b);
    if (!sa || !sb) { free(sa); free(sb); return false; }
    bool eq = (memcmp(sa, sb, chain_len(a)) == 0);
    free(sa); free(sb);
    return eq;
}

/* =============================================================
 * Debug helpers
 * ============================================================= */

static void __print_bytes(const char* d, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)d[i];
        if (c >= 32 && c < 127) putchar(c);
        else printf("\\x%02X", c);
    }
}

static void __chain_debug_rec(const chain* c, size_t depth, int last) {
    for (size_t i = 0; i < depth; i++) printf("   ");
    printf(last ? "└── " : "├── ");

    if (c->kind == BASE) {
        printf("BASE(\"");
        __print_bytes(c->base.data ? c->base.data : "", c->base.len);
        printf("\")\n");
        return;
    }

    printf("PATCH(idx=%zu, ins=%zu, del=%zu, \"",
           c->patch.idx, c->patch.len, c->patch.replaced_len);
    __print_bytes(c->patch.data ? c->patch.data : "", c->patch.len);
    printf("\")\n");

    __chain_debug_rec(c->patch.parent, depth + 1, 1);
}

void __chain_print_debug(const chain* c) {
    if (!c) {
        fprintf(stderr, "chain_debug_print: NULL\n");
        return;
    }
    printf("FINAL(\"");
    char* flat = chain_read(c);
    if (flat) {
        __print_bytes(flat, chain_len(c));
        printf("\")\n");
        free(flat);
    } else {
        printf("NULL\")\n");
    }
    __chain_debug_rec(c, 0, 1);
}
