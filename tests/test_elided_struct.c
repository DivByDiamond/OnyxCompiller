struct Inner { int x; int y; };
struct Outer { struct Inner i; int z; };
struct Outer o = { 1, 2, 3 };

int main(void) {
    if (o.i.x != 1) return 1;
    if (o.i.y != 2) return 2;
    if (o.z != 3) return 3;
    return 0;
}
