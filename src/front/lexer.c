/*
 * lexer.c — OnyxCC lexer.
 *
 * Tokenizes C99 + small C++ superset. Skips comments and whitespace.
 * String and char escapes are unescaped here; the parser sees only
 * final values.
 *
 * Numeric literals are partially parsed: ival holds the integer value
 * for decimal/hex/octal. Float is parsed on demand by the parser via
 * strtod.
 *
 * The lexer is table-free and uses first-char dispatch for speed.
 */
#include "core/compat.h"

#include "core/cc.h"
#include "front/lexer.h"

/* ---- Keyword table --------------------------------------------------- */
typedef struct { const char *name; token_kind_t kind; } kw_entry_t;

static const kw_entry_t kws[] = {
    {"auto",       T_KW_AUTO},
    {"break",      T_KW_BREAK},
    {"case",       T_KW_CASE},
    {"char",       T_KW_CHAR},
    {"class",      T_KW_CLASS},
    {"const",      T_KW_CONST},
    {"const_cast", T_KW_CONST_CAST},
    {"continue",   T_KW_CONTINUE},
    {"default",    T_KW_DEFAULT},
    {"delete",     T_KW_DELETE},
    {"do",         T_KW_DO},
    {"double",     T_KW_DOUBLE},
    {"else",       T_KW_ELSE},
    {"enum",       T_KW_ENUM},
    {"extern",     T_KW_EXTERN},
    {"float",      T_KW_FLOAT},
    {"for",        T_KW_FOR},
    {"friend",     T_KW_FRIEND},
    {"goto",       T_KW_GOTO},
    {"if",         T_KW_IF},
    {"inline",     T_KW_INLINE},
    {"int",        T_KW_INT},
    {"long",       T_KW_LONG},
    {"namespace",  T_KW_NAMESPACE},
    {"new",        T_KW_NEW},
    {"operator",   T_KW_OPERATOR},
    {"override",   T_KW_OVERRIDE},
    {"private",    T_KW_PRIVATE},
    {"protected",  T_KW_PROTECTED},
    {"public",     T_KW_PUBLIC},
    {"register",   T_KW_REGISTER},
    {"reinterpret_cast", T_KW_REINTERPRET_CAST},
    {"restrict",   T_KW_RESTRICT},
    {"return",     T_KW_RETURN},
    {"short",      T_KW_SHORT},
    {"signed",     T_KW_SIGNED},
    {"sizeof",     T_KW_SIZEOF},
    {"static",     T_KW_STATIC},
    {"static_cast",T_KW_STATIC_CAST},
    {"struct",     T_KW_STRUCT},
    {"switch",     T_KW_SWITCH},
    {"template",   T_KW_TEMPLATE},
    {"this",       T_KW_THIS},
    {"throw",      T_KW_THROW},
    {"try",        T_KW_TRY},
    {"catch",      T_KW_CATCH},
    {"typedef",    T_KW_TYPEDEF},
    {"typename",   T_KW_TYPENAME},
    {"union",      T_KW_UNION},
    {"unsigned",   T_KW_UNSIGNED},
    {"using",      T_KW_USING},
    {"virtual",    T_KW_VIRTUAL},
    {"void",       T_KW_VOID},
    {"volatile",   T_KW_VOLATILE},
    {"while",      T_KW_WHILE},
    {"_Bool",      T_KW_BOOL},
    {"__builtin_va_list", T_KW_TYPEDEF},
    {"__attribute__", T_IDENT},   /* tokenized as ident; parser skips args */
    {"__attribute",   T_IDENT},
    {"__extension__", T_IDENT},
    {"__inline",      T_KW_INLINE},
    {"__inline__",    T_KW_INLINE},
    {"__restrict",    T_KW_RESTRICT},
    {"__restrict__",  T_KW_RESTRICT},
    {"__volatile",    T_KW_VOLATILE},
    {"__volatile__",  T_KW_VOLATILE},
    {"__const",       T_KW_CONST},
    {"__const__",     T_KW_CONST},
    {"__signed",      T_KW_SIGNED},
    {"__signed__",    T_KW_SIGNED},
    {"asm",           T_KW_ASM},
    {"__asm__",       T_KW_ASM},
};

#define N_KWS (int)(sizeof(kws) / sizeof(kws[0]))

static token_kind_t lookup_kw(const char *s, size_t n) {
    /* Linear scan; table is ~50 entries, fast enough. */
    for (int i = 0; i < N_KWS; i++) {
        const char *k = kws[i].name;
        size_t klen = strlen(k);
        if (klen == n && memcmp(k, s, n) == 0) return kws[i].kind;
    }
    return T_IDENT;
}

