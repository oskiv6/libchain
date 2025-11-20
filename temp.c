#ifndef CHAIN_H
#define CHAIN_H

#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>


#ifdef __cplusplus
extern "C" {
#endif

/*




*/

#define CHAIN_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "chain assertion failed: %s", (msg));\
            fprintf(stderr, " at %s:%d (%s)\n", __FILE__, __LINE__, __func__ );\
            abort();\
        }\
    } while (0)

typedef struct chain chain;

typedef enum {
    INSERT,
    REPLACE,
    DELETE
} cmod;

// create chain from string
chain* to_chain(const char* s);

// general modification functions
chain* chain_mod(chain* c, const char* new_content);
chain* chain_fmod(chain* c, cmod f, const char* insert, size_t pos, size_t len);

// translate into string (allocated on arena, freed after dropping chain)
char* chain_stringify(chain* c);

// final text line
size_t chain_len(const chain* c);

// TODO: chain node count
// size_t chain_count(const chain* c);

// snapshots / copying
chain* chain_snapshot(chain* c, size_t version);
chain* chain_copy(chain* c);

// comparison
bool chain_ccmp(chain* c, const char* cstr);
bool chain_cmp(chain* a, chain* b);

// freeing the chain
void chain_drop(chain* c);

// DEBUG
// hide when -DCHAIN_DEBUG not found
void __chain_print_debug(chain* c);

// PROFILING
double __get_strlen_time(void);

#ifdef __cplusplus
}
#endif


/*

implemnetation below

*/

//#ifdef CHAIN_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

// internal structures
typedef struct {
    char* start;
    char* ptr;
    char* end;
    size_t capacity;
    bool is_arena_used;
} chain_arena;

typedef struct {
    size_t idx;
    char*  data;
    size_t len;
    size_t replaced_len;
    ssize_t delta;
    bool is_delete;
    size_t refcount;
} patch;

struct chain {
    char* base_str;
    size_t base_len;

    /*

    patches and data are stored inside chain's arena, thus freeing patches independently
    is not possible. Each patch contains its own reference count, if the patch
    is not referenced anymore refcount drops to 0.

    arena can be used only by the original chain, if the chain is dropped, the arena
    tag the arena is unused.

    arena itself can be freed only when:
        1. is_arena_used == false
        2. each patch refcount == 0
    otherwise, the arena awaits dropping the refcount by being placed in the free queue.

    *required by snapshot logic

    */
    chain_arena ar;
    bool is_arena_owner_alive;      // is the chain alive?

    patch* patches;
    size_t patch_count;
    size_t patch_cap;

    size_t final_length;
};



/*

internals forward declaration

internals should be declared starting with '__' (double underscore)

*/

chain* __CHAIN_FREE_QUEUE = NULL;
size_t __CHAIN_QUEUE_ITEM_COUNT = 0;


static void* __chain_alloc(chain* c, size_t size);
static void  __chain_free(chain* c);
static void __check_free_queue();
static void  __grow_patches(chain* c);
static size_t __str_len(const char* str);
//static void __chain_ensure_unique_patches(chain* c);


static chain* __chain_add_patch(chain* c, size_t idx, const char* content,
    size_t ins_len, size_t repl_len, bool is_delete);


// optimization
// TODO: hide when -DCHAIN_OPTIMIZATION not found
static double __str_len_time = 0.0;


