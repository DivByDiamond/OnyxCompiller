/*
 * compat.h — portability layer for OnyxCC.
 *
 * On Linux/x86_64 (host build):
 *   - Use the system libc (glibc) and getopt.h directly.
 *
 * On OnyxOS (freestanding build with clang + -nostdlib):
 *   - Use the shim in src/shim.c which provides minimal libc.
 *   - Declare our own struct option and getopt_long prototype.
 *
 * When CC_FREESTANDING is defined (self-hosting), the freestanding path
 * is taken regardless of the host platform.
 */
#ifndef CC_COMPAT_H
#define CC_COMPAT_H

#if defined(CC_FREESTANDING) || !defined(__linux__)
/* Freestanding build — provide types directly, no system headers. */
typedef unsigned long size_t;
typedef long ssize_t;
typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef long intptr_t;
typedef unsigned long uintptr_t;
#define NULL ((void*)0)
#define UINT64_MAX 18446744073709551615ULL

/* bool */
#define bool _Bool
#define true 1
#define false 0

/* va_list — use __builtin_va_* (handled by gen.c builtin handlers) */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)
#define va_copy(d,s)  __builtin_va_copy(d,s)

typedef struct { int fd; } FILE;
extern FILE *stderr;
extern FILE *stdout;

/* stdio */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
unsigned long fread(void *buf, unsigned long sz, unsigned long n, FILE *f);
unsigned long fwrite(const void *buf, unsigned long sz, unsigned long n, FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
int fputc(int c, FILE *f);
int putchar(int c);
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int snprintf(char *buf, unsigned long cap, const char *fmt, ...);
int vsnprintf(char *buf, unsigned long cap, const char *fmt, va_list ap);

/* fseek whence constants */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* File EOF marker */
#define EOF (-1)

/* stdlib */
void *malloc(unsigned long n);
void *calloc(unsigned long n, unsigned long sz);
void *realloc(void *p, unsigned long n);
void free(void *p);
void exit(int code);
char *getenv(const char *name);
double strtod(const char *s, char **endp);

/* string */
unsigned long strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, unsigned long n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strncpy(char *d, const char *s, unsigned long n);
char *strcpy(char *d, const char *s);
char *strdup(const char *s);
void *memcpy(void *d, const void *s, unsigned long n);
void *memset(void *d, int c, unsigned long n);
int memcmp(const void *a, const void *b, unsigned long n);
void *memchr(const void *s, int c, unsigned long n);

/* ctype — only what onyxcc uses. */
int isalpha(int c);
int isdigit(int c);
int isspace(int c);
int isupper(int c);
int islower(int c);
int isxdigit(int c);
int isalnum(int c);
int tolower(int c);
int toupper(int c);

/* getopt_long (provided by shim.c) */
struct option {
    const char *name;
    int has_arg;
    int *flag;
    int val;
};
#define no_argument 0
#define required_argument 1
extern char *optarg;
extern int optind;
int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex);

#else
/* Linux build — use system libc */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ctype.h>
#include <getopt.h>
#endif

#endif /* CC_COMPAT_H */
