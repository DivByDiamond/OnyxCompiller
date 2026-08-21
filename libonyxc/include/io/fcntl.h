/*
 * fcntl.h — file control options (libonyxc v0.5).
 *
 * Wraps the kernel fcntl syscall. Provides Linux-compatible flag values.
 */
#ifndef _ONYX_FCNTL_H
#define _ONYX_FCNTL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* open() flags — must match kernel abi.rs. */
#ifndef O_RDONLY
#define O_RDONLY    0x0
#endif
#ifndef O_WRONLY
#define O_WRONLY    0x1
#endif
#ifndef O_RDWR
#define O_RDWR      0x2
#endif
#ifndef O_ACCMODE
#define O_ACCMODE   0x3
#endif
#ifndef O_CREAT
#define O_CREAT     0x40
#endif
#ifndef O_EXCL
#define O_EXCL      0x80
#endif
#ifndef O_TRUNC
#define O_TRUNC     0x200
#endif
#ifndef O_APPEND
#define O_APPEND    0x400
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x800
#endif
#ifndef O_DIRECTORY
#define O_DIRECTORY 0x10000
#endif

/* File access modes for open() mode argument. */
#define S_IRUSR     0400
#define S_IWUSR     0200
#define S_IXUSR     0100
#define S_IRGRP     0040
#define S_IWGRP     0020
#define S_IXGRP     0010
#define S_IROTH     0004
#define S_IWOTH     0002
#define S_IXOTH     0001
#define S_IRWXU     0700
#define S_IRWXG     0070
#define S_IRWXO     0007

/* fcntl() commands — must match kernel abi.rs. */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define FD_CLOEXEC  1

/* Declared in unistd.h via onyxc.h's _onyx_* wrappers. */

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_FCNTL_H */
