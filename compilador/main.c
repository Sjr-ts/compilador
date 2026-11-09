#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NAME 96
#define MAX_TEXT 256
#define MAX_PARAMS 32
#define MAX_ERRORS 4096
#define MAX_OPTS 4096

typedef enum {
    T_EOF, T_ID, T_INT_LITERAL, T_FLOAT_LITERAL, T_CHAR_LITERAL, T_STRING_LITERAL,
    K_INT, K_FLOAT, K_DOUBLE, K_CHAR, K_VOID, K_CONST, K_STRUCT,
    K_RETURN, K_IF, K_ELSE, K_FOR, K_WHILE, K_DO, K_BREAK, K_CONTINUE,
    K_SWITCH, K_CASE, K_DEFAULT, K_SIZEOF,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET,
    T_SEMI, T_COMMA, T_DOT, T_QUESTION, T_COLON,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_INC, T_DEC, T_ASSIGN, T_ADD_ASSIGN, T_SUB_ASSIGN, T_MUL_ASSIGN, T_DIV_ASSIGN,
    T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_AND, T_OR, T_NOT, T_BITAND, T_BITOR,
    T_XOR, T_TILDE, T_ARROW, T_INVALID
} TokenKind;

typedef struct {
    TokenKind kind;
    char text[MAX_TEXT];
    int line;
    int col;
} Token;

typedef struct {
    Token *data;
    size_t count;
    size_t cap;
} TokenVec;

typedef enum { TY_ERROR, TY_VOID, TY_INT, TY_FLOAT, TY_CHAR, TY_STRING, TY_STRUCT } TypeKind;

typedef struct {
    TypeKind kind;
    char tag[MAX_NAME];
    int is_array;
} Type;

typedef struct Symbol {
    char name[MAX_NAME];
    Type type;
    int line;
    int depth;
    int is_function;
    int is_const;
    int has_const;
    double const_value;
    int initialized;
    int reads;
    int defined;
    int variadic;
    int param_count;
    Type params[MAX_PARAMS];
    struct Symbol *next;
} Symbol;

typedef struct {
    int line;
    int col;
    char phase[20];
    char message[MAX_TEXT];
} Diagnostic;

typedef struct {
    int line;
    char kind[48];
    char before[MAX_TEXT];
    char after[MAX_TEXT];
} Optimization;

typedef struct {
    Type type;
    Symbol *symbol;
    int is_lvalue;
    int is_constant;
    double value;
    int syntax_bad;
    char repr[MAX_TEXT];
} Expr;

typedef struct {
    TokenVec tokens;
    size_t pos;
    Symbol *symbols;
    int scope_depth;
    Symbol *current_function;
    Type return_type;
    int loop_depth;
    char loop_induction[MAX_NAME];
    Diagnostic errors[MAX_ERRORS];
    int error_count;
    int lex_count;
    int syntax_count;
    int semantic_count;
    Optimization opts[MAX_OPTS];
    int opt_count;
    char seen_expr[512][MAX_TEXT];
    int seen_expr_count;
    char propagated[MAX_PARAMS * 8][MAX_NAME];
    int propagated_count;
    int had_lex_error;
    int had_syntax_error;
} Compiler;