// C string length calculation
static size_t __str_len(const char* str) {
    CHAIN_ASSERT(str, "NULL string");

    typedef size_t word;
    const char* s = str;

    const size_t wsize = sizeof(word);
    const uintptr_t wmask = (uintptr_t)wsize - 1;

    #ifdef TRACK_STRLEN_TIME
        clock_t st = clock();
    #endif

    while (((uintptr_t)s & wmask) != 0) {
        if (*s == 0) {
        #ifdef TRACK_STRLEN_TIME
            __str_len_time += (double)(clock() - st) / CLOCKS_PER_SEC;
        #endif
            return (size_t)(s - str);
        }
        s++;
    }

    const word lomagic = (word)(~(word)0) / 0xFF;
    const word himagic = lomagic << 7;

    const word* wp = (const word*)s;

    for (;;) {
        word v = *wp;
        if (((v - lomagic) & ~v & himagic) != 0) {
            const char* cp = (const char*)wp;
            for (size_t i = 0; i < wsize; i++) {
                if (cp[i] == 0) {
                #ifdef TRACK_STRLEN_TIME
                    __str_len_time += (double)(clock() - st) / CLOCKS_PER_SEC;
                #endif
                    return (size_t)((cp + i) - str);
                }
            }
        }
        wp++;
    }
}


size_t chain_len(const chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    // VER 2
    return (c->final_length > 0)? c->final_length : 0;
}


double __get_strlen_time(void) {
    return __str_len_time;
}


// arena allocator
static void* __chain_alloc(chain* c, size_t size) {
    CHAIN_ASSERT(c, "NULL chain");

    size = (size + 7) & ~7; // align

    if (c->ar.ptr + size > c->ar.end) {
        size_t old_used = c->ar.ptr - c->ar.start;
        size_t new_cap = c->ar.capacity * 2;
        while (new_cap < old_used + size) new_cap *= 2;

        char* newmem = malloc(new_cap);
        CHAIN_ASSERT(newmem, "allocation failed");
        memcpy(newmem, c->ar.start, old_used);
        free(c->ar.start);

        c->ar.start = newmem;
        c->ar.ptr   = newmem + old_used;
        c->ar.end   = newmem + new_cap;
        c->ar.capacity = new_cap;
    }

    void* p = c->ar.ptr;
    c->ar.ptr += size;
    return p;
}


static void __chain_free(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    free(c->ar.start);
}

static void __check_free_queue() {
    if (__CHAIN_QUEUE_ITEM_COUNT == 0) return;

    size_t write_idx = 0; // new position for surviving items

    for (size_t i = 0; i < __CHAIN_QUEUE_ITEM_COUNT; i++) {
        chain* c = &__CHAIN_FREE_QUEUE[i];

        // skip chains whose arena is still used
        if (c->ar.is_arena_used) {
            __CHAIN_FREE_QUEUE[write_idx++] = *c;
            continue;
        }

        bool patch_in_use = false;
        for (size_t j = 0; j < c->patch_count; j++) {
            patch* p = &c->patches[j];
            if (p->refcount > 0) {
                patch_in_use = true;
                break;
            }
        }

        if (patch_in_use) {
            __CHAIN_FREE_QUEUE[write_idx++] = *c;
            continue;
        }

        // free the chain's arena and patches
        __chain_free(c);
        free(c);
    }

    // update queue count
    __CHAIN_QUEUE_ITEM_COUNT = write_idx;

    // shrink queue array if desired
    __CHAIN_FREE_QUEUE = realloc(__CHAIN_FREE_QUEUE, write_idx * sizeof(chain));
}



// patch management
static void __grow_patches(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    if (c->patch_count < c->patch_cap) return;

    size_t new_cap = c->patch_cap * 2;
    patch* newarr = __chain_alloc(c, new_cap * sizeof(patch));
    memcpy(newarr, c->patches, c->patch_count * sizeof(patch));

    c->patches = newarr;
    c->patch_cap = new_cap;
}


// static void __chain_ensure_unique_patches(chain* c) {
//     CHAIN_ASSERT(c, "NULL chain");

//     if (*c->patch_refcount == 1) return;

//     patch* newarr = __chain_alloc(c, sizeof(patch) * c->patch_cap);
//     memcpy(newarr, c->patches, sizeof(patch) * c->patch_count);

//     (*c->patch_refcount)--;

//     c->patches = newarr;
//     c->patch_refcount = __chain_alloc(c, sizeof(size_t));
//     *c->patch_refcount = 1;
// }


