#define CHAIN_IMPLEMENTATION
#include "./../include/chain.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_INPUT 1024

int main() {
    char input[MAX_INPUT];
    chain* doc = to_chain("");

    printf("Simple chain-based editor. Type lines and press ENTER.\n");
    printf("Commands: /undo, /replace <pos> <len> <text>, /delete <pos> <len>, /exit\n");

    double chain_time_total = 0.0;

    while (1) {
        printf("> ");
        if (!fgets(input, MAX_INPUT, stdin)) break;

        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n') input[len - 1] = 0;

        if (strcmp(input, "/exit") == 0) break;

        clock_t start = clock();

        if (strncmp(input, "/undo", 5) == 0) {
            // simple undo: remove last patch
            if (doc->array_count > 0) doc->array_count--;
        } else if (strncmp(input, "/replace", 8) == 0) {
            size_t pos, rlen;
            char newtext[256];
            if (sscanf(input, "/replace %zu %zu %255[^\n]", &pos, &rlen, newtext) == 3) {
                chain_fmod(doc, REPLACE, newtext, pos, rlen);
            } else {
                printf("Invalid replace command.\n");
            }
        } else if (strncmp(input, "/delete", 7) == 0) {
            size_t pos, dlen;
            if (sscanf(input, "/delete %zu %zu", &pos, &dlen) == 2) {
                chain_fmod(doc, DELETE, NULL, pos, dlen);
            } else {
                printf("Invalid delete command.\n");
            }
        } else {
            // regular input: append at the end
            size_t cur_len = chain_len(doc);
            chain_fmod(doc, INSERT, input, cur_len, 0);
        }

        clock_t end = clock();
        chain_time_total += (double)(end - start) / CLOCKS_PER_SEC;
    }

    char* final_text = chain_stringify(doc);
    printf("\nFinal document:\n%s\n", final_text);

    printf("\nTotal chain processing time: %.6f seconds\n", chain_time_total);

    chain_drop(doc);
    return 0;
}
