/* stdlib.h */
#pragma once
#include <onyxc.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 2147483647

#ifdef __cplusplus
extern "C" {
#endif
int system(const char *command);
#ifdef __cplusplus
}
#endif
