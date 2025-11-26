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
static chain* to_chain(const char* s);

// general modification functions
static chain* chain_mod(chain* c, const char* new_content);
static chain* chain_fmod(chain* c, cmod f, const char* insert, size_t pos, size_t len);

// translate into string (allocated on arena, freed after dropping chain)
static char* chain_stringify(chain* c);

// final text line
static size_t chain_len(const chain* c);

// TODO: chain node count
// size_t chain_count(const chain* c);

// snapshots / copying
static chain* chain_snapshot(chain* c, size_t version);
static chain* chain_copy(chain* c);

// comparison
static bool chain_ccmp(chain* c, const char* cstr);
static bool chain_cmp(chain* a, chain* b);

// freeing the chain
static void chain_drop(chain* c);

// DEBUG
// hide when -DCHAIN_DEBUG not found
static void __chain_print_debug(chain* c);

// PROFILING
static double __get_strlen_time(void);

#ifdef __cplusplus
}
#endif


/*

implemnetation below

*/

#ifdef CHAIN_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

// internal structures

#define __PATCH_MAX_COUNT 32

typedef struct block block;
typedef struct patch patch;


typedef struct chain_arena {
    // memory
    char* start;
    char* ptr;
    char* end;
    size_t capacity;

    // CLEANUP MANAGEMENT
    /*
    true - the arena is meant to be shut down
    false - the arena is still available to use

    in order to make arena always available to free, arena may enter critical zone
    (when the 95% of space is used the critical zone allows for one more allocation
    (if 5% is appropriate), then the arena is tagged as full and the next one is allocated

    */
    bool is_full;                           // is the arena fully allocated?
    float usage;

    /*
    arena is collectible only when `current_alive_count == 0` and `is_full == true`

    */
    bool is_collectible;                    // may the arena already be freed?
    size_t current_alive_count;             // how many data blocks are alive right now?
    /*
        control data, `current_alive_count` cannot be higher than `max_alive_count`,
        `max_alive_count` can be modified to higher value, but it never updates if
        the arena is fully callocated
    */
    size_t max_alive_count;

    // arena chain
    struct chain_arena* next;
} chain_arena;


struct patch {
    char*  data;

    size_t idx;
    size_t len;
    size_t replaced_len;
    ssize_t delta;
    bool is_delete;

    size_t refcount;
    block* parent_block;
    chain_arena* parent_arena;
};


struct block {
    struct block* next;
    size_t count;
    patch items[__PATCH_MAX_COUNT];

    chain_arena* parent_arena;
};


struct chain {
    //char* base_str;
    char* base_data;
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
    chain_arena arena;
    chain_arena* current_arena;

    // patch* patches;                 // patch array
    block* array;
    block* current_array;
    size_t total_patches;

    size_t final_length;
};



/*

internals forward declaration

internals should be declared starting with '__' (double underscore)

*/


// memory management

#define KB(x) ((size_t)x * 1024)
#define MB(x) ((size_t)x * 1024 * 1024)

static size_t __STATIC_ARENA_SIZE = (KB(8)); // 8KB

/*
you can customize the arena size, this allows you to allocate
appropriate arena sizes for your usage needs
*/
static inline void SET_ARENA_SIZE(size_t size) {

    // arena must hold at least __PATCH_MAX_COUNT
    if (size < sizeof(patch) * __PATCH_MAX_COUNT) return;

    __STATIC_ARENA_SIZE = size;
}

#define __ARENA_CRITICAL_PERCENT 98.0

static chain_arena* __arena_new(void);
static void* __arena_alloc(chain* c, size_t size);
static char* __arena_alloc_data(chain* c, size_t size);
static block* __arena_alloc_block(chain* c);
static void __arena_release_patch(chain* c, patch* p);
static void __arena_release_block(chain* c, block* b);
static void __arena_unlink(chain* c, chain_arena* a);
static void __arena_free(chain_arena* a);


static size_t __str_len(const char* str);

static chain* __chain_add_patch(chain* c, size_t idx, const char* content,
    size_t ins_len, size_t repl_len, bool is_delete);


//static void __chain_ensure_unique_patches(chain* c);


// profiling data

// IMPL