/* ---- Init ------------------------------------------------------------ */
void lex_init(lexer_t *lx, const char *src, size_t len, const char *filename) {
    memset(lx, 0, sizeof(*lx));
    lx->src = src;
    lx->src_len = len;
    lx->pos = 0;
    lx->filename = filename;
    lx->line = 1;
    lx->col = 1;
    lx->has_next = false;
    /* Prime cur with first token. */
    lex_next(lx);
}

/* ---- Helpers --------------------------------------------------------- */
static int peek_ch(lexer_t *lx, int ahead) {
    size_t p = lx->pos + ahead;
    if (p >= lx->src_len) return -1;
    return (unsigned char)lx->src[p];
}

static int cur_ch(lexer_t *lx) {
    if (lx->pos >= lx->src_len) return -1;
    return (unsigned char)lx->src[lx->pos];
}

static void advance(lexer_t *lx) {
    int c = cur_ch(lx);
    if (c < 0) return;
    lx->pos++;
    if (c == '\n') { lx->line++; lx->col = 1; }
    else { lx->col++; }
}

static void skip_ws(lexer_t *lx) {
    for (;;) {
        int c = cur_ch(lx);
        if (c < 0) return;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
            advance(lx);
            continue;
        }
        /* Line comment */
        if (c == '/' && peek_ch(lx, 1) == '/') {
            while (cur_ch(lx) >= 0 && cur_ch(lx) != '\n') advance(lx);
            continue;
        }
        /* Block comment */
        if (c == '/' && peek_ch(lx, 1) == '*') {
            advance(lx); advance(lx);
            while (cur_ch(lx) >= 0) {
                if (cur_ch(lx) == '*' && peek_ch(lx, 1) == '/') {
                    advance(lx); advance(lx);
                    break;
                }
                advance(lx);
            }
            continue;
        }
        return;
    }
}

/* ---- Token fillers --------------------------------------------------- */
static void set_text(token_t *t, const char *s, size_t n) {
    if (n >= CC_MAX_TOKEN_LEN) n = CC_MAX_TOKEN_LEN - 1;
    memcpy(t->text, s, n);
    t->text[n] = 0;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Read an escape sequence; lx is positioned just after the backslash.
 * Returns the unescaped char or -1 on error. */
static int read_escape(lexer_t *lx) {
    int c = cur_ch(lx);
    if (c < 0) return -1;
    advance(lx);
    switch (c) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '0':  return '\0';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"':  return '"';
        case 'a':  return '\a';
        case 'b':  return '\b';
        case 'f':  return '\f';
        case 'v':  return '\v';
        case '?':  return '?';
        case 'x': {
            int v = 0, h;
            while ((h = hexval(cur_ch(lx))) >= 0) {
                v = (v << 4) | h;
                advance(lx);
            }
            return v & 0xff;
        }
        default:
            if (c >= '0' && c <= '7') {
                int v = c - '0';
                for (int i = 0; i < 2; i++) {
                    int d = cur_ch(lx);
                    if (d >= '0' && d <= '7') {
                        v = (v << 3) | (d - '0');
                        advance(lx);
                    } else break;
                }
                return v & 0xff;
            }
            cc_warn("unknown escape \\%c", c);
            return c;
    }
}

static void lex_string(lexer_t *lx, token_t *t) {
    /* cur is opening quote */
    advance(lx);
    /* Use a small dynamic buffer. Most strings are short. */
    static char buf[65536];
    size_t len = 0;
    for (;;) {
        int c = cur_ch(lx);
        if (c < 0) {
            cc_error_at(NULL, lx->line, "unterminated string literal");
            break;
        }
        if (c == '"') {
            advance(lx);
            break;
        }
        if (c == '\\') {
            advance(lx);
            int e = read_escape(lx);
            if (e >= 0) {
                if (len < sizeof(buf) - 1) buf[len++] = (char)e;
            }
            continue;
        }
        if (c == '\n') {
            cc_error_at(NULL, lx->line, "newline in string literal");
            break;
        }
        if (len < sizeof(buf) - 1) buf[len++] = (char)c;
        advance(lx);
    }
    /* Allocate persistent copy. */
    t->str = (char *)malloc(len + 1);
    if (!t->str) cc_fatal("oom: string literal");
    memcpy(t->str, buf, len);
    t->str[len] = 0;
    t->str_len = len;
    t->kind = T_STRING;
}

