struct S { char name[16]; int id; };
struct S s = { "hello", 42 };

int main(void) {
    if (s.name[0] != 'h') return 1;
    if (s.name[4] != 'o') return 2;
    if (s.name[5] != 0) return 3;
    if (s.id != 42) return 4;
    return 0;
}
