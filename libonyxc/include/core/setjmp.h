/* OnyxCC inline contexts for RV64 LP64D: SP, s0..s11, fs0..fs11,
 * fcsr and continuation PC. Not an external assembly implementation.
 * The buffer belongs to its still-active calling frame; longjmp does not
 * restore signal masks. Modified automatic locals must be volatile. */
#ifndef _ONYX_SETJMP_H
#define _ONYX_SETJMP_H

typedef unsigned long jmp_buf[27];
#define setjmp(env) __builtin_onyx_setjmp(env)
#define longjmp(env, value) __builtin_onyx_longjmp(env, value)

#endif