static void lex_char_lit(lexer_t *lx, token_t *t) {
    advance(lx);
    int v;
    int c = cur_ch(lx);
    if (c == '\\') {
        advance(lx);
        v = read_escape(lx);
    } else {
        v = c;
        advance(lx);
    }
    if (cur_ch(lx) == '\'') {
        advance(lx);
    } else {
        cc_error_at(NULL, lx->line, "unterminated char literal");
    }
    t->kind = T_CHAR_LIT;
    t->ival = (unsigned)v & 0xff;
}

static void lex_number(lexer_t *lx, token_t *t) {
    const char *start = lx->src + lx->pos;
    size_t start_off = lx->pos;
    bool is_float = false;
    bool is_hex = false;

    int c = cur_ch(lx);
    if (c == '0' && (peek_ch(lx, 1) == 'x' || peek_ch(lx, 1) == 'X')) {
        is_hex = true;
        advance(lx); advance(lx);
        while (isxdigit(cur_ch(lx)) || cur_ch(lx) == '_') advance(lx);
    } else {
        while (isdigit(cur_ch(lx)) || cur_ch(lx) == '_') advance(lx);
        if (cur_ch(lx) == '.') {
            is_float = true;
            advance(lx);
            while (isdigit(cur_ch(lx)) || cur_ch(lx) == '_') advance(lx);
        }
        if (cur_ch(lx) == 'e' || cur_ch(lx) == 'E') {
            is_float = true;
            advance(lx);
            if (cur_ch(lx) == '+' || cur_ch(lx) == '-') advance(lx);
            while (isdigit(cur_ch(lx))) advance(lx);
        }
    }

    /* Suffix: u/U, l/L, ll/LL, ul/UL, f/F, etc. We swallow them. */
    bool is_unsigned = false;
    bool is_long = false;
    bool is_longlong = false;
    for (;;) {
        int c2 = cur_ch(lx);
        if (c2 == 'u' || c2 == 'U') { is_unsigned = true; advance(lx); }
        else if (c2 == 'l' || c2 == 'L') {
            if (is_long) is_longlong = true;
            is_long = true;
            advance(lx);
        }
        else if (c2 == 'f' || c2 == 'F') { is_float = true; advance(lx); }
        else break;
    }

    size_t n = lx->pos - start_off;
    set_text(t, start, n);

    if (is_float) {
        t->kind = T_DOUBLE;
        /* Strip underscores for strtod. */
        char buf[64];
        size_t j = 0;
        for (size_t i = 0; i < n && j < sizeof(buf) - 1; i++) {
            if (start[i] != '_') buf[j++] = start[i];
        }
        buf[j] = 0;
        t->fval = strtod(buf, NULL);
    } else {
        /* Parse integer value. */
        uint64_t v = 0;
        if (is_hex) {
            const char *p = start + 2;
            while (p < start + n) {
                if (*p == '_') { p++; continue; }
                int d = hexval(*p);
                if (d < 0) break;
                v = (v << 4) | d;
                p++;
            }
        } else {
            const char *p = start;
            while (p < start + n) {
                if (*p == '_') { p++; continue; }
                if (!isdigit(*p)) break;
                v = v * 10 + (*p - '0');
                p++;
            }
        }
        t->ival = v;
        if (is_unsigned && is_longlong) t->kind = T_ULONG;
        else if (is_longlong) t->kind = T_LONG;
        else if (is_unsigned) t->kind = T_UINT;
        else t->kind = T_INT;
    }
}

static void lex_ident(lexer_t *lx, token_t *t) {
    const char *start = lx->src + lx->pos;
    size_t start_off = lx->pos;
    advance(lx);
    int c;
    while ((c = cur_ch(lx)) >= 0) {
        if (isalnum(c) || c == '_') advance(lx);
        else break;
    }
    size_t n = lx->pos - start_off;
    set_text(t, start, n);
    t->kind = lookup_kw(start, n);
}

/* Read the next token from src into t. Does not skip preprocessor
 * directives — the preprocessor layer (pp.c) handles those. */
