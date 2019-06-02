#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    for (int i = 0; i < atoi(argv[argc - 1]); i++) {
        fprintf(stdout, "0");
    }
    fflush(stdout);
    return 0;
}
