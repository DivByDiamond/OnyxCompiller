// EXPECT: 1 2 3 5 8 13 21 34 55 89 
#include <stdio.h>
#include <stdlib.h>
static int cmp(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}
int main(void) {
    int arr[10] = {5, 2, 89, 1, 55, 13, 3, 34, 21, 8};
    qsort(arr, 10, sizeof(int), cmp);
    for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
    printf("\n");
    return 0;
}
