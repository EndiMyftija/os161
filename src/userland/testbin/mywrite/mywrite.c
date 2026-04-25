#include <unistd.h>
#include <string.h>

int main() {
    const char *msg = "SUCCESS: The kernel didn't crash!\n";
    // We pass 1 for stdout, our string, and the exact length.
    // Right now, your kernel ignores the '1' anyway.
    write(1, msg, strlen(msg));
    return 0;
}