#define system onyx_system
#define signal onyx_signal
#include "onyxc.h"
#include <stdlib.h>
#include <signal.h>
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)

static struct {
    int error, ring, access_error, open_error, collisions, opens;
    int write_error, zero_write, writes, close_error, closes;
    int spawn_error, spawned, wait_error, waits, unlink_error, unlinks;
    int signal_error, signals, violation;
    char path[32], previous_path[32], script[1024];
    size_t length;
} fake;
static onyx_sighandler_t installed;
int *___errno_location(void) { return &fake.error; }
static void reset(void) { memset(&fake, 0, sizeof(fake)); fake.ring = 2; }
long _onyx_getpid(void) { return 123; }
long _onyx_getring(void) { return fake.ring; }
long _onyx_access(const char *path, int mode) {
    if (strcmp(path, "/bin/osh") || mode != X_OK) fake.violation = 1;
    return fake.access_error;
}
long _onyx_open(const char *path, int flags, int mode) {
    fake.opens++;
    if (flags != (O_WRONLY | O_CREAT | O_EXCL) || mode != 0600)
        fake.violation = 1;
    if (fake.opens > 1 && !strcmp(path, fake.previous_path)) fake.violation = 1;
    memcpy(fake.previous_path, path, 32);
    if (fake.open_error) return fake.open_error;
    if (fake.collisions) { fake.collisions--; return -EEXIST; }
    memcpy(fake.path, path, 32);
    return 7;
}
long _onyx_write(int fd, const void *buf, size_t size) {
    if (fd != 7) fake.violation = 1;
    fake.writes++;
    if (fake.write_error && fake.writes == 2) return fake.write_error;
    if (fake.zero_write) return 0;
    if (size > 2) size = 2;
    memcpy(fake.script + fake.length, buf, size);
    fake.length += size;
    return (long)size;
}
long _onyx_close(int fd) {
    if (fd != 7) fake.violation = 1;
    fake.closes++;
    return fake.close_error;
}
long _onyx_spawn(const char *path, char *const *argv, int ring) {
    fake.spawned++;
    if (strcmp(path, "/bin/osh") || strcmp(argv[0], "/bin/osh") ||
        strcmp(argv[1], fake.path) || argv[2] || ring != fake.ring ||
        fake.closes != 1 || !fake.length || fake.script[fake.length - 1] != '\n')
        fake.violation = 1;
    return fake.spawn_error ? fake.spawn_error : 45;
}
long _onyx_waitpid(int child, int *status, int options) {
    if (child != 45 || options != 0 || fake.unlinks) fake.violation = 1;
    fake.waits++;
    if (fake.wait_error) return fake.wait_error;
    if (fake.waits == 1) return -ENOENT;
    if (fake.waits == 2) return -EINTR;
    *status = 7;
    return 45;
}
long _onyx_unlink(const char *path) {
    if (strcmp(path, fake.path)) fake.violation = 1;
    fake.unlinks++;
    return fake.unlink_error;
}
long _onyx_sigaction(int number, const void *new_action, void *old_action) {
    const struct sigaction *action = new_action;
    struct sigaction *previous = old_action;
    fake.signals++;
    if (action->sa_mask || action->sa_flags || action->sa_restorer) fake.violation = 1;
    if (number == SIGKILL) return -EINVAL;
    if (fake.signal_error) return fake.signal_error;
    previous->sa_handler = installed;
    installed = action->sa_handler;
    return 0;
}
static void handler(int number) { (void)number; }
int main(void) {
    reset();
    CHECK(system(NULL) == 1 && !fake.opens && !fake.violation);
    fake.access_error = -ENOENT;
    CHECK(system(NULL) == 0 && errno == ENOENT);
    reset(); fake.collisions = 2;
    CHECK(system("echo ok") == (7 << 8));
    CHECK(fake.opens == 3 && fake.spawned == 1 && fake.waits == 3);
    CHECK(fake.closes == 1 && fake.unlinks == 1 && !fake.violation);
    CHECK(fake.length == 8 && !memcmp(fake.script, "echo ok\n", 8));
    reset(); fake.spawn_error = -EPERM;
    CHECK(system("") == -1 && errno == EPERM && fake.unlinks == 1);
    CHECK(fake.length == 1 && fake.script[0] == '\n' && !fake.violation);
    reset(); fake.ring = 1;
    CHECK(system("x") == (7 << 8) && !fake.violation);
    reset(); fake.ring = -EPERM;
    CHECK(system("x") == -1 && errno == EPERM && !fake.opens);
    reset(); fake.ring = 3;
    CHECK(system("x") == -1 && errno == EINVAL && !fake.opens);
    reset(); fake.open_error = -EACCES;
    CHECK(system("x") == -1 && errno == EACCES && !fake.unlinks && !fake.closes);
    reset(); fake.collisions = 200;
    CHECK(system("x") == -1 && errno == EEXIST && fake.opens == 128 && !fake.unlinks);
    reset(); fake.write_error = -ENOSPC;
    CHECK(system("long command") == -1 && errno == ENOSPC);
    CHECK(fake.closes == 1 && fake.unlinks == 1 && !fake.spawned);
    reset(); fake.write_error = -EINTR;
    CHECK(system("retry") == (7 << 8) && !memcmp(fake.script, "retry\n", 6));
    reset(); fake.zero_write = 1;
    CHECK(system("x") == -1 && errno == EIO && fake.closes == 1 && fake.unlinks == 1);
    reset(); fake.close_error = -EIO;
    CHECK(system("x") == -1 && errno == EIO && !fake.spawned && fake.unlinks == 1);
    reset(); fake.wait_error = -ECHILD;
    CHECK(system("x") == -1 && errno == ECHILD && fake.unlinks == 1);
    reset(); fake.unlink_error = -EACCES;
    CHECK(system("x") == -1 && errno == EACCES && fake.unlinks == 1);
    reset(); fake.spawn_error = -EPERM; fake.unlink_error = -EACCES;
    CHECK(system("x") == -1 && errno == EPERM && fake.unlinks == 1);
    reset(); installed = SIG_DFL;
    CHECK(signal(SIGINT, handler) == SIG_DFL && installed == handler);
    CHECK(signal(SIGINT, SIG_IGN) == handler && installed == SIG_IGN);
    CHECK(signal(SIGINT, SIG_DFL) == SIG_IGN && installed == SIG_DFL);
    CHECK(signal(SIGINT, SIG_ERR) == SIG_ERR && errno == EINVAL && fake.signals == 3);
    CHECK(signal(SIGKILL, handler) == SIG_ERR && errno == EINVAL);
    fake.signal_error = -EPERM;
    CHECK(signal(SIGINT, handler) == SIG_ERR && errno == EPERM && !fake.violation);
    return 0;
}
