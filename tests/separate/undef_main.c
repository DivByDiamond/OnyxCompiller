/* Negative fixture: sep_missing() is never defined anywhere.
 * Linking this object MUST fail. */

extern int sep_missing(void);

int main(void) {
    return sep_missing();
}