static chain* __chain_add_patch(chain* c, size_t idx, const char* content, size_t ins_len,
        size_t repl_len, bool is_delete) {

    CHAIN_ASSERT(c, "NULL chain");
    if (!is_delete) {
        CHAIN_ASSERT(content, "NULL string");
    }

     //__chain_ensure_unique_patches(c);

    size_t cl = c->final_length;
    if (idx > cl) idx = cl;
    if (repl_len > cl - idx) repl_len = cl - idx;

    __grow_patches(c);

    patch* p = &c->patches[c->patch_count++];
    p->idx = idx;
    p->len = ins_len;
    p->replaced_len = repl_len;
    p->delta = (ssize_t)ins_len - (ssize_t)repl_len;
    p->is_delete = is_delete;

    if (!is_delete && ins_len) {
        p->data = __chain_alloc(c, ins_len + 1);
        memcpy(p->data, content, ins_len);
        p->data[ins_len] = 0;
    } else {
        p->data = NULL;
    }
    c->final_length += p->delta;

    return c;


}


// create chain
chain* to_chain(const char* s) {
    CHAIN_ASSERT(s, "NULL string");

    chain* c = malloc(sizeof(chain));
    memset(c, 0, sizeof(chain));

    // arena init
    c->ar.capacity = 1024;
    c->ar.start = malloc(1024);
    c->ar.ptr = c->ar.start;
    c->ar.end = c->ar.start + 1024;
    c->ar.is_arena_used = true;

    size_t len = strlen(s);

    c->base_str = __chain_alloc(c, len + 1);
    memcpy(c->base_str, s, len + 1);
    c->base_len = len;

    c->patch_cap = 4;
    c->patch_count = 0;
    c->patches = __chain_alloc(c, sizeof(patch) * 4);



    c->final_length = len;

    return c;
}


// general modifications
chain* chain_mod(chain* c, const char* content) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(content, "NULL string");

    size_t new_len = __str_len(content);
    size_t old_len = c->final_length;
    return __chain_add_patch(c, 0, content, new_len, old_len, false);
}


chain* chain_fmod(chain* c, cmod flag, const char* content, size_t pos, size_t l) {
    CHAIN_ASSERT(c, "NULL chain");

    if (!content) {
        if (flag != DELETE) {
            CHAIN_ASSERT(content, "NULL string used without DELETE flag");

        } else {
            return __chain_add_patch(c, 0, "", 0, 0, true);
        }
    }

    size_t cl = c->final_length;
    if (pos > cl) pos = cl;

    size_t ins = 0, del = 0;
    const char* data = content;
    bool is_delete = false;

    switch (flag) {
        case INSERT:
            ins = __str_len(content);
            del = 0;
            break;

        case REPLACE:
            ins = __str_len(content);
            del = l;
            if (pos + del > cl) del = cl - pos;
            break;

        case DELETE:
            ins = 0;
            del = l;
            if (pos + del > cl) del = cl - pos;
            data = "";
            is_delete = true;
            break;

        default:
            abort();
    }

    return __chain_add_patch(c, pos, data, ins, del, is_delete);
}


// translate to string
char* chain_stringify(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    size_t final_len = c->final_length;
    char* out = __chain_alloc(c, final_len + 1);
    if (!out) abort();

    size_t cur_len = c->base_len;
    memcpy(out, c->base_str, c->base_len);

    for (size_t i = 0; i < c->patch_count; i++) {
        const patch* p = &c->patches[i];

        size_t pos = p->idx;
        if (pos > cur_len) pos = cur_len;
        size_t del = p->replaced_len;
        if (del > cur_len - pos) del = cur_len - pos;

        memmove(
            out + pos + p->len,
            out + pos + del,
            cur_len - pos - del
        );

        if (p->len)
            memcpy(out + pos, p->data, p->len);

        cur_len = cur_len - del + p->len;
    }

    out[cur_len] = 0;

    return out;
}


