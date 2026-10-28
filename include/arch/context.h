/* Inline RV64 LP64D nonlocal context operations. */
#ifndef CC_CONTEXT_H
#define CC_CONTEXT_H

/* env_reg holds a writable/readable 27-word context address. Both operations
 * clobber caller-saved registers. setjmp leaves its integer result in T0;
 * longjmp transfers control and does not return to its emission site. */
void rv_setjmp(int env_reg);
void rv_longjmp(int env_reg, int value_reg);

#endif