static size_t chain_len(const chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    // VER 2
    return (c->final_length > 0)? c->final_length : 0;
}


// create chain
static chain* to_chain(const char* s) {
    CHAIN_ASSERT(s, "NULL string");

    #ifdef CHAIN_PROFILER
        clock_t st, et;
        st = clock();
    #endif

    chain* c = (chain*)malloc(sizeof(chain));
    CHAIN_ASSERT(c, "malloc failed");
    memset(c, 0, sizeof(chain));

    // primary arena (buffer + struct fields)
    c->arena.next = NULL;

    // primary arena
    c->arena.start = (char*)malloc(__STATIC_ARENA_SIZE);
    CHAIN_ASSERT(c->arena.start, "arena malloc failed");

    c->arena.capacity = __STATIC_ARENA_SIZE;
    c->arena.ptr = c->arena.start;
    c->arena.end = c->arena.start + __STATIC_ARENA_SIZE;

    c->arena.is_full = false;
    c->arena.is_collectible = false;
    c->arena.current_alive_count = 0;
    c->arena.max_alive_count = 0;

    c->current_arena = &c->arena;

    // primary arena
    block* b = __arena_alloc_block(c);
    CHAIN_ASSERT(b, "block allocation failed");
    b->count = 0;
    c->array = b;
    c->current_array = b;
    c->total_patches = 0;
    b->next = NULL;


    // base string, it is embedded into chain structure
    size_t len = __str_len(s);
    c->base_data = (char*)__arena_alloc(c, len + 1);
    CHAIN_ASSERT(c->base_data, "base_data malloc failed");
    memcpy(c->base_data, s, len + 1);
    c->base_len = len;

    c->final_length = len;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif


    return c;
}


// general modifications
static chain* chain_mod(chain* c, const char* content) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(content, "NULL string");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    size_t new_len = __str_len(content);
    size_t old_len = c->final_length;

    chain* r = __chain_add_patch(c, 0, content, new_len, old_len, false);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return r;
}


static chain* chain_fmod(chain* c, cmod flag, const char* content, size_t pos, size_t l) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif


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

    chain* r = __chain_add_patch(c, pos, data, ins, del, is_delete);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return r;
}


// translate to string
static char* chain_stringify(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    size_t final_len = c->final_length;

    // avoid bumping the string data on arena, as the data is freed when chain is dropped
    char* out = (char*)__arena_alloc(c, final_len + 1);
    CHAIN_ASSERT(out, "arena allocation failed");

    memcpy(out, c->base_data, c->base_len);

    size_t cur_len = c->base_len;

    block* b = c->array;


    while (b) {
        for (size_t i = 0; i < b->count; i++) {
            const patch* p = &b->items[i];

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
        b = b->next;
    }

    out[cur_len] = '\0';

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return out;
}


// snapshot / copy
static chain* chain_snapshot(chain* c, size_t version) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    chain* s = (chain*)malloc(sizeof(chain));
    CHAIN_ASSERT(s, "malloc failed");
    memset(s, 0, sizeof(chain));

    /* init snapshot primary arena */
    s->arena.start = (char*)malloc(__STATIC_ARENA_SIZE);
    CHAIN_ASSERT(s->arena.start, "snapshot arena malloc failed");
    s->arena.capacity = __STATIC_ARENA_SIZE;
    s->arena.ptr = s->arena.start;
    s->arena.end = s->arena.start + __STATIC_ARENA_SIZE;
    s->arena.is_full = false;
    s->arena.usage = 0.0f;
    s->arena.is_collectible = false;
    s->arena.current_alive_count = 0;
    s->arena.max_alive_count = 0;
    s->arena.next = NULL;
    s->current_arena = &s->arena;

    block* b = __arena_alloc_block(s);
    CHAIN_ASSERT(b, "snapshot initial block alloc failed");
    b->next = NULL;
    b->count = 0;
    s->array = b;
    s->current_array = b;
    s->total_patches = 0;

    /* copy base pointer (shared) */
    s->base_data = c->base_data;
    s->base_len = c->base_len;

    /* iterate source blocks and copy pointers up to version count */
    size_t copied = 0;
    block* src = c->array;
    while (src && (version == 0 || copied < version)) {
        for (size_t i = 0; i < src->count && (version == 0 || copied < version); i++) {
            patch* sp = &src->items[i];

            if (s->current_array->count == __PATCH_MAX_COUNT) {
                block* nb = __arena_alloc_block(s);
                CHAIN_ASSERT(nb, "snapshot new block alloc failed");
                nb->next = NULL;
                nb->count = 0;
                s->current_array->next = nb;
                s->current_array = nb;
            }

            /* share pointer: increment the patch's refcount and owning arena counts */
            sp->refcount++;
            if (sp->parent_arena) {
                sp->parent_arena->current_alive_count++;
                if (sp->parent_arena->current_alive_count > sp->parent_arena->max_alive_count)
                    sp->parent_arena->max_alive_count = sp->parent_arena->current_alive_count;
            }

            /* store pointer into snapshot block */
            s->current_array->items[s->current_array->count++] = *sp;
            s->total_patches++;
            copied++;
        }
        src = src->next;
    }

    // snapshot final_length
    s->final_length = s->base_len;
    block* it = s->array;
    while (it) {
        for (size_t i = 0; i < it->count; i++) {
            patch* p = &it->items[i];
            if (p) s->final_length += p->delta;
        }
        it = it->next;
    }

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return s;

}


