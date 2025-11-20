#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHAIN_IMPLEMENTATION
#include "./../include/chain.h"

#define ITERATIONS 100000

// --------------------
// Native C string editor
// --------------------
char* native_insert(char* base, const char* ins, size_t pos) {
    size_t base_len = strlen(base);
    size_t ins_len = strlen(ins);
    char* newstr = malloc(base_len + ins_len + 1);
    if (!newstr) abort();

    if (pos > base_len) pos = base_len;

    memcpy(newstr, base, pos);
    memcpy(newstr + pos, ins, ins_len);
    memcpy(newstr + pos + ins_len, base + pos, base_len - pos);
    newstr[base_len + ins_len] = 0;

    free(base);
    return newstr;
}

char* native_replace(char* base, const char* rep, size_t pos, size_t len) {
    size_t base_len = strlen(base);
    size_t rep_len = strlen(rep);
    if (pos + len > base_len) len = base_len - pos;

    char* newstr = malloc(base_len - len + rep_len + 1);
    if (!newstr) abort();

    memcpy(newstr, base, pos);
    memcpy(newstr + pos, rep, rep_len);
    memcpy(newstr + pos + rep_len, base + pos + len, base_len - pos - len);
    newstr[base_len - len + rep_len] = 0;

    free(base);
    return newstr;
}

char* native_delete(char* base, size_t pos, size_t len) {
    size_t base_len = strlen(base);
    if (pos + len > base_len) len = base_len - pos;

    char* newstr = malloc(base_len - len + 1);
    if (!newstr) abort();

    memcpy(newstr, base, pos);
    memcpy(newstr + pos, base + pos + len, base_len - pos - len);
    newstr[base_len - len] = 0;

    free(base);
    return newstr;
}

// --------------------
// Benchmark main
// --------------------
int main() {
    const char* initial = "Hello, World!";

    // ----------------
    // Native C benchmark
    // ----------------
    char* native = strdup(initial);
    clock_t st = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        native = native_replace(native, "Developer", 7, 5);
        native = native_replace(native, "Goodbye, ", 0, 7);
    }
    clock_t et = clock();
    double native_time = (double)(et - st) / CLOCKS_PER_SEC;
    //printf("native C final string: %s\n", native);
    free(native);
    printf("Native C processing time: %.6f s\n", native_time);

    // ----------------
    // Chain benchmark
    // ----------------
    chain* c = to_chain(initial);
    st = clock();
    for (int i = 0; i < ITERATIONS; i++) {
        chain_fmod(c, REPLACE, "Developer", 7, 5);
        chain_fmod(c, REPLACE, "Goodbye, ", 0, 7);
    }
    et = clock();
    double chain_time = (double)(et - st) / CLOCKS_PER_SEC;

    char* chain_str = chain_stringify(c);
    //printf("Chain final string: %s\n", chain_str);
    chain_drop(c);

    printf("Chain processing time: %.6f s\n", chain_time);

    return 0;
}
