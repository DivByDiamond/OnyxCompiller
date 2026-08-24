/*
 * assert.h — runtime assertions for libonyxc.
 */
#undef assert

#ifdef NDEBUG
#define assert(x) ((void)0)
#else
/* __ONYX_FILE__ is substituted by the preprocessor; __LINE__ is the
 * line number macro. */
void __assert_fail(const char *expr, const char *file, int line,
                   const char *func);
#define assert(x) \
    ((x) ? (void)0 : __assert_fail(#x, __FILE__, __LINE__, __func__))

/* __func__ is provided by the compiler as a per-function string constant
 * (recognized in parse_primary). */
#endif
