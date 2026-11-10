/* Negative fixture: duplicate global definition with dup_a.c.
 * Linking both objects MUST fail. */

int sep_clash(void) { return 2; }