static void lex_one(lexer_t *lx, token_t *t) {
    memset(t, 0, sizeof(*t));
    skip_ws(lx);
    t->src_off = lx->pos;
    t->pos.file = lx->filename;
    t->pos.line = lx->line;
    t->pos.col = lx->col;

    int c = cur_ch(lx);
    if (c < 0) { t->kind = T_EOF; return; }

    /* Identifier / keyword */
    if (isalpha(c) || c == '_') { lex_ident(lx, t); return; }

    /* Number */
    if (isdigit(c)) { lex_number(lx, t); return; }

    /* String */
    if (c == '"') { lex_string(lx, t); return; }

    /* Char literal */
    if (c == '\'') { lex_char_lit(lx, t); return; }

    /* Punctuation */
    advance(lx);
    switch (c) {
        case '(': t->kind = T_LPAREN; return;
        case ')': t->kind = T_RPAREN; return;
        case '{': t->kind = T_LBRACE; return;
        case '}': t->kind = T_RBRACE; return;
        case '[': t->kind = T_LBRACKET; return;
        case ']': t->kind = T_RBRACKET; return;
        case ';': t->kind = T_SEMI; return;
        case ',': t->kind = T_COMMA; return;
        case '~': t->kind = T_TILDE; return;
        case '?': t->kind = T_QUESTION; return;
        case ':': t->kind = T_COLON; return;
        case '.': {
            if (cur_ch(lx) == '.' && peek_ch(lx, 1) == '.') {
                advance(lx); advance(lx);
                t->kind = T_ELLIPSIS; return;
            }
            t->kind = T_DOT; return;
        }
        case '+':
            if (cur_ch(lx) == '+') { advance(lx); t->kind = T_INC; return; }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_PLUS_ASSIGN; return; }
            t->kind = T_PLUS; return;
        case '-':
            if (cur_ch(lx) == '-') { advance(lx); t->kind = T_DEC; return; }
            if (cur_ch(lx) == '>') { advance(lx); t->kind = T_ARROW; return; }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_MINUS_ASSIGN; return; }
            t->kind = T_MINUS; return;
        case '*':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_STAR_ASSIGN; return; }
            t->kind = T_STAR; return;
        case '/':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_SLASH_ASSIGN; return; }
            t->kind = T_SLASH; return;
        case '%':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_PERCENT_ASSIGN; return; }
            t->kind = T_PERCENT; return;
        case '=':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_EQ; return; }
            t->kind = T_ASSIGN; return;
        case '!':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_NE; return; }
            t->kind = T_NOT; return;
        case '<':
            if (cur_ch(lx) == '<') {
                advance(lx);
                if (cur_ch(lx) == '=') { advance(lx); t->kind = T_SHL_ASSIGN; return; }
                t->kind = T_SHL; return;
            }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_LE; return; }
            t->kind = T_LT; return;
        case '>':
            if (cur_ch(lx) == '>') {
                advance(lx);
                if (cur_ch(lx) == '=') { advance(lx); t->kind = T_SHR_ASSIGN; return; }
                t->kind = T_SHR; return;
            }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_GE; return; }
            t->kind = T_GT; return;
        case '&':
            if (cur_ch(lx) == '&') { advance(lx); t->kind = T_AND; return; }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_AND_ASSIGN; return; }
            t->kind = T_AMP; return;
        case '|':
            if (cur_ch(lx) == '|') { advance(lx); t->kind = T_OR; return; }
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_OR_ASSIGN; return; }
            t->kind = T_PIPE; return;
        case '^':
            if (cur_ch(lx) == '=') { advance(lx); t->kind = T_XOR_ASSIGN; return; }
            t->kind = T_CARET; return;
        case '#':
            if (cur_ch(lx) == '#') { advance(lx); t->kind = T_HASHHASH; return; }
            t->kind = T_HASH; return;
        default:
            cc_error_at(NULL, lx->line, "unexpected char 0x%02x", (unsigned)c);
            t->kind = T_EOF;
            return;
    }
}

void lex_next(lexer_t *lx) {
    if (lx->has_next) {
        lx->cur = lx->next;
        lx->has_next = false;
        lex_peek(lx);
    } else {
        lex_one(lx, &lx->cur);
    }
}

void lex_peek(lexer_t *lx) {
    if (!lx->has_next) {
        lex_one(lx, &lx->next);
        lx->has_next = true;
    }
}

const char *tok_kind_str(token_kind_t k) {
    switch (k) {
        case T_EOF: return "EOF";
        case T_LPAREN: return "(";
        case T_RPAREN: return ")";
        case T_LBRACE: return "{";
        case T_RBRACE: return "}";
        case T_LBRACKET: return "[";
        case T_RBRACKET: return "]";
        case T_SEMI: return ";";
        case T_COMMA: return ",";
        case T_DOT: return ".";
        case T_ARROW: return "->";
        case T_COLON: return ":";
        case T_QUESTION: return "?";
        case T_ELLIPSIS: return "...";
        case T_ASSIGN: return "=";
        case T_PLUS: return "+";
        case T_MINUS: return "-";
        case T_STAR: return "*";
        case T_SLASH: return "/";
        case T_PERCENT: return "%";
        case T_INC: return "++";
        case T_DEC: return "--";
        case T_IDENT: return "IDENT";
        case T_INT: return "INT";
        case T_STRING: return "STRING";
        default: return "?";
    }
}
