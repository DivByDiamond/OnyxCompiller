/*
 * termios.c — terminal mode control for libonyxc.
 *
 * Thin wrapper over the TCGETS/TCSETS ioctls. The kernel stores a
 * per-process terminal state (ECHO/ICANON/VMIN/VTIME); these helpers
 * read/modify/write it so full-screen programs (oed, osysmon) can switch
 * to raw mode and back.
 */
#include "onyxc.h"
#include <termios.h>
#include <errno.h>

int tcgetattr(int fd, struct termios *t) {
    long r = _onyx_ioctl(fd, TCGETS, (long)t);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int tcsetattr(int fd, int optional_actions, const struct termios *t) {
    (void)optional_actions;   /* TCSANOW semantics only */
    long r = _onyx_ioctl(fd, TCSETS, (long)t);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int cfgetispeed(const struct termios *t) { return (int)t->c_ispeed; }
int cfgetospeed(const struct termios *t) { return (int)t->c_ospeed; }

int cfsetispeed(struct termios *t, int speed) {
    t->c_ispeed = (speed_t)speed;
    return 0;
}

int cfsetospeed(struct termios *t, int speed) {
    t->c_ospeed = (speed_t)speed;
    return 0;
}

/* Put the terminal in raw mode (the nano/vim/btop prerequisite):
 * no echo, no line editing, no signal chars, 1-byte reads. */
void cfmakeraw_apply(struct termios *t) {
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                    ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int cfmakeraw(struct termios *t) {
    cfmakeraw_apply(t);
    return 0;
}

/* Restore "sane" cooked-mode defaults. */
int cfsetsane(struct termios *t) {
    t->c_iflag |= ICRNL;
    t->c_oflag |= OPOST | ONLCR;
    t->c_lflag |= ECHO | ECHOE | ECHOK | ICANON | ISIG;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
    return 0;
}

/* Allocate a PTY pair: open /dev/ptmx, resolve the pts number via
 * TIOCGPTN and open the matching /dev/pts/N slave. On success *mfd and
 * *sfd hold the master/slave descriptors and 0 is returned; on failure
 * -1 is returned with errno set (and the master fd is closed). */
int pty_open(int *mfd, int *sfd) {
    int m = (int)_onyx_open("/dev/ptmx", O_RDWR, 0);
    if (m < 0) {
        errno = (int)(-m);
        return -1;
    }
    int n = 0;
    long r = _onyx_ioctl(m, TIOCGPTN, (long)&n);
    if (r < 0) {
        errno = (int)(-r);
        _onyx_close(m);
        return -1;
    }
    char path[16];
    {
        const char *pfx = "/dev/pts/";
        int i = 0;
        while (pfx[i] != '\0') { path[i] = pfx[i]; i++; }
        if (n >= 10) { /* PTY_MAX caps the pts number at a single digit */
            errno = ENODEV;
            _onyx_close(m);
            return -1;
        }
        path[i++] = (char)('0' + n);
        path[i] = '\0';
    }
    int s = (int)_onyx_open(path, O_RDWR, 0);
    if (s < 0) {
        errno = (int)(-s);
        _onyx_close(m);
        return -1;
    }
    *mfd = m;
    *sfd = s;
    return 0;
}