static void copy_text(char *dst, size_t cap, const char *src) {
    if (!cap) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void format_text(char *dst, size_t cap, const char *fmt, ...) {
    if (!cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
}

static Type make_type(TypeKind kind) {
    Type t;
    memset(&t, 0, sizeof(t));
    t.kind = kind;
    return t;
}

static int type_equal(Type a, Type b) {
    if (a.kind != b.kind) return 0;
    if (a.kind == TY_STRUCT && strcmp(a.tag, b.tag) != 0) return 0;
    return a.is_array == b.is_array;
}

static int type_assignable(Type dst, Type src) {
    if (dst.kind == TY_ERROR || src.kind == TY_ERROR) return 1;
    if (type_equal(dst, src)) return 1;
    if (!dst.is_array && !src.is_array && dst.kind == TY_FLOAT && (src.kind == TY_INT || src.kind == TY_CHAR)) return 1;
    if (!dst.is_array && !src.is_array && dst.kind == TY_INT && src.kind == TY_CHAR) return 1;
    return 0;
}

static const char *type_name(Type t) {
    static char buffers[4][MAX_NAME];
    static unsigned index = 0;
    char *out = buffers[(index++) % 4];
    switch (t.kind) {
        case TY_VOID: snprintf(out, MAX_NAME, "void"); break;
        case TY_INT: snprintf(out, MAX_NAME, "int"); break;
        case TY_FLOAT: snprintf(out, MAX_NAME, "float"); break;
        case TY_CHAR: snprintf(out, MAX_NAME, "char"); break;
        case TY_STRING: snprintf(out, MAX_NAME, "string"); break;
        case TY_STRUCT: snprintf(out, MAX_NAME, "%s", t.tag); break;
        case TY_ERROR: snprintf(out, MAX_NAME, "tipo invalido"); break;
        default: snprintf(out, MAX_NAME, "desconhecido"); break;
    }
    return out;
}

static void diagnostic(Compiler *c, int line, int col, const char *phase, const char *fmt, ...) {
    if (c->error_count >= MAX_ERRORS) return;
    Diagnostic *d = &c->errors[c->error_count++];
    d->line = line;
    d->col = col;
    snprintf(d->phase, sizeof(d->phase), "%s", phase);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->message, sizeof(d->message), fmt, ap);
    va_end(ap);
    if (strcmp(phase, "LEXICO") == 0) { c->lex_count++; c->had_lex_error = 1; }
    else if (strcmp(phase, "SINTATICO") == 0) { c->syntax_count++; c->had_syntax_error = 1; }
    else if (strcmp(phase, "SEMANTICO") == 0) c->semantic_count++;
}

static void optimization(Compiler *c, int line, const char *kind, const char *before, const char *after) {
    if (c->opt_count >= MAX_OPTS) return;
    Optimization *o = &c->opts[c->opt_count++];
    o->line = line;
    snprintf(o->kind, sizeof(o->kind), "%s", kind);
    snprintf(o->before, sizeof(o->before), "%s", before);
    snprintf(o->after, sizeof(o->after), "%s", after);
}

static void token_push(TokenVec *v, Token t) {
    if (v->count == v->cap) {
        size_t next = v->cap ? v->cap * 2 : 256;
        Token *p = (Token *)realloc(v->data, next * sizeof(Token));
        if (!p) { fprintf(stderr, "Memoria insuficiente.\n"); exit(2); }
        v->data = p;
        v->cap = next;
    }
    v->data[v->count++] = t;
}

static Token make_token(TokenKind kind, const char *src, size_t len, int line, int col) {
    Token t;
    t.kind = kind;
    size_t n = len < MAX_TEXT - 1 ? len : MAX_TEXT - 1;
    memcpy(t.text, src, n);
    t.text[n] = '\0';
    t.line = line;
    t.col = col;
    return t;
}

static TokenKind keyword_kind(const char *s) {
    struct Pair { const char *s; TokenKind k; } words[] = {
        {"int",K_INT},{"float",K_FLOAT},{"double",K_DOUBLE},{"char",K_CHAR},{"void",K_VOID},
        {"const",K_CONST},{"struct",K_STRUCT},{"return",K_RETURN},{"if",K_IF},{"else",K_ELSE},
        {"for",K_FOR},{"while",K_WHILE},{"do",K_DO},{"break",K_BREAK},{"continue",K_CONTINUE},
        {"switch",K_SWITCH},{"case",K_CASE},{"default",K_DEFAULT},{"sizeof",K_SIZEOF}
    };
    size_t n = sizeof(words) / sizeof(words[0]);
    for (size_t i = 0; i < n; i++) if (strcmp(s, words[i].s) == 0) return words[i].k;
    return T_ID;
}

static int is_ident_start(int c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_ident_part(int c) { return isalnum((unsigned char)c) || c == '_'; }

static void lex_source(Compiler *c, const char *src) {
    size_t i = 0;
    int line = 1, col = 1, line_start = 1;
    while (src[i]) {
        char ch = src[i];
        if (line_start && (ch == ' ' || ch == '\t' || ch == '\r')) { i++; col++; continue; }
        if (line_start && ch == '#') {
            while (src[i] && src[i] != '\n') { i++; col++; }
            continue;
        }
        line_start = 0;
        if (ch == '\n') { i++; line++; col = 1; line_start = 1; continue; }
        if (isspace((unsigned char)ch)) { i++; col++; continue; }
        if (ch == '/' && src[i + 1] == '/') {
            while (src[i] && src[i] != '\n') { i++; col++; }
            continue;
        }
        if (ch == '/' && src[i + 1] == '*') {
            int start_line = line, start_col = col;
            i += 2; col += 2;
            while (src[i] && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') { i++; line++; col = 1; line_start = 1; }
                else { i++; col++; }
            }
            if (src[i]) { i += 2; col += 2; }
            else diagnostic(c, start_line, start_col, "LEXICO", "comentario de bloco nao foi fechado");
            continue;
        }
        int start_line = line, start_col = col;
        size_t start = i;
        if (is_ident_start(ch)) {
            i++; col++;
            while (is_ident_part(src[i])) { i++; col++; }
            Token t = make_token(T_ID, src + start, i - start, start_line, start_col);
            t.kind = keyword_kind(t.text);
            token_push(&c->tokens, t);
            continue;
        }
        if (isdigit((unsigned char)ch)) {
            int is_float = 0;
            i++; col++;
            while (isdigit((unsigned char)src[i])) { i++; col++; }
            if (src[i] == '.' && isdigit((unsigned char)src[i + 1])) {
                is_float = 1; i++; col++;
                while (isdigit((unsigned char)src[i])) { i++; col++; }
            }
            if (src[i] == 'e' || src[i] == 'E') {
                size_t exp_pos = i;
                size_t j = i + 1;
                if (src[j] == '+' || src[j] == '-') j++;
                if (isdigit((unsigned char)src[j])) {
                    is_float = 1; i = j + 1; col += (int)(i - exp_pos);
                    while (isdigit((unsigned char)src[i])) { i++; col++; }
                }
            }
            token_push(&c->tokens, make_token(is_float ? T_FLOAT_LITERAL : T_INT_LITERAL, src + start, i - start, start_line, start_col));
            continue;
        }
        if (ch == '"' || ch == '\'') {
            char quote = ch;
            i++; col++;
            int closed = 0;
            while (src[i]) {
                if (src[i] == '\\' && src[i + 1]) { i += 2; col += 2; continue; }
                if (src[i] == quote) { i++; col++; closed = 1; break; }
                if (src[i] == '\n') break;
                i++; col++;
            }
            if (!closed) diagnostic(c, start_line, start_col, "LEXICO", "literal %s nao foi fechado", quote == '"' ? "string" : "caractere");
            else token_push(&c->tokens, make_token(quote == '"' ? T_STRING_LITERAL : T_CHAR_LITERAL, src + start, i - start, start_line, start_col));
            continue;
        }
        struct Op { const char *s; TokenKind k; } ops[] = {
            {"++",T_INC},{"--",T_DEC},{"+=",T_ADD_ASSIGN},{"-=",T_SUB_ASSIGN},{"*=",T_MUL_ASSIGN},{"/=",T_DIV_ASSIGN},
            {"==",T_EQ},{"!=",T_NE},{"<=",T_LE},{">=",T_GE},{"&&",T_AND},{"||",T_OR},{"->",T_ARROW}
        };
        int matched = 0;
        for (size_t oi = 0; oi < sizeof(ops)/sizeof(ops[0]); oi++) {
            size_t n = strlen(ops[oi].s);
            if (strncmp(src + i, ops[oi].s, n) == 0) {
                token_push(&c->tokens, make_token(ops[oi].k, src + i, n, line, col));
                i += n; col += (int)n; matched = 1; break;
            }
        }
        if (matched) continue;
        TokenKind kind = T_INVALID;
        switch (ch) {
            case '(': kind=T_LPAREN; break; case ')': kind=T_RPAREN; break;
            case '{': kind=T_LBRACE; break; case '}': kind=T_RBRACE; break;
            case '[': kind=T_LBRACKET; break; case ']': kind=T_RBRACKET; break;
            case ';': kind=T_SEMI; break; case ',': kind=T_COMMA; break;
            case '.': kind=T_DOT; break; case '?': kind=T_QUESTION; break; case ':': kind=T_COLON; break;
            case '+': kind=T_PLUS; break; case '-': kind=T_MINUS; break; case '*': kind=T_STAR; break;
            case '/': kind=T_SLASH; break; case '%': kind=T_PERCENT; break; case '=': kind=T_ASSIGN; break;
            case '<': kind=T_LT; break; case '>': kind=T_GT; break; case '!': kind=T_NOT; break;
            case '&': kind=T_BITAND; break; case '|': kind=T_BITOR; break; case '^': kind=T_XOR; break;
            case '~': kind=T_TILDE; break;
        }
        if (kind == T_INVALID || ch == '@' || ch == '$' || ch == '`' || ch == '\\') {
            diagnostic(c, line, col, "LEXICO", "caractere nao reconhecido '%c'", ch);
            i++; col++;
        } else {
            token_push(&c->tokens, make_token(kind, src + i, 1, line, col));
            i++; col++;
        }
    }
    Token eof = make_token(T_EOF, "<fim>", 5, line, col);
    token_push(&c->tokens, eof);
}

static Token *peek(Compiler *c) { return &c->tokens.data[c->pos < c->tokens.count ? c->pos : c->tokens.count - 1]; }
static Token *previous(Compiler *c) { return &c->tokens.data[c->pos ? c->pos - 1 : 0]; }
static int check(Compiler *c, TokenKind k) { return peek(c)->kind == k; }
static Token advance_token(Compiler *c) { if (!check(c, T_EOF)) c->pos++; return *previous(c); }
static int match(Compiler *c, TokenKind k) { if (!check(c,k)) return 0; advance_token(c); return 1; }
static int expect(Compiler *c, TokenKind k, const char *what) {
    if (match(c,k)) return 1;
    Token *t = peek(c);
    diagnostic(c, t->line, t->col, "SINTATICO", "esperado %s antes de '%s'", what, t->text);
    return 0;
}

static void scope_enter(Compiler *c) { c->scope_depth++; }

static void scope_leave(Compiler *c, int report_dead) {
    Symbol *s = c->symbols;
    while (s && s->depth == c->scope_depth) {
        Symbol *next = s->next;
        if (report_dead && !s->is_function && !s->is_const && s->initialized && s->reads == 0) {
            char before[MAX_TEXT], after[MAX_TEXT];
            snprintf(before, sizeof(before), "declaracao de %s", s->name);
            snprintf(after, sizeof(after), "remover declaracao e inicializacao de %s", s->name);
            optimization(c, s->line, "eliminacao de codigo morto", before, after);
        }
        free(s);
        s = next;
    }
    c->symbols = s;
    if (c->scope_depth > 0) c->scope_depth--;
}

static Symbol *find_symbol(Compiler *c, const char *name) {
    for (Symbol *s = c->symbols; s; s = s->next) if (strcmp(s->name,name)==0) return s;
    return NULL;
}

static Symbol *find_global(Compiler *c, const char *name) {
    for (Symbol *s = c->symbols; s; s = s->next) if (s->depth == 0 && strcmp(s->name,name)==0) return s;
    return NULL;
}

static Symbol *add_symbol(Compiler *c, const char *name, Type type, int line, int is_function) {
    Symbol *s = (Symbol *)calloc(1, sizeof(Symbol));
    if (!s) { fprintf(stderr,"Memoria insuficiente.\n"); exit(2); }
    snprintf(s->name,sizeof(s->name),"%s",name);
    s->type = type;
    s->line = line;
    s->depth = c->scope_depth;
    s->is_function = is_function;
    s->next = c->symbols;
    c->symbols = s;
    return s;
}

static int is_type_start(TokenKind k) {
    return k==K_INT || k==K_FLOAT || k==K_DOUBLE || k==K_CHAR || k==K_VOID || k==K_STRUCT || k==K_CONST;
}

static int is_statement_start(TokenKind k) {
    return is_type_start(k) || k==K_RETURN || k==K_IF || k==K_FOR || k==K_WHILE || k==K_DO ||
           k==K_BREAK || k==K_CONTINUE || k==T_LBRACE || k==T_SEMI || k==T_ID || k==T_INT_LITERAL ||
           k==T_FLOAT_LITERAL || k==T_CHAR_LITERAL || k==T_STRING_LITERAL || k==T_LPAREN || k==T_INC || k==T_DEC;
}

static void synchronize(Compiler *c) {
    while (!check(c,T_EOF)) {
        if (previous(c)->kind == T_SEMI) return;
        if (check(c,T_RBRACE) || is_statement_start(peek(c)->kind)) return;
        advance_token(c);
    }
}

static int accept_semicolon(Compiler *c) {
    if (match(c,T_SEMI)) return 1;
    Token *t = peek(c);
    diagnostic(c,t->line,t->col,"SINTATICO","esperado ';' ao final da instrucao");
    if (!is_statement_start(t->kind) && t->kind != T_RBRACE && t->kind != T_EOF) synchronize(c);
    match(c,T_SEMI);
    return 0;
}

static int expr_precedence(TokenKind k) {
    switch(k) {
        case T_ASSIGN: case T_ADD_ASSIGN: case T_SUB_ASSIGN: case T_MUL_ASSIGN: case T_DIV_ASSIGN: return 1;
        case T_OR: return 2; case T_AND: return 3;
        case T_EQ: case T_NE: return 4;
        case T_LT: case T_LE: case T_GT: case T_GE: return 5;
        case T_PLUS: case T_MINUS: return 6;
        case T_STAR: case T_SLASH: case T_PERCENT: return 7;
        default: return 0;
    }
}

static const char *operator_text(TokenKind k) {
    switch(k) {
        case T_PLUS:return "+"; case T_MINUS:return "-"; case T_STAR:return "*"; case T_SLASH:return "/"; case T_PERCENT:return "%";
        case T_EQ:return "=="; case T_NE:return "!="; case T_LT:return "<"; case T_LE:return "<="; case T_GT:return ">"; case T_GE:return ">=";
        case T_AND:return "&&"; case T_OR:return "||"; case T_ASSIGN:return "="; case T_ADD_ASSIGN:return "+=";
        case T_SUB_ASSIGN:return "-="; case T_MUL_ASSIGN:return "*="; case T_DIV_ASSIGN:return "/=";
        default:return "?";
    }
}

static int expr_is_numeric(Type t) { return t.kind==TY_INT || t.kind==TY_FLOAT || t.kind==TY_CHAR; }

static void remember_expression(Compiler *c, int line, const char *repr) {
    if (!repr[0] || c->seen_expr_count >= 512) return;
    for (int i=0;i<c->seen_expr_count;i++) {
        if (strcmp(c->seen_expr[i],repr)==0) {
            char after[MAX_TEXT];
            format_text(after,sizeof(after),"reutilizar o resultado de %s",repr);
            optimization(c,line,"eliminacao de subexpressao comum",repr,after);
            return;
        }
    }
    snprintf(c->seen_expr[c->seen_expr_count++],MAX_TEXT,"%s",repr);
}

static Expr parse_expression(Compiler *c, int min_prec);

static Expr bad_expr(void) {
    Expr e;
    memset(&e,0,sizeof(e));
    e.type=make_type(TY_ERROR);
    e.syntax_bad=1;
    snprintf(e.repr,sizeof(e.repr),"<expressao invalida>");
    return e;
}

static Expr parse_primary(Compiler *c) {
    Token t = *peek(c);
    if (match(c,T_INT_LITERAL) || match(c,T_FLOAT_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e));
        e.type=make_type(t.kind==T_FLOAT_LITERAL?TY_FLOAT:TY_INT);
        e.is_constant=1; e.value=strtod(t.text,NULL);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        return e;
    }
    if (match(c,T_CHAR_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e));
        e.type=make_type(TY_CHAR); e.is_constant=1;
        e.value=(unsigned char)(t.text[1] ? t.text[1] : 0);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        return e;
    }
    if (match(c,T_STRING_LITERAL)) {
        Expr e; memset(&e,0,sizeof(e)); e.type=make_type(TY_STRING);
        snprintf(e.repr,sizeof(e.repr),"%s",t.text); return e;
    }
    if (match(c,T_ID)) {
        Expr e; memset(&e,0,sizeof(e));
        snprintf(e.repr,sizeof(e.repr),"%s",t.text);
        if (match(c,T_LPAREN)) {
            Symbol *fn=find_symbol(c,t.text);
            if (!fn || !fn->is_function) {
                if (!fn) diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' nao declarada",t.text);
                else diagnostic(c,t.line,t.col,"SEMANTICO","'%s' nao e uma funcao",t.text);
            }
            Type arg_types[MAX_PARAMS]; int argc=0, malformed=0;
            if (!check(c,T_RPAREN)) {
                do {
                    if (argc<MAX_PARAMS) arg_types[argc]=parse_expression(c,2).type;
                    else parse_expression(c,2);
                    argc++;
                    if (check(c,T_RPAREN)) break;
                    if (!match(c,T_COMMA)) {
                        Token *p=peek(c);
                        diagnostic(c,p->line,p->col,"SINTATICO","esperado ',' ou ')' na lista de argumentos");
                        malformed=1;
                        while (!check(c,T_RPAREN) && !check(c,T_SEMI) && !check(c,T_EOF)) advance_token(c);
                        break;
                    }
                } while (!check(c,T_EOF));
            }
            expect(c,T_RPAREN,"')'");
            if (fn && fn->is_function && !malformed && !fn->variadic) {
                if (argc!=fn->param_count) {
                    diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' espera %d argumento(s), recebeu %d",t.text,fn->param_count,argc);
                } else {
                    int n=argc<MAX_PARAMS?argc:MAX_PARAMS;
                    for(int i=0;i<n;i++) if(!type_assignable(fn->params[i],arg_types[i]))
                        diagnostic(c,t.line,t.col,"SEMANTICO","argumento %d de '%s' tem tipo %s; esperado %s",i+1,t.text,type_name(arg_types[i]),type_name(fn->params[i]));
                }
                e.type=fn->type;
            } else if (fn && fn->is_function) e.type=fn->type;
            else e.type=make_type(TY_ERROR);
            format_text(e.repr,sizeof(e.repr),"%s()",t.text);
            return e;
        }
        Symbol *s=find_symbol(c,t.text);
        if (!s) {
            diagnostic(c,t.line,t.col,"SEMANTICO","identificador '%s' nao declarado",t.text);
            e.type=make_type(TY_ERROR);
        } else if (s->is_function) {
            diagnostic(c,t.line,t.col,"SEMANTICO","funcao '%s' usada sem chamada",t.text);
            e.type=s->type;
        } else {
            e.type=s->type; e.symbol=s; e.is_lvalue=1; s->reads++;
            if (s->is_const && s->has_const) {
                e.is_constant=1; e.value=s->const_value;
                int known=0;
                for(int i=0;i<c->propagated_count;i++) if(strcmp(c->propagated[i],s->name)==0) known=1;
                if(!known && c->propagated_count<MAX_PARAMS*8) {
                    char after[MAX_TEXT], before[MAX_TEXT];
                    snprintf(before,sizeof(before),"uso de %s",s->name);
                    if((int)s->const_value==s->const_value) snprintf(after,sizeof(after),"substituir %s por %d",s->name,(int)s->const_value);
                    else snprintf(after,sizeof(after),"substituir %s por %.6g",s->name,s->const_value);
                    optimization(c,t.line,"propagacao de constantes",before,after);
                    snprintf(c->propagated[c->propagated_count++],MAX_NAME,"%s",s->name);
                }
            }
        }
        return e;
    }
    if (match(c,T_LPAREN)) {
        Expr e=parse_expression(c,1);
        if (!expect(c,T_RPAREN,"')'")) e.syntax_bad=1;
        char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"(%s)",e.repr); copy_text(e.repr,sizeof(e.repr),tmp);
        return e;
    }
    if (match(c,K_SIZEOF)) {
        Expr inner;
        if (match(c,T_LPAREN)) { inner=parse_expression(c,1); expect(c,T_RPAREN,"')'"); }
        else inner=parse_primary(c);
        (void)inner;
        Expr e; memset(&e,0,sizeof(e)); e.type=make_type(TY_INT); e.is_constant=1; e.value=4; snprintf(e.repr,sizeof(e.repr),"sizeof"); return e;
    }
    diagnostic(c,t.line,t.col,"SINTATICO","expressao esperada antes de '%s'",t.text);
    if (!check(c,T_EOF) && !check(c,T_SEMI) && !check(c,T_COMMA) && !check(c,T_RPAREN) && !check(c,T_RBRACE)) advance_token(c);
    return bad_expr();
}

