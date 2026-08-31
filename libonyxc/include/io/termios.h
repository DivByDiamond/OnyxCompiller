/*
 * termios.h — terminal interface for libonyxc.
 *
 * Provides the POSIX termios subset needed by full-screen terminal
 * programs (editors, monitors): raw mode, echo control, canonical mode
 * toggle, VMIN/VTIME. Backed by TCGETS/TCSETS ioctls against the kernel's
 * per-process terminal state.
 */
#ifndef _LIBONYXC_TERMIOS_H
#define _LIBONYXC_TERMIOS_H

/* ── Input modes (c_iflag) ──────────────────────────────────────────── */
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

/* ── Output modes (c_oflag) ─────────────────────────────────────────── */
#define OPOST   0000001
#define OLCUC   0000002
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define OFILL   0000100
#define OFDEL   0000200

/* ── Control modes (c_cflag) ────────────────────────────────────────── */
#define CSIZE   0000060
#define CS5     0000000
#define CS6     0000020
#define CS7     0000040
#define CS8     0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000

/* Baud rates (c_cflag bits, subset). */
#define B0      0000000
#define B50     0000001
#define B75     0000002
#define B110    0000003
#define B134    0000004
#define B150    0000005
#define B200    0000006
#define B300    0000007
#define B600    0000010
#define B1200   0000011
#define B1800   0000012
#define B2400   0000013
#define B4800   0000014
#define B9600   0000015
#define B19200  0000016
#define B38400  0000017

/* ── Local modes (c_lflag) ──────────────────────────────────────────── */
#define ISIG    0000001
#define ICANON  0000002
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define IEXTEN  0100000

/* ── Control characters (c_cc indices) ──────────────────────────────── */
#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VTIME     5
#define VMIN      6
#define VSWTC     7
#define VSTART    8
#define VSTOP     9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

#define NCCS 32

typedef unsigned char cc_t;
typedef unsigned int  speed_t;
typedef unsigned int  tcflag_t;

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_line;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

/* ── ioctl requests (match kernel abi.rs) ──────────────────────────── */
#define TCGETS     0x5401
#define TCSETS     0x5402
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCGPTN   0x80045430

/* ── Window size (TIOCGWINSZ/TIOCSWINSZ) ───────────────────────────── */
struct winsize {
    unsigned short ws_row;    /* rows, in characters */
    unsigned short ws_col;    /* columns, in characters */
    unsigned short ws_xpixel; /* horizontal size, pixels (unused) */
    unsigned short ws_ypixel; /* vertical size, pixels (unused) */
};

/* ── Functions ──────────────────────────────────────────────────────── */
int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

int cfgetispeed(const struct termios *termios_p);
int cfgetospeed(const struct termios *termios_p);
int cfsetispeed(struct termios *termios_p, int speed);
int cfsetospeed(struct termios *termios_p, int speed);
int cfmakeraw(struct termios *termios_p);
int cfsetsane(struct termios *termios_p);

/* Allocate a PTY pair (open /dev/ptmx + /dev/pts/N). 0 on success. */
int pty_open(int *mfd, int *sfd);

void cfmakeraw_apply(struct termios *t);   /* alias, no syscall */

#endif /* _LIBONYXC_TERMIOS_H */