static chain* chain_copy(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif


    char* flat = chain_stringify(c);
    chain* nc = to_chain(flat);
    //free(flat);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return nc;
}


// comparisons
static bool chain_ccmp(chain* c, const char* s) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(s, "NULL string");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    size_t sl = __str_len(s);
    if (sl != chain_len(c)) return false;

    char* flat = chain_stringify(c);
    int eq = memcmp(flat, s, sl) == 0;
    //free(flat);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return eq;
}


static bool chain_cmp(chain* a, chain* b) {
    CHAIN_ASSERT(a, "NULL chain (first arg)");
    CHAIN_ASSERT(b, "NULL chain (second arg)");


    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    if (chain_len(a) != chain_len(b)) return false;
    char* af = chain_stringify(a);
    char* bf = chain_stringify(b);
    int eq = memcmp(af, bf, chain_len(a)) == 0;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    // free(af);
    // free(bf);
    return eq;
}


static void chain_drop(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    // release blocks and their patches
    block* cur = c->array;
    while (cur) {
        block* next = cur->next;
        __arena_release_block(c, cur);
        cur = next;
    }
    c->array = NULL;
    c->current_array = NULL;

    // walk arena chain freeing collectible non-primary arenas
    chain_arena* a = c->arena.next;
    while (a) {
        chain_arena* next = a->next;
        if (a->is_full && a->current_alive_count == 0) {
            __arena_unlink(c, a);
            __arena_free(a);
        } else {
            /* If still referenced by snapshots, leave it alone. Optionally push to a global free queue. */
        }
        a = next;
    }
    c->arena.next = NULL;

    // free primary arena buffer if safe (we freed contained patches above)
    if (c->arena.start) {
        free(c->arena.start);
        c->arena.start = NULL;
        c->arena.ptr = NULL;
        c->arena.end = NULL;
    }

    // free chain struct itself
    free(c);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return;
}

// ==== INTERNAL FUNCTIONS
/* declared with '__' (double underscore) */


