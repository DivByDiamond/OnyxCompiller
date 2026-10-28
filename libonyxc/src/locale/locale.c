/* C/POSIX locale only. Unsupported locales fail without changing state. */
#include "onyxc.h"
#include <locale.h>

static char locale_name[] = "C";
static struct lconv c_locale = {
    ".", "", "", "", "",
    "", "", "", "", "",
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX, CHAR_MAX,
    CHAR_MAX, CHAR_MAX
};
static const char *category_names[] = {
    "LC_CTYPE", "LC_NUMERIC", "LC_TIME", "LC_COLLATE", "LC_MONETARY",
    "LC_MESSAGES"
};
static int supported(const char *name) {
    return strcmp(name, "C") == 0 || strcmp(name, "POSIX") == 0;
}
static const char *environment_locale(int category) {
    char *name = getenv("LC_ALL");
    if (name && *name) return name;
    name = getenv(category_names[category]);
    if (name && *name) return name;
    name = getenv("LANG");
    if (name && *name) return name;
    return "C";
}
char *setlocale(int category, const char *locale) {
    int i;
    if (category < LC_CTYPE || category > LC_ALL) return NULL;
    if (!locale) return locale_name;
    if (*locale) return supported(locale) ? locale_name : NULL;
    /* Validate every category before accepting an LC_ALL environment request. */
    if (category == LC_ALL) {
        for (i = LC_CTYPE; i < LC_ALL; ++i)
            if (!supported(environment_locale(i))) return NULL;
    } else if (!supported(environment_locale(category))) return NULL;
    return locale_name;
}
struct lconv *localeconv(void) {
    return &c_locale;
}
