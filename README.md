# libchain
A small, portable C library implementing a chain-of-patches text model. Provides safe and efficient operations for modifying text buffers at any position.

----

## Platform support:
- macOS (arm64)
- linux (test not performed)
- Windows (test not performed)

---

# Building & using

Chain is distributed as two files:
- chain.h      → put in your include path
- chain.c      → compile with the rest of your project

No configuration, no macros to define, only CHAIN_IMPLEMENTATION in exactly one translation unit.


--- 

You can learn more about this libary in `examples` and `DOC.md`

---