static chain* __chain_add_patch(chain* c, size_t idx, const char* content, size_t ins_len,
        size_t repl_len, bool is_delete) {

    CHAIN_ASSERT(c, "NULL chain");
    if (!is_delete) CHAIN_ASSERT(content, "NULL string");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    size_t cl = c->final_length;
    if (idx > cl) idx = cl;
    if (repl_len > cl - idx) repl_len = cl - idx;

    /* allocate new block if current is full */
    if (c->current_array->count == __PATCH_MAX_COUNT) {
        block* nb = __arena_alloc_block(c);
        CHAIN_ASSERT(nb, "new block allocation failed");
        nb->count = 0;
        nb->next = NULL;
        c->current_array->next = nb;
        c->current_array = nb;
    }

    /* allocate patch struct inside current arena (no automatic refcount bump elsewhere) */
    patch* p = (patch*)__arena_alloc(c, sizeof(patch));
    memset(p, 0, sizeof(patch));

    p->idx = idx;
    p->len = ins_len;
    p->replaced_len = repl_len;
    p->delta = (ssize_t)ins_len - (ssize_t)repl_len;
    p->is_delete = is_delete;
    p->refcount = 1; /* owned by this chain */

    if (!is_delete && ins_len) {
        p->data = __arena_alloc_data(c, ins_len + 1); // this bumps parent arena's current_alive_count
        memcpy(p->data, content, ins_len);
        p->data[ins_len] = '\0';
    } else {
        p->data = (char*)__arena_alloc(c, 1); // do not count as 'alive' (this is internal sentinel)
        p->data[0] = '\0';

        p->parent_arena = c->current_arena;
        p->parent_arena->current_alive_count++;
        if (p->parent_arena->current_alive_count > p->parent_arena->max_alive_count)
            p->parent_arena->max_alive_count = p->parent_arena->current_alive_count;
    }

    p->parent_arena = c->current_arena;

    ///* append pointer to block */
    size_t slot = c->current_array->count++;
    c->current_array->items[slot] = *p;
    p->parent_block = c->current_array;

    c->total_patches++;
    c->final_length += p->delta;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return c;
}

// C string length calculation
static size_t __str_len(const char* str) {
    CHAIN_ASSERT(str, "NULL string");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    typedef size_t word;
    const char* s = str;

    const size_t wsize = sizeof(word);
    const uintptr_t wmask = (uintptr_t)wsize - 1;

    while (((uintptr_t)s & wmask) != 0) {
        if (*s == 0) {
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
                    return (size_t)((cp + i) - str);
                }
            }
        }
        wp++;
    }

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER, time and memory usage

    #endif
}


// ==== MEMORY MANAGEMENT

// == memory allocation

static chain_arena* __arena_new(void) {

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    chain_arena* a = (chain_arena*)malloc(sizeof(chain_arena));
    CHAIN_ASSERT(a, "allocation of the new arenea failed");

    a->start = (char*)malloc(__STATIC_ARENA_SIZE);
    CHAIN_ASSERT(a->start, "new arena buffer allocation failed");

    a->capacity = __STATIC_ARENA_SIZE;
    a->ptr = a->start;
    a->end = a->start + __STATIC_ARENA_SIZE;

    a->is_full = false;
    a->usage = 0.0f;
    a->is_collectible = false;
    a->current_alive_count = 0;
    a->max_alive_count = 0;
    a->next = NULL;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif


    return a;
}

static void* __arena_alloc(chain* c, size_t size) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(c->current_arena != NULL, "current_arena NULL");
    CHAIN_ASSERT(c->current_arena->start != NULL, "arena->start NULL");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    size = (size + 7) & ~((size_t)7);

    // predicted usage
    size_t used = (size_t)(c->current_arena->ptr - c->current_arena->start);
    size_t predicted_used = used + size;
    double predicted_percent = ((double)predicted_used * 100.0 / (double)c->current_arena->capacity);

    if (predicted_percent >= __ARENA_CRITICAL_PERCENT) {
        // mark current as full and switch to new
        c->current_arena->is_full = true;
        c->current_arena->usage = ((double)used) / (double)c->current_arena->capacity;

        chain_arena* new_arena = __arena_new();
        c->current_arena->next = new_arena;
        c->current_arena = new_arena;
        used = 0;
        predicted_used = size;
    }

    uintptr_t raw = (uintptr_t)c->current_arena->ptr;
    uintptr_t aligned = (raw + 7) & ~((uintptr_t)7);
    char* p = (char*)aligned;

    // check for extremly large allocation
    if (p + size > c->current_arena->end) {
        chain_arena* new_arena = __arena_new();
        c->current_arena->next = new_arena;
        c->current_arena = new_arena;

        raw = (uintptr_t)c->current_arena->ptr;
        aligned = (raw + 7) & ~((uintptr_t)7);
        p = (char*)aligned;

        CHAIN_ASSERT(size <= c->current_arena->capacity, "allocation size is bigger than arena capacity");
    }

    // allocate
    c->current_arena->ptr = p + size;

    // calculate usage
    size_t new_used = (size_t)(c->current_arena->ptr - c->current_arena->start);
    c->current_arena->usage = ((double)new_used) / (double)c->current_arena->capacity;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return (void*)p;
}


