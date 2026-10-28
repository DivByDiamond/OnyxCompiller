// EXPECT: storage-classes-ok
#include <stdio.h>
typedef struct { long member; } object;
extern object first;
object first;
object second;
static object hidden;
object visible;
int main(void) {
    first.member = 3; second.member = 7;
    hidden.member = 11; visible.member = 13;
    if (&first == &second || first.member != 3 || second.member != 7) return 1;
    if (&hidden == &visible || hidden.member != 11 || visible.member != 13) return 2;
    printf("storage-classes-ok\n"); return 0;
}
