#include "./../include/chain.h"

#include <stdio.h>

int main () {


    // initialization of the chain
    chain* msg = to_chain("Hello, Worl");                   // o: Hello, Worl
    printf("(test case): %s\n", chain_read(msg));
    msg = chain_fmod(msg, INSERT, "d!", chain_len(msg), 2); // o: Hello, World!

    // to finalize chain modifications, just use
    char* cmsg = chain_read(msg);
    printf("%s\n", cmsg);

    // you can also modify whole buffer at once
    msg = chain_mod(msg, "This is modification");
    cmsg = chain_read(msg);
    printf("%s\n", cmsg);

    // and finally you can see a modification tree using debug tool
    __chain_print_debug(msg);


    // remember to drop the chain
    chain_drop(&msg);
}
