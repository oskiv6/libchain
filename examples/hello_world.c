#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHAIN_IMPLEMENTATION
#include "./../include/chain.h"

int main () {

    chain* c = to_chain("Hello, World");
    c = chain_mod(c, "Test");
    c = chain_mod(c, "Test1");
    c = chain_mod(c, "Test2");

    chain* snap = chain_snapshot(c, 1);
    snap = chain_mod(snap, "Test3");

    c = chain_fmod(c, INSERT, " Developer", 5, 10);

    printf("%s\n", chain_stringify(c));

    chain_drop(c);
    chain_drop(snap);

}
