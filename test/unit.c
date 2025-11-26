#define CHAIN_IMPLEMENTATION
#include "../chain.h"

#include <stdio.h>



int main () {

    SET_ARENA_SIZE(KB(6));

    chain* c = to_chain("Hello, World!");

    c = chain_fmod(c, INSERT, " My name is libchain", chain_len(c), 20);
    c = chain_mod(c, "This is test unit");

    char* s = chain_stringify(c);

    printf("%s\n\n", s);

    chain_drop(c);

    c = to_chain("Simple");

    c = chain_fmod(c, INSERT, " modification", chain_len(c), 13);

    c = chain_mod(c, "Hello, World!");

    s = chain_stringify(c);

    chain* snap = chain_snapshot(c, 2);

    snap = chain_mod(snap, "libchain");

    char* s_snap = chain_stringify(snap);

    printf("%s\n", s);
    printf("%s\n", s_snap);

    chain_drop(c);
    chain_drop(snap);


    return 0;
}
