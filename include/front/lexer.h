/*
 * lexer.h — OnyxCC lexer / preprocessor tokenizer.
 */
#ifndef CC_LEXER_H
#define CC_LEXER_H

#include "core/cc.h"

typedef enum {
    T_EOF = 0,
    /* Punctuation */
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COMMA, T_DOT, T_ARROW, T_COLON, T_QUESTION, T_ELLIPSIS,
    /* Assignments */
    T_ASSIGN, T_PLUS_ASSIGN, T_MINUS_ASSIGN, T_STAR_ASSIGN, T_SLASH_ASSIGN,
    T_PERCENT_ASSIGN, T_AND_ASSIGN, T_OR_ASSIGN, T_XOR_ASSIGN,
    T_SHL_ASSIGN, T_SHR_ASSIGN,
    /* Arithmetic */
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_INC, T_DEC,
    /* Shift */
    T_SHL, T_SHR,
    /* Relational */
    T_EQ, T_NE, T_LT, T_GT, T_LE, T_GE,
    /* Logical */
    T_AND, T_OR, T_NOT,
    /* Bitwise */
    T_AMP, T_PIPE, T_CARET, T_TILDE,
    /* Literals */
    T_INT, T_LONG, T_UINT, T_ULONG, T_FLOAT, T_DOUBLE, T_CHAR_LIT, T_STRING,
    /* Identifiers / keywords */
    T_IDENT,
    /* Keywords */
    T_KW_AUTO, T_KW_BREAK, T_KW_CASE, T_KW_CHAR, T_KW_CONST, T_KW_CONTINUE,
    T_KW_DEFAULT, T_KW_DO, T_KW_DOUBLE, T_KW_ELSE, T_KW_ENUM, T_KW_EXTERN,
    T_KW_FLOAT, T_KW_FOR, T_KW_GOTO, T_KW_IF, T_KW_INLINE, T_KW_INT,
    T_KW_LONG, T_KW_REGISTER, T_KW_RESTRICT, T_KW_RETURN, T_KW_SHORT,
    T_KW_SIGNED, T_KW_SIZEOF, T_KW_STATIC, T_KW_STRUCT, T_KW_SWITCH,
    T_KW_TYPEDEF, T_KW_UNION, T_KW_UNSIGNED, T_KW_VOID, T_KW_VOLATILE,
    T_KW_WHILE, T_KW_BOOL, T_KW_COMPLEX, T_KW_IMAGINARY,
    /* C++ */
    T_KW_CLASS, T_KW_NAMESPACE, T_KW_PUBLIC, T_KW_PRIVATE, T_KW_PROTECTED,
    T_KW_TEMPLATE, T_KW_TYPENAME, T_KW_NEW, T_KW_DELETE, T_KW_THIS,
    T_KW_OPERATOR, T_KW_FRIEND, T_KW_VIRTUAL, T_KW_OVERRIDE, T_KW_USING,
    T_KW_STATIC_CAST, T_KW_REINTERPRET_CAST, T_KW_CONST_CAST,
    T_KW_TRY, T_KW_CATCH, T_KW_THROW,
    /* Preprocessor passthrough */
    T_HASH, T_HASHHASH,
    T_KW_ASM,    /* asm / __asm__ */
    /* C++ additions (Phase A). Appended at the end so existing token
     * kind numbering stays stable. */
    T_SCOPE,     /* :: — single token, never two split colons */
    T_KW_NULLPTR, T_KW_CONSTEXPR, T_KW_EXPLICIT, T_KW_DYNAMIC_CAST,
    /* Special */
    T_BUILTIN,   /* __builtin_onyx_* */
} token_kind_t;

typedef struct {
    token_kind_t kind;
    cc_pos_t pos;
    /* Byte offset in lx->src where this token starts. */
    size_t src_off;
    /* For literals and identifiers */
    char text[CC_MAX_TOKEN_LEN];
    /* For integer literals: parsed value */
    uint64_t ival;
    /* For float literals (rarely used; we mostly use double via libm later) */
    double fval;
    /* For string literals: literal bytes (already unescaped). Length stored
     * separately because strings may contain embedded NULs. */
    char *str;
    size_t str_len;
    /* Preprocessor: was this token produced from a macro expansion? */
    bool from_macro;
} token_t;

typedef struct {
    const char *src;       /* current source buffer */
    size_t src_len;
    size_t pos;            /* byte offset in src */
    const char *filename;  /* logical filename */
    int line;
    int col;

    token_t cur;           /* current token (peek) */
    token_t next;          /* one-token lookahead */
    bool has_next;
} lexer_t;

void lex_init(lexer_t *lx, const char *src, size_t len, const char *filename);
void lex_next(lexer_t *lx);          /* fill lx->cur from lx->next or src */
void lex_peek(lexer_t *lx);          /* ensure lx->next is filled */
const char *tok_kind_str(token_kind_t k);

/* At top level the parser uses lx->cur; we expose helpers. */
#define CUR(lx)   (&(lx)->cur)
#define PEEK(lx)  (&(lx)->next)

#endif /* CC_LEXER_H */