// snapshot / copy
chain* chain_snapshot(chain* c, size_t version) {
    CHAIN_ASSERT(c, "NULL chain");

    chain* s = malloc(sizeof(chain));
    CHAIN_ASSERT(s, "allocation failed");
    memset(s, 0, sizeof(chain));

    // arena init
    s->ar.capacity = 1024;
    s->ar.start = malloc(1024);
    s->ar.ptr = s->ar.start;
    s->ar.end = s->ar.start + 1024;

    s->base_len = c->base_len;
    s->base_str = c->base_str;

    s->patch_cap = version + 2;
    s->patch_count = 0;
    s->patches = __chain_alloc(s, sizeof(patch) * s->patch_cap);

    // compute final_length
    size_t len = c->base_len;
    for (size_t i = 0; i < version && i < c->patch_count; i++) {
        s->patches[i] = c->patches[i];
        len = len - c->patches[i].replaced_len + c->patches[i].len;
    }
    s->patch_count = version;
    s->final_length = len;

    return s;
}


chain* chain_copy(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    char* flat = chain_stringify(c);
    chain* nc = to_chain(flat);
    return nc;
}


// comparisons
bool chain_ccmp(chain* c, const char* s) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(s, "NULL string");

    size_t sl = strlen(s);
    if (sl != chain_len(c)) return false;

    char* flat = chain_stringify(c);
    int eq = memcmp(flat, s, sl) == 0;
    return eq;
}

bool chain_cmp(chain* a, chain* b) {
    CHAIN_ASSERT(a, "NULL chain (first arg)");
    CHAIN_ASSERT(b, "NULL chain (second arg)");

    if (chain_len(a) != chain_len(b)) return false;
    char* af = chain_stringify(a);
    char* bf = chain_stringify(b);
    int eq = memcmp(af, bf, chain_len(a)) == 0;
    return eq;
}

// free chain
void chain_drop(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    __check_free_queue();

    c->ar.is_arena_used = false;

    for (size_t i = 0; i < c->patch_count; i++) {
        patch* p = &c->patches[i];

        if (p->refcount > 0) {

            // whole chain goes into free queue
            __CHAIN_FREE_QUEUE = realloc(__CHAIN_FREE_QUEUE, sizeof(chain) * ++__CHAIN_QUEUE_ITEM_COUNT);
            __CHAIN_FREE_QUEUE[__CHAIN_QUEUE_ITEM_COUNT - 1] = *c;

            return; // cannot free
        }
    }

    // all patch references are 0 and arena is not used
    __chain_free(c);
    free(c);
}

// debug
static void __print_bytes(const char* d, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)d[i];
        if (c >= 32 && c < 127) putchar(c);
        else printf("\\x%02X", c);
    }
}

static void __chain_print_patch(const patch* p, size_t depth) {
    for (size_t i = 0; i < depth; i++) printf("   ");
    printf("└── ");

    if (p->is_delete) {
        printf("PATCH(idx=%zu, del=%zu)\n", p->idx, p->replaced_len);
        return;
    }

    printf("PATCH(idx=%zu, ins=%zu, del=%zu \"",
           p->idx, p->len, p->replaced_len);

    if (p->data)
        __print_bytes(p->data, p->len);

    printf("\")\n");
}

void __chain_print_debug(chain* c) {
    if (!c) {
        fprintf(stderr, "chain_debug_print: NULL\n");
        abort();
    }

    printf("FINAL(\"");
    char* flat = chain_stringify(c);
    if (flat) {
        __print_bytes(flat, chain_len(c));
        printf("\")\n");
        //free(flat);
    } else {
        printf("NULL\")\n");
    }

    // print patches
    for (size_t i = 0; i < c->patch_count; i++) {
        __chain_print_patch(&c->patches[i], i);
    }

    // BASE line
    printf("   └── BASE(\"");
    if (c->base_len && c->base_str)
        __print_bytes(c->base_str, c->base_len);
    printf("\")\n");
}

//#endif /* CHAIN_IMPLEMENTATION */
#endif /* CHAIN_H */

/*
MIT License

Copyright (c) 2025 Oskar Strzelecki

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
