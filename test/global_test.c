#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHAIN_IMPLEMENTATION
#include "../include/chain.h"

// silent equality check
static int check_str(const char* expected, chain* ch) {
    char* s = chain_stringify(ch);
    if (!s) return 0;

    bool test = strcmp(s, expected) == 0;
    free(s);
    return test;
}

// silent length check
static int check_len(size_t expected, chain* ch) {
    return chain_len(ch) == expected;
}

int main(void) {

    clock_t start_time = clock();

    // basic construction
    {
        chain* c = to_chain("Hello, Chain!");
        check_str("Hello, Chain!", c);
        check_len(13, c);
        chain_drop(c);
    }

    {
        chain* s = to_chain("Old text");
        chain_drop(s);
    }

    {
        chain* s = to_chain("World");
        size_t len = chain_len(s);
        (void)len;
        chain_drop(s);
    }

    {
        chain* s = to_chain("Hello, World!");
        chain_drop(s);
    }

    // copy
    {
        chain* s = to_chain("Hello, Chain!");
        chain* copy = chain_copy(s);

        check_str("Hello, Chain!", copy);
        check_str("Hello, Chain!", s);

        chain_drop(copy);
        chain_drop(s);
    }

    // comparison
    {
        chain* a = to_chain("test123");
        chain* b = to_chain("test123");

        chain_ccmp(a, "test123");
        chain_ccmp(a, "wrong");
        chain_cmp(a, b);

        chain_drop(a);
        chain_drop(b);
    }

    // insert
    {
        chain* s = to_chain("hi");
        s = chain_fmod(s, INSERT, "x", 1, 0);
        chain_drop(s);
    }

    // long chain modification loop
    {
        chain* root = to_chain("start");
        chain* cur = root;

        for (int i = 0; i < 1000; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "p%d_", i);
            chain* new_cur = chain_fmod(cur, INSERT, buf, chain_len(cur), 0);
            if (cur != root) chain_drop(cur);
            cur = new_cur;
        }

        char* final = chain_stringify(cur);
        (void)final;
        free(final);

        chain_drop(root);
    }

    // short chain
    {
        chain* s = to_chain("abc");
        chain_drop(s);
    }

    clock_t end_time = clock();
    printf("test duration: %.6f seconds\n",
           (double)(end_time - start_time) / CLOCKS_PER_SEC);

    return 0;
}
