int arr_empty[5] = {};
char str_size[10] = "hello";
int arr_partial[5] = {1, 2};
char *str_arr[] = {"hello", "world"};

int main(void) {
    int ok = 0;
    if (arr_empty[0] != 0) return 1;
    if (arr_empty[4] != 0) return 2;
    if (str_size[0] != 'h') return 3;
    if (str_size[5] != 0) return 4;
    if (str_size[9] != 0) return 5;
    if (arr_partial[0] != 1) return 6;
    if (arr_partial[1] != 2) return 7;
    if (arr_partial[4] != 0) return 8;
    if (str_arr[0][0] != 'h') return 9;
    if (str_arr[1][0] != 'w') return 10;
    return 0;
}
