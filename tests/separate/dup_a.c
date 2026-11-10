/* Negative fixture: duplicate global definition with dup_b.c.
 * Linking both objects MUST fail. */

int sep_clash(void) { return 1; }