static Expr parse_unary(Compiler *c) {
    Token t=*peek(c);
    if (match(c,T_PLUS)||match(c,T_MINUS)||match(c,T_NOT)||match(c,T_BITAND)||match(c,T_STAR)||match(c,T_INC)||match(c,T_DEC)) {
        Expr e=parse_unary(c);
        if ((t.kind==T_INC||t.kind==T_DEC) && !e.is_lvalue)
            diagnostic(c,t.line,t.col,"SEMANTICO","operador '%s' exige uma variavel modificavel",t.text);
        if ((t.kind==T_PLUS||t.kind==T_MINUS||t.kind==T_NOT) && !expr_is_numeric(e.type) && e.type.kind!=TY_ERROR)
            diagnostic(c,t.line,t.col,"SEMANTICO","operador '%s' exige operando numerico",t.text);
        if (t.kind==T_MINUS && e.is_constant) e.value=-e.value;
        if (t.kind==T_NOT && e.is_constant) e.value=!e.value;
        e.type=(t.kind==T_NOT)?make_type(TY_INT):e.type;
        char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s%s",t.text,e.repr); copy_text(e.repr,sizeof(e.repr),tmp);
        return e;
    }
    Expr e=parse_primary(c);
    for (;;) {
        if (match(c,T_LBRACKET)) {
            Expr idx=parse_expression(c,1);
            if (!expr_is_numeric(idx.type) && idx.type.kind!=TY_ERROR)
                diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","indice de vetor deve ser numerico");
            expect(c,T_RBRACKET,"']'");
            if (!e.type.is_array && e.type.kind!=TY_ERROR)
                diagnostic(c,t.line,t.col,"SEMANTICO","indice aplicado a objeto que nao e vetor");
            e.type.is_array=0; e.is_lvalue=1;
            char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s[%s]",e.repr,idx.repr); copy_text(e.repr,sizeof(e.repr),tmp);
            continue;
        }
        if (match(c,T_INC)||match(c,T_DEC)) {
            Token op=*previous(c);
            if (!e.is_lvalue) diagnostic(c,op.line,op.col,"SEMANTICO","operador '%s' exige uma variavel modificavel",op.text);
            char tmp[MAX_TEXT]; format_text(tmp,sizeof(tmp),"%s%s",e.repr,op.text); copy_text(e.repr,sizeof(e.repr),tmp);
            continue;
        }
        break;
    }
    return e;
}

