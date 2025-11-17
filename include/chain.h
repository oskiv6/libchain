#ifndef __CHAIN_H__
#define __CHAIN_H__

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct chain chain;

typedef enum {
    INSERT,   // insert 'content' at position i
    REPLACE,  // replace l bytes starting at i with 'content'
    DELETE    // delete l bytes starting at i (content ignored, may be NULL)
} cmod;

/* PUBLIC API */
chain* to_chain(const char* cstr);                                         // create a chain from C string
chain* chain_ref(chain* s);                                                // increment reference count
void   chain_drop(chain** s);                                               // decrement reference count and free when 0

size_t chain_len(const chain* s);                                          // get chain logical length

char*  chain_read(const chain* s);                                         // returns newly malloc'd C string (caller frees)

chain* chain_mod(chain* s, const char* new_content);                       // full replace
chain* chain_fmod(chain* s, cmod f, const char* insert, size_t pos, size_t len); // general modification

chain* chain_copy(const chain* s);                                         // deep copy (flattened)

bool chain_ccpy(const chain* s, const char* cstr);                         // compare chain with C string
bool chain_cpy(const chain* a, const chain* b);                            // compare two chains (content)

void __chain_print_debug(const chain* s);                                  // debug printer

#ifdef __cplusplus
}
#endif

#endif // __CHAIN_H__
