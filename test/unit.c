#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/chain.h"

// silent equality check
static int check_str(const char* expected, chain* ch) {
    char* s = chain_read(ch);
    if (!s) return 0;
    int match = (strcmp(s, expected) == 0);
    free(s);
    return match;
}

// silent length check
static int check_len(size_t expected, chain* ch) {
    return chain_len(ch) == expected;
}

// always reassign without prints
#define CHAIN_REASSIGN(var, new_chain) \
    do { \
        chain* __tmp = (new_chain); \
        chain_drop(&(var)); \
        (var) = __tmp; \
    } while(0)

int main(void) {

    clock_t start_time = clock();

    {
        chain* c1 = to_chain("Hello, Chain!");
        check_str("Hello, Chain!", c1);
        check_len(13, c1);
        chain_drop(&c1);
    }

    {
        chain* e = to_chain(NULL);
        check_str("", e);
        check_len(0, e);
        chain_drop(&e);
    }

    {
        chain* base = to_chain("alive");
        chain* r1 = chain_ref(base);
        chain* r2 = chain_ref(base);
        chain_drop(&r1);
        chain_drop(&r2);
        check_str("alive", base);
        chain_drop(&base);
    }

    {
        chain* s = to_chain("Old text");
        CHAIN_REASSIGN(s, chain_mod(s, "New text"));
        CHAIN_REASSIGN(s, chain_mod(s, "Successful modification"));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("World");
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "Hello, ", 0, 0));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Hello, World");
        size_t len = chain_len(s);
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "!", len, 0));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Hello, World!");
        CHAIN_REASSIGN(s, chain_fmod(s, REPLACE, " from chain!", 5, 11));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Hello, World!");
        CHAIN_REASSIGN(s, chain_fmod(s, DELETE, "", 5, 7));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Hello, World!");
        CHAIN_REASSIGN(s, chain_fmod(s, REPLACE, "Chain", 7, 6));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Old text");
        CHAIN_REASSIGN(s, chain_mod(s, "New text"));
        CHAIN_REASSIGN(s, chain_mod(s, "World"));
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "Hello, ", 0, 0));
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "!", chain_len(s), 0));
        CHAIN_REASSIGN(s, chain_fmod(s, REPLACE, "Chain", 7, 5));
        CHAIN_REASSIGN(s, chain_fmod(s, DELETE, NULL, 5, 8));
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, ", Chain", 5, 7));
        chain_drop(&s);
    }

    {
        chain* s = to_chain("Hello, Chain!");
        chain* copy = chain_copy(s);
        CHAIN_REASSIGN(s, chain_mod(s, "modified"));
        chain_drop(&copy);
        chain_drop(&s);
    }

    {
        chain* a = to_chain("test123");
        chain* b = to_chain("test123");
        chain_ccpy(a, "test123");
        chain_ccpy(a, "wrong");
        chain_cpy(a, b);
        chain_drop(&a);
        chain_drop(&b);
    }

    {
        chain* fresh = chain_fmod(NULL, INSERT, "from nothing", 0, 0);
        chain_drop(&fresh);
    }

    {
        chain* shorty = to_chain("hi");
        chain* safe = chain_fmod(shorty, INSERT, "x", 100, 0);
        chain_drop(&safe);
        chain_drop(&shorty);
    }

    {
        chain* root = to_chain("");
        chain* cur = root;

        for (int i = 0; i < 1000; i++) {
            char buf[32];
            snprintf(buf, sizeof(buf), "p%d ", i);
            chain* new_cur = chain_fmod(cur, INSERT, buf, chain_len(cur), 0);
            if (cur != root) chain_drop(&cur);
            cur = new_cur;
        }

        char* final = chain_read(cur);
        free(final);

        chain_drop(&root);
        chain_drop(&cur);
    }

    {
        chain* s = to_chain("abc");
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "X", 0, 0));
        CHAIN_REASSIGN(s, chain_fmod(s, INSERT, "Y", 4, 0));
        CHAIN_REASSIGN(s, chain_fmod(s, REPLACE, "123", 1, 3));
        chain_drop(&s);
    }

    {
        chain* e = to_chain("");
        CHAIN_REASSIGN(e, chain_fmod(e, INSERT, "hello", 0, 0));
        chain_drop(&e);
    }

    clock_t end_time = clock();

    printf("test duration: %.6f seconds\n",
           (double)(end_time - start_time) / CLOCKS_PER_SEC);

    return 0;
}