static Expr parse_expression(Compiler *c, int min_prec) {
    Expr left=parse_unary(c);
    while (expr_precedence(peek(c)->kind)>=min_prec) {
        Token op=advance_token(c);
        int prec=expr_precedence(op.kind);
        int right_assoc=prec==1;
        Expr right=parse_expression(c,prec+(right_assoc?0:1));
        if (prec==1) {
            if (!left.is_lvalue && left.type.kind!=TY_ERROR)
                diagnostic(c,op.line,op.col,"SEMANTICO","lado esquerdo de atribuicao nao e modificavel");
            if (left.symbol && left.symbol->is_const)
                diagnostic(c,op.line,op.col,"SEMANTICO","nao e permitido alterar a constante '%s'",left.symbol->name);
            if (!type_assignable(left.type,right.type) && left.type.kind!=TY_ERROR && right.type.kind!=TY_ERROR)
                diagnostic(c,op.line,op.col,"SEMANTICO","atribuicao incompativel: %s recebe %s",type_name(left.type),type_name(right.type));
            if (left.symbol) { left.symbol->initialized=1; left.symbol->has_const=0; }
            left.is_constant=0; left.type=left.type.kind==TY_ERROR?right.type:left.type;
        } else {
            char left_symbol[MAX_NAME]="", right_symbol[MAX_NAME]="";
            if(left.symbol) snprintf(left_symbol,sizeof(left_symbol),"%s",left.symbol->name);
            if(right.symbol) snprintf(right_symbol,sizeof(right_symbol),"%s",right.symbol->name);
            if ((op.kind==T_PLUS||op.kind==T_MINUS||op.kind==T_STAR||op.kind==T_SLASH||op.kind==T_PERCENT) &&
                ((!expr_is_numeric(left.type) && left.type.kind!=TY_ERROR)||(!expr_is_numeric(right.type)&&right.type.kind!=TY_ERROR)))
                diagnostic(c,op.line,op.col,"SEMANTICO","operador '%s' exige operandos numericos",op.text);
            if (op.kind==T_EQ||op.kind==T_NE||op.kind==T_LT||op.kind==T_LE||op.kind==T_GT||op.kind==T_GE||op.kind==T_AND||op.kind==T_OR)
                left.type=make_type(TY_INT);
            else if (left.type.kind==TY_FLOAT||right.type.kind==TY_FLOAT) left.type=make_type(TY_FLOAT);
            else if (left.type.kind==TY_ERROR) left.type=right.type;
            left.is_lvalue=0; left.symbol=NULL;
            if (left.is_constant && right.is_constant) {
                double a=left.value,b=right.value,r=0; int valid=1;
                switch(op.kind) {
                    case T_PLUS:r=a+b;break; case T_MINUS:r=a-b;break; case T_STAR:r=a*b;break;
                    case T_SLASH:if(b==0) valid=0; else r=a/b;break;
                    case T_PERCENT:if(b==0) valid=0; else r=(long)a%(long)b;break;
                    case T_EQ:r=a==b;break; case T_NE:r=a!=b;break; case T_LT:r=a<b;break;
                    case T_LE:r=a<=b;break; case T_GT:r=a>b;break; case T_GE:r=a>=b;break;
                    case T_AND:r=(a!=0)&&(b!=0);break; case T_OR:r=(a!=0)||(b!=0);break;
                    default:valid=0;break;
                }
                if(valid && (op.kind==T_PLUS||op.kind==T_MINUS||op.kind==T_STAR||op.kind==T_SLASH||op.kind==T_PERCENT)) {
                    char before[MAX_TEXT],after[MAX_TEXT];
                    format_text(before,sizeof(before),"%s %s %s",left.repr,operator_text(op.kind),right.repr);
                    if((int)r==r) snprintf(after,sizeof(after),"%d",(int)r); else snprintf(after,sizeof(after),"%.6g",r);
                    optimization(c,op.line,"constant folding",before,after);
                    left.value=r;
                } else left.value=r;
                left.is_constant=valid;
            } else left.is_constant=0;
            if(op.kind==T_STAR && c->loop_depth>0 && c->loop_induction[0]) {
                int induction=((strcmp(left_symbol,c->loop_induction)==0 && right.is_constant) ||
                              (strcmp(right_symbol,c->loop_induction)==0 && left.is_constant));
                if(induction) {
                    char before[MAX_TEXT],after[MAX_TEXT];
                    snprintf(before,sizeof(before),"multiplicacao de %s por constante dentro do laco",c->loop_induction);
                    snprintf(after,sizeof(after),"substituir por atualizacao incremental do produto");
                    optimization(c,op.line,"reducao de forca",before,after);
                }
            }
            char repr[MAX_TEXT];
            format_text(repr,sizeof(repr),"(%s %s %s)",left.repr,operator_text(op.kind),right.repr);
            if (!left.is_constant && !right.is_constant && (op.kind==T_PLUS||op.kind==T_MINUS||op.kind==T_STAR||op.kind==T_SLASH))
                remember_expression(c,op.line,repr);
            snprintf(left.repr,sizeof(left.repr),"%s",repr);
        }
    }
    return left;
}

