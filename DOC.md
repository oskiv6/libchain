# Chain – A tiny, fast, rope-like string library in pure C

**Chain** is an extremely small, zero-dependency, pure-C library that gives you mutable-string-like behavior without the usual pain of manual buffer management.

> **Think of it as “C strings, but actually nice to edit”.**

It is heavily inspired by ropes but implemented in a way that is trivial to drop into any C project: no STL, no exceptions, no hidden allocations except an arena you control.

Perfect for:
- compilers / transpilers
- interpreters
- source-code manipulation
- template engines
- any situation where you repeatedly insert/delete/replace text


## Why Chain exists

Normal null-terminated C strings are excellent for immutable text, but as soon as you need to:

- insert in the middle
- delete a slice
- append thousands of tiny pieces
- keep multiple versions of the same text

…you end up writing the same boring, error-prone buffer-resizing code over and over.

Example:
```c
// Native C string modification
int main() {
    char* text = strdup("Hello, World!");
    
    // replace "World" with "Developer"
    char* new_text1 = malloc(strlen(text) - 5 + 9 + 1);
    memcpy(new_text1, text, 7);               // "Hello, "
    memcpy(new_text1 + 7, "Developer", 9);   // "Developer"
    strcpy(new_text1 + 16, text + 12);       // copy "!"
    free(text);
    text = new_text1;
    
    // insert "Goodbye, " at the beginning
    char* new_text2 = malloc(strlen(text) + 9 + 1);
    memcpy(new_text2, "Goodbye, ", 9);
    strcpy(new_text2 + 9, text);
    free(text);
    text = new_text2;
    
    printf("Native C result: %s\n", text);
    free(text);
}
```

versus:

```c
int main() {
    chain* c = to_chain("Hello, World!");
    
    // replace "World" with "Developer"
    chain_fmod(c, REPLACE, "Developer", 7, 5);
    
    // insert "Goodbye, " at the beginning
    chain_fmod(c, INSERT, "Goodbye, ", 0, 0);
    
    char* result = chain_stringify(c);
    printf("Chain result: %s\n", result);
    free(result);
    chain_drop(c);
}
```

## Design
```
FINAL ← PATCH3 ← PATCH2 ← PATCH1 ← BASE
```

- **BASE** holds the original literal
- Every **PATCH** is a tiny struct describing an edit (insert/replace/delete) + pointer to new data
- Patches form a list of modifications
- Reference counting + an arena allocator make everything cheap and cache-friendly

When you modify a chain you get a **new** chain handle that shares almost all nodes with the old one. When the last handle to a particular version is dropped, the whole unreachable patch chain is freed in a single arena reset — essentially **O(1) cleanup**.

## Public API (current version v0.2)

```c
// create a chain from null-terminated string
chain* to_chain(const char* s);                                     

// get chain (text) length
size_t chain_len(const chain* c);

/*
    Converts a chain into a null-terminated C string.

    The returned pointer refers to memory inside the chain’s arena.
    It stays valid until the chain is modified or dropped.

    The chain owns this buffer. Do not free it manually.

    To obtain an independent heap-allocated string, use:
    
        `char* temp = malloc(chain_len(c) + 1);
        memcpy(temp, chain_stringify(c), chain_len(c) + 1);`

    A dedicated function `chain_mstringify()` will later handle
    this automatically by returning a heap-allocated buffer that the
    caller is responsible for freeing.
*/
char* chain_stringify(const chain* c);                                      

// modificate entire chain by creating a fresh patch with passed content
chain* chain_mod(chain* c, const char* new_content); 

// modificate specific point of the string with appropriate flag and content
chain* chain_fmod(chain* c, cmod f, const char* insert, size_t pos, size_t len); 

// create an independent snapshot up to a certain version
chain* chain_snapshot(chain* c, size_t version);

// create a flattened copy of the give chain
chain* chain_copy(const chain* c);  

// compare chain content with given string
bool chain_ccmp(const chain* c, const char* cstr);

// compare two chains
bool chain_cmp(const chain* a, const chain* b);     

// used to drop reference or free the chain
void chain_drop(chain* cptr);

```

# Building & using

Chain is distributed as two files:
- chain.h      → put in your include path
- chain.c      → compile with the rest of your project

No configuration, no macros to define, only CHAIN_IMPLEMENTATION in exactly one translation unit.

---

### Roadmap

- High-level modification functions:
  1. chain_replace()
  2. chain_insert()
  3. chain_delete()
- More chain optimized functions (chain_print())
- Cache 
- Merge and split
- Small string optimization (no allocation for small strings)
- Undo / redo 
- Flushing big chains
