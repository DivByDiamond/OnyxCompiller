// EXPECT: readback=42 hello
#include <stdio.h>
int main(void) {
    FILE *f = fopen("/tmp/t07.txt", "w");
    if (!f) { printf("open-fail\n"); return 1; }
    fprintf(f, "42 hello\n");
    fclose(f);
    f = fopen("/tmp/t07.txt", "r");
    int v; char buf[16];
    fscanf(f, "%d %15s", &v, buf);
    fclose(f);
    printf("readback=%d %s\n", v, buf);
    remove("/tmp/t07.txt");
    return 0;
}