static int parse_type(Compiler *c, Type *out, int *is_const) {
    *is_const=0;
    if (match(c,K_CONST)) *is_const=1;
    Token t=*peek(c);
    switch(t.kind) {
        case K_INT: advance_token(c); *out=make_type(TY_INT); return 1;
        case K_FLOAT: case K_DOUBLE: advance_token(c); *out=make_type(TY_FLOAT); return 1;
        case K_CHAR: advance_token(c); *out=make_type(TY_CHAR); return 1;
        case K_VOID: advance_token(c); *out=make_type(TY_VOID); return 1;
        case K_STRUCT:
            advance_token(c);
            if (!check(c,T_ID)) { diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","esperado nome apos struct"); *out=make_type(TY_ERROR); return 0; }
            *out=make_type(TY_STRUCT);
            Token tag_token=advance_token(c); copy_text(out->tag,sizeof(out->tag),tag_token.text);
            return 1;
        default: return 0;
    }
}

static void parse_declaration(Compiler *c, int consume_semi);
static void parse_statement(Compiler *c);
static void parse_compound(Compiler *c, int new_scope);

static void check_initializer(Compiler *c, Symbol *s, Expr e, int line) {
    if (!s || e.syntax_bad || e.type.kind==TY_ERROR) return;
    s->initialized=1;
    Type target=s->type;
    target.is_array=0;
    if (!type_assignable(target,e.type) && e.type.kind!=TY_ERROR)
        diagnostic(c,line,1,"SEMANTICO","variavel '%s' do tipo %s recebe valor do tipo %s",s->name,type_name(s->type),type_name(e.type));
    if (s->is_const && e.is_constant) { s->has_const=1; s->const_value=e.value; }
}

static void parse_initializer(Compiler *c, Symbol *s, int line) {
    if (match(c,T_LBRACE)) {
        while (!check(c,T_RBRACE) && !check(c,T_EOF)) {
            if (check(c,T_COMMA)) {
                Token *p=peek(c);
                diagnostic(c,p->line,p->col,"SINTATICO","elemento ausente na inicializacao do vetor");
                advance_token(c);
                continue;
            }
            Expr e=parse_expression(c,2);
            if (s) {
                Type element_type=s->type;
                element_type.is_array=0;
                if (!type_assignable(element_type,e.type) && e.type.kind!=TY_ERROR)
                    diagnostic(c,line,1,"SEMANTICO","elemento de inicializacao incompativel com '%s'",s->name);
            }
            if (!match(c,T_COMMA)) break;
        }
        expect(c,T_RBRACE,"'}'");
        if(s) s->initialized=1;
    } else {
        Expr e=parse_expression(c,2);
        check_initializer(c,s,e,line);
    }
}

static void parse_declaration(Compiler *c, int consume_semi) {
    int is_const=0; Type base;
    if(!parse_type(c,&base,&is_const)) {
        Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","tipo de dado esperado");
        if(!check(c,T_EOF)) advance_token(c);
        return;
    }
    if(base.kind==TY_VOID && !check(c,T_STAR)) {
        Token *p=peek(c);
        if(p->kind!=T_ID) diagnostic(c,p->line,p->col,"SEMANTICO","variavel nao pode ter tipo void");
    }
    for(;;) {
        Token name=*peek(c);
        if(!match(c,T_ID)) {
            diagnostic(c,name.line,name.col,"SINTATICO","identificador esperado na declaracao");
            while(!check(c,T_SEMI)&&!check(c,T_RPAREN)&&!check(c,T_EOF)&&!check(c,T_RBRACE)) advance_token(c);
            break;
        }
        Type var_type=base;
        if(match(c,T_LBRACKET)) {
            var_type.is_array=1;
            if(!check(c,T_RBRACKET)) parse_expression(c,1);
            expect(c,T_RBRACKET,"']'");
        }
        Symbol *old=NULL;
        for(Symbol *q=c->symbols;q && q->depth==c->scope_depth;q=q->next) if(strcmp(q->name,name.text)==0) old=q;
        if(old) diagnostic(c,name.line,name.col,"SEMANTICO","identificador '%s' ja declarado neste escopo",name.text);
        Symbol *s=old?old:add_symbol(c,name.text,var_type,name.line,0);
        s->is_const=is_const;
        if(match(c,T_ASSIGN)) parse_initializer(c,s,name.line);
        if(!match(c,T_COMMA)) break;
    }
    if(consume_semi) accept_semicolon(c);
}

static void parse_parameter_list(Compiler *c, Type *types, char names[][MAX_NAME], int *count, int *bad) {
    *count=0; *bad=0;
    if(check(c,K_VOID) && c->pos+1<c->tokens.count && c->tokens.data[c->pos+1].kind==T_RPAREN) { advance_token(c); return; }
    if(check(c,T_RPAREN)) return;
    for(;;) {
        int is_const=0; Type t;
        if(!parse_type(c,&t,&is_const)) {
            Token *p=peek(c);
            diagnostic(c,p->line,p->col,"SINTATICO","tipo esperado na lista de parametros");
            *bad=1;
            if(match(c,T_COMMA)) continue;
            while(!check(c,T_RPAREN)&&!check(c,T_EOF)&&!check(c,T_COMMA)) advance_token(c);
            if(match(c,T_COMMA)) continue;
            break;
        }
        Token n=*peek(c);
        if(!match(c,T_ID)) {
            diagnostic(c,n.line,n.col,"SINTATICO","nome do parametro esperado"); *bad=1;
        } else if(*count<MAX_PARAMS) {
            types[*count]=t; copy_text(names[*count],MAX_NAME,n.text); (*count)++;
        }
        if(!match(c,T_COMMA)) break;
        if(check(c,T_COMMA)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","parametro ausente entre virgulas"); *bad=1; advance_token(c); }
    }
}

static void parse_function(Compiler *c, Type ret, Token name, int is_const) {
    (void)is_const;
    Type params[MAX_PARAMS]; char names[MAX_PARAMS][MAX_NAME]; int count=0,bad=0;
    parse_parameter_list(c,params,names,&count,&bad);
    expect(c,T_RPAREN,"')'");
    Symbol *fn=find_global(c,name.text);
    if(fn && !fn->is_function) {
        diagnostic(c,name.line,name.col,"SEMANTICO","'%s' ja foi declarado como variavel",name.text);
        fn=NULL;
    }
    if(!fn) { fn=add_symbol(c,name.text,ret,name.line,1); fn->param_count=count; }
    else if(fn->param_count!=count || !type_equal(fn->type,ret)) {
        diagnostic(c,name.line,name.col,"SEMANTICO","declaracao conflitante da funcao '%s'",name.text);
    }
    if(fn) {
        if(!fn->param_count && count) { fn->param_count=count; for(int i=0;i<count;i++) fn->params[i]=params[i]; }
        else for(int i=0;i<count && i<MAX_PARAMS;i++) fn->params[i]=params[i];
    }
    if(match(c,T_SEMI)) return;
    if(!check(c,T_LBRACE)) {
        Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","esperado ';' ou corpo da funcao");
        while(!check(c,T_LBRACE)&&!check(c,T_SEMI)&&!check(c,T_EOF)) advance_token(c);
        if(match(c,T_SEMI)) return;
    }
    if(fn && fn->defined) diagnostic(c,name.line,name.col,"SEMANTICO","funcao '%s' definida mais de uma vez",name.text);
    if(fn) fn->defined=1;
    Symbol *old_func=c->current_function;
    Type old_ret=c->return_type;
    c->current_function=fn; c->return_type=ret;
    c->seen_expr_count=0; c->propagated_count=0; c->loop_induction[0]='\0'; c->loop_depth=0;
    scope_enter(c);
    for(int i=0;i<count;i++) {
        if(names[i][0]) {
            Symbol *p=find_symbol(c,names[i]);
            if(p && p->depth==c->scope_depth) diagnostic(c,name.line,name.col,"SEMANTICO","parametro '%s' duplicado",names[i]);
            else add_symbol(c,names[i],params[i],name.line,0);
        }
    }
    parse_compound(c,1);
    scope_leave(c,0);
    c->current_function=old_func; c->return_type=old_ret;
}

static void parse_struct_definition(Compiler *c) {
    Token kw=advance_token(c);
    Token tag=*peek(c);
    if(!match(c,T_ID)) { diagnostic(c,tag.line,tag.col,"SINTATICO","nome da estrutura esperado"); return; }
    if(match(c,T_LBRACE)) {
        while(!check(c,T_RBRACE)&&!check(c,T_EOF)) {
            int is_const=0; Type field;
            if(!parse_type(c,&field,&is_const)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","tipo esperado no campo da estrutura"); advance_token(c); continue; }
            if(!match(c,T_ID)) { Token *p=peek(c); diagnostic(c,p->line,p->col,"SINTATICO","nome do campo esperado"); synchronize(c); continue; }
            while(match(c,T_COMMA)) { if(!match(c,T_ID)) { diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","nome do campo esperado"); break; } }
            accept_semicolon(c);
        }
        expect(c,T_RBRACE,"'}'");
        accept_semicolon(c);
    } else {
        c->pos--;
        (void)kw;
    }
}

static void parse_external(Compiler *c) {
    if(check(c,K_STRUCT) && c->pos+2<c->tokens.count && c->tokens.data[c->pos+2].kind==T_LBRACE) {
        parse_struct_definition(c); return;
    }
    size_t declaration_start=c->pos;
    int is_const=0; Type base;
    if(!parse_type(c,&base,&is_const)) {
        Token t=advance_token(c);
        diagnostic(c,t.line,t.col,"SINTATICO","declaracao externa inesperada: '%s'",t.text);
        synchronize(c); return;
    }
    Token name=*peek(c);
    if(!match(c,T_ID)) {
        diagnostic(c,name.line,name.col,"SINTATICO","identificador esperado depois do tipo");
        synchronize(c); return;
    }
    if(match(c,T_LPAREN)) { parse_function(c,base,name,is_const); return; }
    c->pos=declaration_start;
    parse_declaration(c,1);
}

static void parse_compound(Compiler *c, int new_scope) {
    if(!expect(c,T_LBRACE,"'{'")) return;
    if(new_scope) scope_enter(c);
    while(!check(c,T_RBRACE)&&!check(c,T_EOF)) {
        size_t before=c->pos;
        parse_statement(c);
        if(c->pos==before && !check(c,T_EOF)) advance_token(c);
    }
    expect(c,T_RBRACE,"'}'");
    if(new_scope) scope_leave(c,1);
}

static void parse_if(Compiler *c) {
    advance_token(c);
    expect(c,T_LPAREN,"'('");
    Expr cond=parse_expression(c,1);
    if(cond.type.kind!=TY_ERROR && !expr_is_numeric(cond.type)) diagnostic(c,previous(c)->line,previous(c)->col,"SEMANTICO","condicao do if deve ser escalar");
    expect(c,T_RPAREN,"')'");
    parse_statement(c);
    if(match(c,K_ELSE)) parse_statement(c);
}

static void parse_while(Compiler *c) {
    advance_token(c); expect(c,T_LPAREN,"'('"); parse_expression(c,1); expect(c,T_RPAREN,"')'");
    c->loop_depth++; parse_statement(c); c->loop_depth--;
}

static void parse_for(Compiler *c) {
    advance_token(c); expect(c,T_LPAREN,"'('");
    scope_enter(c);
    char previous_induction[MAX_NAME];
    snprintf(previous_induction,sizeof(previous_induction),"%s",c->loop_induction);
    if(is_type_start(peek(c)->kind)) {
        parse_declaration(c,0);
        if(c->symbols && c->symbols->depth==c->scope_depth && !c->symbols->is_function)
            snprintf(c->loop_induction,sizeof(c->loop_induction),"%s",c->symbols->name);
    } else if(!check(c,T_SEMI)) parse_expression(c,1);
    expect(c,T_SEMI,"';' no cabecalho do for");
    if(!check(c,T_SEMI)) parse_expression(c,1);
    expect(c,T_SEMI,"segundo ';' no cabecalho do for");
    if(!check(c,T_RPAREN)) parse_expression(c,1);
    expect(c,T_RPAREN,"')'");
    c->loop_depth++;
    parse_statement(c);
    c->loop_depth--;
    snprintf(c->loop_induction,sizeof(c->loop_induction),"%s",previous_induction);
    scope_leave(c,1);
}

static void parse_statement(Compiler *c) {
    if(check(c,T_LBRACE)) { parse_compound(c,1); return; }
    if(is_type_start(peek(c)->kind)) { parse_declaration(c,1); return; }
    if(check(c,K_IF)) { parse_if(c); return; }
    if(check(c,K_WHILE)) { parse_while(c); return; }
    if(check(c,K_FOR)) { parse_for(c); return; }
    if(match(c,K_DO)) { c->loop_depth++; parse_statement(c); c->loop_depth--; if(match(c,K_WHILE)) { expect(c,T_LPAREN,"'('"); parse_expression(c,1); expect(c,T_RPAREN,"')'"); accept_semicolon(c); } else diagnostic(c,peek(c)->line,peek(c)->col,"SINTATICO","esperado while depois do bloco do"); return; }
    if(match(c,K_RETURN)) {
        Token start=*previous(c);
        if(!check(c,T_SEMI)) {
            Expr e=parse_expression(c,1);
            if(c->current_function && c->return_type.kind!=TY_VOID && !type_assignable(c->return_type,e.type) && e.type.kind!=TY_ERROR)
                diagnostic(c,start.line,start.col,"SEMANTICO","retorno do tipo %s incompativel com %s",type_name(e.type),type_name(c->return_type));
        } else if(c->current_function && c->return_type.kind!=TY_VOID) diagnostic(c,start.line,start.col,"SEMANTICO","funcao deve retornar um valor do tipo %s",type_name(c->return_type));
        accept_semicolon(c); return;
    }
    if(match(c,K_BREAK)||match(c,K_CONTINUE)) {
        Token t=*previous(c);
        if(!c->loop_depth) diagnostic(c,t.line,t.col,"SEMANTICO","'%s' usado fora de um laco",t.text);
        accept_semicolon(c); return;
    }
    if(match(c,T_SEMI)) return;
    if(check(c,K_ELSE)) {
        Token t=advance_token(c); diagnostic(c,t.line,t.col,"SINTATICO","'else' sem um 'if' correspondente");
        if(check(c,T_LBRACE)) parse_statement(c);
        return;
    }
    if(check(c,K_CASE)||check(c,K_DEFAULT)||check(c,K_SWITCH)) {
        Token t=advance_token(c); diagnostic(c,t.line,t.col,"SINTATICO","construcao '%s' nao suportada neste subconjunto",t.text); synchronize(c); return;
    }
    Expr e=parse_expression(c,1);
    (void)e;
    accept_semicolon(c);
}

static void add_builtin(Compiler *c, const char *name, Type ret, int variadic) {
    Symbol *s=add_symbol(c,name,ret,0,1); s->variadic=variadic; s->defined=1;
}

static void parse_program(Compiler *c) {
    add_builtin(c,"printf",make_type(TY_INT),1);
    add_builtin(c,"scanf",make_type(TY_INT),1);
    add_builtin(c,"puts",make_type(TY_INT),1);
    add_builtin(c,"putchar",make_type(TY_INT),1);
    add_builtin(c,"getchar",make_type(TY_INT),1);
    add_builtin(c,"strlen",make_type(TY_INT),0);
    while(!check(c,T_EOF)) {
        size_t before=c->pos;
        parse_external(c);
        if(c->pos==before && !check(c,T_EOF)) advance_token(c);
    }
}

static char *read_file(const char *path) {
    FILE *f=fopen(path,"rb");
    if(!f) return NULL;
    if(fseek(f,0,SEEK_END)!=0) { fclose(f); return NULL; }
    long n=ftell(f);
    if(n<0) { fclose(f); return NULL; }
    rewind(f);
    char *buf=(char *)malloc((size_t)n+1);
    if(!buf) { fclose(f); return NULL; }
    size_t got=fread(buf,1,(size_t)n,f); fclose(f); buf[got]='\0'; return buf;
}

static void print_source(const char *source) {
    int line=1; const char *p=source;
    while(*p) {
        const char *end=strchr(p,'\n');
        size_t n=end?(size_t)(end-p):strlen(p);
        printf("%4d | %.*s\n",line++,(int)n,p);
        if(!end) break;
        p=end+1;
    }
}

static void print_report(Compiler *c, const char *filename, const char *source) {
    printf("\n============================================================\nANALISE DO ARQUIVO: %s\n============================================================\n",filename);
    print_source(source);
    const char *phases[]={"LEXICO","SINTATICO","SEMANTICO"};
    const char *titles[]={"ANALISE LEXICA","ANALISE SINTATICA","ANALISE SEMANTICA"};
    for(int p=0;p<3;p++) {
        printf("\n[%s]\n",titles[p]); int found=0;
        for(int i=0;i<c->error_count;i++) if(strcmp(c->errors[i].phase,phases[p])==0) {
            printf("Linha %d, coluna %d: %s\n",c->errors[i].line,c->errors[i].col,c->errors[i].message); found++;
        }
        if(!found) printf("Nenhum problema encontrado.\n");
    }
    printf("\n[OTIMIZACAO]\n");
    if(!c->opt_count) printf("Nenhuma oportunidade identificada.\n");
    for(int i=0;i<c->opt_count;i++) {
        Optimization *o=&c->opts[i];
        printf("Linha %d | %s\n  Antes: %s\n  Sugestao: %s\n",o->line,o->kind,o->before,o->after);
    }
    printf("\n============================================================\nRESUMO\n============================================================\n");
    printf("Erros lexicos:       %d\n",c->lex_count);
    printf("Erros sintaticos:    %d\n",c->syntax_count);
    printf("Erros semanticos:    %d\n",c->semantic_count);
    printf("Total de erros:      %d\n",c->lex_count+c->syntax_count+c->semantic_count);
    printf("Otimizacoes sugeridas: %d\n",c->opt_count);
    if(c->lex_count+c->syntax_count+c->semantic_count==0) printf("Estado: analise concluida sem erros detectados.\n");
    else printf("Estado: analise concluida com diagnosticos.\n");
}

static int run_file(const char *filename) {
    char *source=read_file(filename);
    if(!source) { fprintf(stderr,"Nao foi possivel abrir '%s': %s\n",filename,strerror(errno)); return 2; }
    Compiler c; memset(&c,0,sizeof(c));
    lex_source(&c,source);
    parse_program(&c);
    print_report(&c,filename,source);
    for(Symbol *s=c.symbols;s;) { Symbol *n=s->next; free(s); s=n; }
    free(c.tokens.data); free(source);
    return c.lex_count+c.syntax_count+c.semantic_count ? 1 : 0;
}

int main(int argc, char **argv) {
    char filename[512];
    if(argc>1) return run_file(argv[1]);
    printf("Digite o nome do arquivo (.c ou .txt): ");
    if(scanf("%511s",filename)!=1) return 2;
    return run_file(filename);
}
