#include <cstdio>
#include <cstdlib>
// Code/native_client/main.cpp -- entry point, replaced in Plan 04
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: skyrim-coop --pid <pid> --port <port>\n");
        return 1;
    }
    printf("skyrim-coop native client stub\n");
    return 0;
}