/*

allocation wrapper

it uses __arena_alloc to return data pointer and
also take care of counting alive memory allocations

*/
static char* __arena_alloc_data(chain* c, size_t size) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif


    char* p = (char*)__arena_alloc(c, size);
    CHAIN_ASSERT(p, "arena allocation returned NULL");


    chain_arena* a = c->current_arena;
    a->current_alive_count++;
    if (a->current_alive_count > a->max_alive_count) a->max_alive_count = a->current_alive_count;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return p;
}


static block* __arena_alloc_block(chain* c) {
    CHAIN_ASSERT(c, "NULL chain");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    block* b = (block*)__arena_alloc(c, sizeof(block));
    CHAIN_ASSERT(b, "arena allocation returned NULL");

    memset(b, 0, sizeof(block));

    chain_arena* a = c->current_arena;
    b->parent_arena = a;
    a->current_alive_count++;
    if (a->current_alive_count > a->max_alive_count) a->max_alive_count = a->current_alive_count;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return b;
}

// ==  memory release

static void __arena_release_patch(chain* c, patch* p) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(p, "NULL patch");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    if (p->refcount == 0) return;

    p->refcount--;
    if (p->refcount > 0) return;

    chain_arena* a = p->parent_arena;
    p->parent_arena = NULL;
    p->data = NULL;
    p->parent_block = NULL;

    if (a) {
        CHAIN_ASSERT(a->current_alive_count > 0, "arena alive underflow (patch)");
        a->current_alive_count--;
        if (a->is_full && a->current_alive_count == 0) {
            a->is_collectible = true;
            if (a != &c->arena) {
                __arena_unlink(c, a);
                __arena_free(a);
            } else {
                /* primary arena: keep buffer until chain_drop frees it */
            }
        }
    }

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

    return;
}


static void __arena_release_block(chain* c, block* b) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(b, "NULL block");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();


    #endif


    chain_arena* a = b->parent_arena;

    /* release patch pointers referenced by this block */
    for (size_t i = 0; i < b->count; ++i) {
        patch* p = &b->items[i];
        if (p) __arena_release_patch(c, p);
        //b->items[i] = NULL;
    }

    /* mark block released */
    b->parent_arena = NULL;
    b->count = 0;
    b->next = NULL;

    if (a) {
        CHAIN_ASSERT(a->current_alive_count > 0, "arena alive underflow (block)");
        a->current_alive_count--;
        if (a->is_full && a->current_alive_count == 0) {
            a->is_collectible = true;
            if (a != &c->arena) {
                __arena_unlink(c, a);
                __arena_free(a);
            }
        }
    }

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif

}

static void __arena_unlink(chain* c, chain_arena* a) {
    CHAIN_ASSERT(c, "NULL chain");
    CHAIN_ASSERT(c, "NULL arena");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    chain_arena* prev = &c->arena;
    while (prev && prev->next && prev->next != a) prev = prev->next;
    if (prev && prev->next == a) prev->next = a->next;

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif
}


// free an arena
static void __arena_free(chain_arena* a) {
    CHAIN_ASSERT(a, "NULL arena");

    #ifdef CHAIN_PROFILER

        clock_t st, et;
        st = clock();

    #endif

    if (a->start) free(a->start);
    free(a);

    #ifdef CHAIN_PROFILER

        et = clock();
        // TODO: PROFILER

    #endif
}


// ==== DEBUG
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

static void __chain_print_debug(chain* c) {
    CHAIN_ASSERT(false, "DEBUG VERSION OUTDATED");
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

    // // print patches
    // size_t depth = 0;
    // for (size_t i = c->array->count; i > 0; i--) {
    //     __chain_print_patch(c->array->items[i - 1], depth++);
    // }

    // BASE line
    // for (size_t i = 0; i < depth; i++) printf("   ");
    // printf("   └── BASE(\"");
    // if (c->base_len && c->base_data)
    //     __print_bytes(c->base_data, c->base_len);
    // printf("\")\n");
}

#endif /* CHAIN_IMPLEMENTATION */
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
