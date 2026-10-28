// EXPECT: locale-ok
#include <locale.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern char **environ;
static char *env_c[] = {"LC_ALL=POSIX", NULL};
static char *env_missing[] = {"LC_ALL=missing", NULL};
static char *env_numeric[] = {"LANG=C", "LC_NUMERIC=missing", NULL};
static char *env_lang[] = {"LANG=C", NULL};
static char *env_empty[] = {NULL};
int main(void) {
    struct lconv *lc;
    int category;
    if (FLT_RADIX != 2 || FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53) return 1;
    if (LDBL_MANT_DIG != DBL_MANT_DIG || DECIMAL_DIG != 17) return 2;
    if (FLT_MIN <= 0 || DBL_MIN <= 0 || FLT_MAX <= 1 || DBL_MAX <= 1) return 3;
    if (FLT_EPSILON <= 0 || DBL_EPSILON <= 0) return 4;
    for (category = LC_CTYPE; category <= LC_ALL; ++category) {
        if (strcmp(setlocale(category, NULL), "C")) return 5;
        if (!setlocale(category, "POSIX")) return 6;
        if (!setlocale(category, "C")) return 7;
        if (setlocale(category, "unsupported.UTF-8") != NULL) return 8;
        if (strcmp(setlocale(category, NULL), "C")) return 9;
    }
    if (setlocale(-1, "C") || setlocale(99, NULL)) return 10;
    lc = localeconv();
    if (strcmp(lc->decimal_point, ".") || *lc->thousands_sep || *lc->grouping) return 11;
    if (*lc->int_curr_symbol || *lc->currency_symbol || *lc->mon_decimal_point) return 12;
    if (*lc->mon_thousands_sep || *lc->mon_grouping || *lc->positive_sign || *lc->negative_sign) return 13;
    if (lc->frac_digits != CHAR_MAX || lc->int_frac_digits != CHAR_MAX) return 14;
    if (lc->p_cs_precedes != CHAR_MAX || lc->p_sep_by_space != CHAR_MAX) return 15;
    if (lc->n_cs_precedes != CHAR_MAX || lc->n_sep_by_space != CHAR_MAX) return 16;
    if (lc->p_sign_posn != CHAR_MAX || lc->n_sign_posn != CHAR_MAX) return 17;
    if (lc->int_p_cs_precedes != CHAR_MAX || lc->int_p_sep_by_space != CHAR_MAX) return 18;
    if (lc->int_n_cs_precedes != CHAR_MAX || lc->int_n_sep_by_space != CHAR_MAX) return 19;
    if (lc->int_p_sign_posn != CHAR_MAX || lc->int_n_sign_posn != CHAR_MAX) return 20;
    environ = env_c;
    if (!setlocale(LC_ALL, "")) return 21;
    environ = env_missing;
    if (setlocale(LC_ALL, "")) return 22;
    if (strcmp(setlocale(LC_ALL, NULL), "C")) return 23;
    environ = env_numeric;
    if (setlocale(LC_ALL, "") || setlocale(LC_NUMERIC, "")) return 24;
    if (!setlocale(LC_TIME, "")) return 25;
    environ = env_lang;
    if (!setlocale(LC_ALL, "")) return 26;
    environ = env_empty;
    if (!setlocale(LC_ALL, "")) return 27;
    printf("locale-ok\n");
    return 0;
}
