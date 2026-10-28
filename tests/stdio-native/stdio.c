#define snprintf onyx_snprintf
#define tmpfile onyx_tmpfile
#define fclose onyx_fclose
#define fgetc onyx_fgetc
#define ungetc onyx_ungetc
#define feof onyx_feof
#define fread onyx_fread
#define setvbuf onyx_setvbuf
#define fwrite onyx_fwrite
#define fputs onyx_fputs
#define puts onyx_puts
#define fflush onyx_fflush
#define ferror onyx_ferror
#define clearerr onyx_clearerr
#define stdout onyx_stdout
#define stdin onyx_stdin
#include "onyxc.h"
static char captured[32768];
static size_t captured_size;
static int calls;
static int failure_call;
static int zero_write;
static int input_used;
long _onyx_read(int fd, void *buf, size_t count) {
    if (fd != 0) return -9;
    if (!count || input_used) return 0;
    *(char *)buf = 'i'; input_used = 1; return 1;
}
static int opens, unlinks, closes;
static char prior_name[L_tmpnam];
long _onyx_getpid(void) { return 123; }
long _onyx_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!(flags & O_EXCL)) return -22;
    if (!opens++) { memcpy(prior_name, path, L_tmpnam); return -EEXIST; }
    if (strcmp(prior_name, path) == 0) return -EIO;
    return 3;
}
long _onyx_unlink(const char *path) { (void)path; unlinks++; return 0; }
long _onyx_close(int fd) { if (fd != 3) return -EBADF; closes++; return 0; }
long _onyx_lseek(int fd, long offset, int whence) {
    (void)fd; (void)offset; (void)whence; return -29;
}
long _onyx_write(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t i;
    if (fd != 1) return -9;
    calls++;
    if (calls == failure_call) return -5;
    if (zero_write) return 0;
    if (n > 2) n = 2;
    for (i = 0; i < n; i++) captured[captured_size++] = p[i];
    return (long)n;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
    char custom[3], formatted[128];
    FILE *temporary;
    CHECK(fgetc(stdin) == 'i');
    CHECK(fgetc(stdin) == EOF && feof(stdin));
    CHECK(ungetc('Z', stdin) == 'Z' && !feof(stdin));
    CHECK(fgetc(stdin) == 'Z' && fgetc(stdin) == EOF);
    CHECK(fread(custom, 0, 2, stdin) == 0);
    CHECK(puts("hello") >= 0);
    CHECK(captured_size == 6 && memcmp(captured, "hello\n", 6) == 0);
    CHECK(fwrite("", 0, 100, stdout) == 0);
    CHECK(fwrite("x", (size_t)-1, 2, stdout) == 0 && ferror(stdout));
    clearerr(stdout);
    CHECK(fputs("x", stdin) < 0 && ferror(stdin));
    clearerr(stdin);
    captured_size = 0; calls = 0; failure_call = 2;
    CHECK(fputs("abcdef\n", stdout) < 0 && ferror(stdout));
    CHECK(captured_size == 2);
    failure_call = 0; clearerr(stdout);
    CHECK(fflush(stdout) == 0);
    CHECK(captured_size == 7 && memcmp(captured, "abcdef\n", 7) == 0);
    captured_size = 0; zero_write = 1;
    CHECK(fputs("z\n", stdout) < 0 && ferror(stdout));
    zero_write = 0; clearerr(stdout);
    CHECK(fflush(stdout) == 0);
    CHECK(captured_size == 2 && memcmp(captured, "z\n", 2) == 0);
    captured_size = 0;
    CHECK(setvbuf(stdout, custom, _IOFBF, sizeof(custom)) == 0);
    CHECK(fputs("ab", stdout) >= 0 && captured_size == 0);
    CHECK(fflush(stdout) == 0 && captured_size == 2);
    CHECK(setvbuf(stdout, NULL, _IONBF, 0) == 0);
    CHECK(fputs("c", stdout) >= 0 && captured_size == 3);
    CHECK(setvbuf(stdout, NULL, _IOLBF, 8) == 0);
    CHECK(fputs("d", stdout) >= 0 && captured_size == 3);
    CHECK(fputs("e\n", stdout) >= 0 && captured_size == 6);
    CHECK(setvbuf(stdout, NULL, _IONBF, 0) == 0);
    CHECK(snprintf(formatted, sizeof(formatted), "%.17g", 0.1) == 19);
    CHECK(strcmp(formatted, "0.10000000000000001") == 0);
    CHECK(snprintf(formatted, sizeof(formatted), "%.99f", 1.0) == 101);
    CHECK(formatted[0] == '1' && formatted[1] == '.' && !formatted[101]);
    CHECK(snprintf(formatted, sizeof(formatted), "%.9999999999999999999f", 1.0) < 0);
    CHECK(snprintf(formatted, sizeof(formatted), "%*f", INT_MIN, 1.0) < 0);
    temporary = tmpfile();
    CHECK(temporary != NULL && opens == 2 && unlinks == 1);
    CHECK(fclose(temporary) == 0 && closes == 1);
    return 0;
}
