#include "checkers.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINE_LENGTH 256

typedef struct Symbol {
    char name[64];
    char type[16];
    int line_declared;
    int used_before_declared;
    struct Symbol *next;
} Symbol;

typedef struct SymbolTable {
    Symbol *head;
    int count;
} SymbolTable;

typedef struct Error {
    int line;
    char phase[32];
    char description[256];
    struct Error *next;
} Error;

typedef struct Optimization {
    int line;
    char suggestion[128];
    char original[256];
    char optimized[256];
    struct Optimization *next;
} Optimization;

typedef struct LastAssign {
    char name[64];
    int line;
    struct LastAssign *next;
} LastAssign;

static SymbolTable symbol_table;
static Error *error_head = NULL;
static Optimization *optim_head = NULL;
static LastAssign *last_assign_head = NULL;

static int error_count_lexical = 0;
static int error_count_syntax = 0;
static int error_count_semantic = 0;
static int optimization_count = 0;

static SymbolTable create_symbol_table(void) {
    SymbolTable table;
    table.head = NULL;
    table.count = 0;
    return table;
}

static Symbol* find_symbol(const char *name) {
    Symbol *curr = symbol_table.head;
    while (curr) {
        if (strcmp(curr->name, name) == 0)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

static void insert_symbol(const char *name, const char *type, int line) {
    Symbol *existing = find_symbol(name);
    if (existing) return;
    Symbol *new_sym = (Symbol *)malloc(sizeof(Symbol));
    strncpy(new_sym->name, name, 63);
    new_sym->name[63] = '\0';
    strncpy(new_sym->type, type, 15);
    new_sym->type[15] = '\0';
    new_sym->line_declared = line;
    new_sym->used_before_declared = 0;
    new_sym->next = symbol_table.head;
    symbol_table.head = new_sym;
    symbol_table.count++;
}

static void mark_symbol_used(const char *name, int line) {
    Symbol *sym = find_symbol(name);
    if (!sym) return;
    if (line < sym->line_declared) {
        sym->used_before_declared = 1;
    }
}

static void add_error(int line, const char *phase, const char *description) {
    Error *err = (Error *)malloc(sizeof(Error));
    err->line = line;
    strncpy(err->phase, phase, 31);
    err->phase[31] = '\0';
    strncpy(err->description, description, 255);
    err->description[255] = '\0';
    err->next = error_head;
    error_head = err;

    if (strcmp(phase, "LEXICA") == 0) error_count_lexical++;
    else if (strcmp(phase, "SINTATICA") == 0) error_count_syntax++;
    else if (strcmp(phase, "SEMANTICA") == 0) error_count_semantic++;
}

static void add_optimization(int line, const char *suggestion, const char *original, const char *optimized) {
    Optimization *opt = (Optimization *)malloc(sizeof(Optimization));
    opt->line = line;
    strncpy(opt->suggestion, suggestion, 127);
    opt->suggestion[127] = '\0';
    strncpy(opt->original, original, 255);
    opt->original[255] = '\0';
    strncpy(opt->optimized, optimized, 255);
    opt->optimized[255] = '\0';
    opt->next = optim_head;
    optim_head = opt;
    optimization_count++;
}

static void track_last_assign(const char *name, int line) {
    LastAssign *la = (LastAssign *)malloc(sizeof(LastAssign));
    strncpy(la->name, name, 63);
    la->name[63] = '\0';
    la->line = line;
    la->next = last_assign_head;
    last_assign_head = la;
}

static int check_and_clear_dead_code(const char *name, int current_line) {
    LastAssign *curr = last_assign_head;
    LastAssign *prev = NULL;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            char full_original[256], full_optimized[256];
            snprintf(full_original, sizeof(full_original), "%s = <valor anterior>", name);
            snprintf(full_optimized, sizeof(full_optimized), "remover atribuicao anterior de %s", name);
            add_optimization(0, "eliminacao de codigo morto", full_original, full_optimized);
            if (prev) {
                prev->next = curr->next;
            } else {
                last_assign_head = curr->next;
            }
            free(curr);
            return 1;
        }
        prev = curr;
        curr = curr->next;
    }
    return 0;
}

static void trim_whitespace(char *str) {
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    if (*start == 0) {
        str[0] = '\0';
        return;
    }
    char *end = start + strlen(start) - 1;
    while (end > start && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    if (start != str) {
        memmove(str, start, end - start + 2);
    }
}

static int is_comment_line(const char *line) {
    char *trimmed = (char *)line;
    while (isspace((unsigned char)*trimmed)) trimmed++;
    return (*trimmed == '/' && *(trimmed + 1) == '/');
}

static void strip_comments(char *line) {
    char *comment_start = strstr(line, "//");
    if (comment_start) {
        *comment_start = '\0';
    }
}

static void lexical_analysis(const char *line, int line_num) {
    int i = 0;
    while (line[i] != '\0') {
        char c = (unsigned char)line[i];

        if (isspace(c)) {
            i++;
            continue;
        }

        if (c == '/' && line[i + 1] == '/') {
            break;
        }

        if (isalnum(c) || c == '_' || c == '.' || c == '\'' || c == '\"') {
            char token[64] = {0};
            int j = 0;
            while (
                (isalnum((unsigned char)c) || c == '_' || c == '.' || c == '\'' || c == '\"') &&
                j < 63) {
                token[j++] = c;
                c = (unsigned char)line[++i];
            }
            token[j] = '\0';
        } else if (c == '=' && line[i + 1] == '=') {
            i += 2;
        } else if (c == '>' && line[i + 1] == '=') {
            i += 2;
        } else if (c == '<' && line[i + 1] == '=') {
            i += 2;
        } else if (c == '!' && line[i + 1] == '=') {
            i += 2;
        } else if (c == '=' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}' ||
                   c == '+' || c == '-' || c == '*' || c == '/' ||
                   c == '>' || c == '<' || c == '!' || c == ':' || c == ',' || c == '.' ||
                   c == '?' || c == '[' || c == ']' || c == '&' || c == '|' || c == '%' ||
                   c == '^' || c == '~' || c == '#') {
            i++;
        } else if (c == '@' || c == '$' || c == '\\' || c == '`' || c == '"' && line[i-1] != '\\') {
            char error_desc[8];
            error_desc[0] = line[i];
            error_desc[1] = '\0';
            add_error(line_num, "LEXICA", error_desc);
            i++;
        } else {
            char error_desc[8];
            error_desc[0] = line[i];
            error_desc[1] = '\0';
            add_error(line_num, "LEXICA", error_desc);
            i++;
        }
    }
}

static int check_line_syntax(const char *line, int line_num) {
    char cleaned[MAX_LINE_LENGTH];
    strncpy(cleaned, line, MAX_LINE_LENGTH - 1);
    cleaned[MAX_LINE_LENGTH - 1] = '\0';
    trim_whitespace(cleaned);

    if (cleaned[0] == '\0' || is_comment_line(cleaned))
        return 1;

    char line_no_comments[MAX_LINE_LENGTH];
    strncpy(line_no_comments, cleaned, MAX_LINE_LENGTH - 1);
    line_no_comments[MAX_LINE_LENGTH - 1] = '\0';
    strip_comments(line_no_comments);
    trim_whitespace(line_no_comments);

    if (line_no_comments[0] == '\0')
        return 1;

    char *ptr = line_no_comments;
    while (isspace((unsigned char)*ptr)) ptr++;

    int has_type = 0;
    if (strncmp(ptr, "int", 3) == 0 && (ptr[3] == ' ' || ptr[3] == '\t' || ptr[3] == '\0')) {
        ptr += 3;
        has_type = 1;
    } else if (strncmp(ptr, "float", 5) == 0 && (ptr[5] == ' ' || ptr[5] == '\t' || ptr[5] == '\0')) {
        ptr += 5;
        has_type = 1;
    } else if (strncmp(ptr, "char", 4) == 0 && (ptr[4] == ' ' || ptr[4] == '\t' || ptr[4] == '\0')) {
        ptr += 4;
        has_type = 1;
    }

    if (!has_type) {
        char *eq = strchr(line_no_comments, '=');
        if (eq) {
            char *assign_start = line_no_comments;
            while (isspace((unsigned char)*assign_start)) assign_start++;
            char target[64] = {0};
            int t = 0;
            while (*assign_start && *assign_start != '=' && *assign_start != ';' && t < 63) {
                if (!isspace((unsigned char)*assign_start)) {
                    target[t++] = *assign_start;
                }
                assign_start++;
            }
            target[t] = '\0';
            trim_whitespace(target);
            if (target[0] != '\0') {
                while (*ptr && *ptr != '=' && *ptr != ';') ptr++;
                if (*ptr == '=') {
                    ptr++;
                    while (isspace((unsigned char)*ptr)) ptr++;
                    char *semi = strchr(ptr, ';');
                    if (semi) return 1;
                    add_error(line_num, "SINTATICA", "Esperado ';' ao final da declaracao");
                    return 0;
                }
            }
        }
        while (*ptr && !isspace((unsigned char)*ptr)) ptr++;
        while (isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '\0') return 1;
        return 0;
    }

    while (isspace((unsigned char)*ptr)) ptr++;

    char ident[64] = {0};
    int k = 0;
    while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '=' && *ptr != ';' && k < 63) {
        ident[k++] = *ptr++;
    }
    ident[k] = '\0';

    if (ident[0] == '\0') {
        add_error(line_num, "SINTATICA", "Esperado identificador apos tipo de dado");
        return 0;
    }

    while (isspace((unsigned char)*ptr)) ptr++;

    if (*ptr != '=') {
        add_error(line_num, "SINTATICA", "Esperado '=' apos identificador");
        return 0;
    }

    ptr++;
    while (isspace((unsigned char)*ptr)) ptr++;

    char value[MAX_LINE_LENGTH] = {0};
    int v = 0;
    while (*ptr && *ptr != ';' && *ptr != '\0' && v < MAX_LINE_LENGTH - 1) {
        value[v++] = *ptr++;
    }
    value[v] = '\0';
    trim_whitespace(value);

    char *ptr_check = ptr;
    while (*ptr_check == ' ' || *ptr_check == '\t' || *ptr_check == '\n' || *ptr_check == '\r') ptr_check++;
    if (*ptr_check != ';') {
        add_error(line_num, "SINTATICA", "Esperado ';' ao final da declaracao");
        return 0;
    }

    return 1;
}

static void check_line_semantics(const char *line, int line_num) {
    char cleaned[MAX_LINE_LENGTH];
    strncpy(cleaned, line, MAX_LINE_LENGTH - 1);
    cleaned[MAX_LINE_LENGTH - 1] = '\0';
    trim_whitespace(cleaned);

    if (cleaned[0] == '\0' || is_comment_line(cleaned))
        return;

    char line_no_comments[MAX_LINE_LENGTH];
    strncpy(line_no_comments, cleaned, MAX_LINE_LENGTH - 1);
    line_no_comments[MAX_LINE_LENGTH - 1] = '\0';
    strip_comments(line_no_comments);
    trim_whitespace(line_no_comments);

    if (line_no_comments[0] == '\0')
        return;

    char *ptr = line_no_comments;
    while (isspace((unsigned char)*ptr)) ptr++;

    char var_type[16] = {0};
    if (strncmp(ptr, "int", 3) == 0 && (ptr[3] == ' ' || ptr[3] == '\t' || ptr[3] == '\0')) {
        strncpy(var_type, "int", 15);
        var_type[15] = '\0';
        ptr += 3;
    } else if (strncmp(ptr, "float", 5) == 0 && (ptr[5] == ' ' || ptr[5] == '\t' || ptr[5] == '\0')) {
        strncpy(var_type, "float", 15);
        var_type[15] = '\0';
        ptr += 5;
    } else if (strncmp(ptr, "char", 4) == 0 && (ptr[4] == ' ' || ptr[4] == '\t' || ptr[4] == '\0')) {
        strncpy(var_type, "char", 15);
        var_type[15] = '\0';
        ptr += 4;
    } else {
        // Check for assignment without declaration (e.g., "quantidade = 8;")
        char *eq = strchr(line_no_comments, '=');
        if (eq) {
            char *assign_start = line_no_comments;
            while (isspace((unsigned char)*assign_start)) assign_start++;
            char target[64] = {0};
            int t = 0;
            while (*assign_start && *assign_start != '=' && *assign_start != ';' && t < 63) {
                if (!isspace((unsigned char)*assign_start)) {
                    target[t++] = *assign_start;
                }
                assign_start++;
            }
            target[t] = '\0';
            trim_whitespace(target);
            if (target[0] != '\0') {
                Symbol *sym = find_symbol(target);
                if (!sym) {
                    char desc[256];
                    snprintf(desc, sizeof(desc), "Variavel '%s' utilizada antes de ser declarada", target);
                    add_error(line_num, "SEMANTICA", desc);
                } else {
                    mark_symbol_used(target, line_num);
                    check_and_clear_dead_code(target, line_num);
                    track_last_assign(target, line_num);
                }
            }
        }
        return;
    }

    while (isspace((unsigned char)*ptr)) ptr++;

    char ident[64] = {0};
    int k = 0;
    while (*ptr && !isspace((unsigned char)*ptr) && *ptr != '=' && *ptr != ';' && k < 63) {
        ident[k++] = *ptr++;
    }
    ident[k] = '\0';

    if (ident[0] == '\0') return;

    Symbol *existing = find_symbol(ident);
    if (existing) {
        char desc[256];
        snprintf(desc, sizeof(desc), "Identificador '%s' ja declarado anteriormente", ident);
        add_error(line_num, "SEMANTICA", desc);
        return;
    }

    insert_symbol(ident, var_type, line_num);
    track_last_assign(ident, line_num);

    while (isspace((unsigned char)*ptr)) ptr++;

    if (*ptr != '=') return;
    ptr++;
    while (isspace((unsigned char)*ptr)) ptr++;

    char value[MAX_LINE_LENGTH] = {0};
    int v = 0;
    while (*ptr && *ptr != ';' && *ptr != '\0' && v < MAX_LINE_LENGTH - 1) {
        value[v++] = *ptr++;
    }
    value[v] = '\0';
    trim_whitespace(value);

    if (value[0] == '\0') return;

    char *val_ptr = value;
    while (isspace((unsigned char)*val_ptr)) val_ptr++;

    if (*val_ptr == '"') {
        if (strcmp(var_type, "int") == 0 || strcmp(var_type, "float") == 0 || strcmp(var_type, "char") == 0) {
            char desc[256];
            snprintf(desc, sizeof(desc), "Variavel '%s' declarada como %s recebeu STRING", ident, var_type);
            add_error(line_num, "SEMANTICA", desc);
        }
    } else if (*val_ptr == '\'') {
        if (strcmp(var_type, "int") == 0 || strcmp(var_type, "float") == 0) {
            char desc[256];
            snprintf(desc, sizeof(desc), "Variavel '%s' declarada como %s recebeu CHAR", ident, var_type);
            add_error(line_num, "SEMANTICA", desc);
        }
    } else if (isdigit((unsigned char)*val_ptr) || *val_ptr == '-' || *val_ptr == '+') {
        int has_dot = 0;
        char *cp = val_ptr;
        while (*cp) {
            if (*cp == '.') has_dot = 1;
            cp++;
        }
        if (has_dot && strcmp(var_type, "int") == 0) {
            char desc[256];
            snprintf(desc, sizeof(desc), "Variavel '%s' declarada como INT recebeu FLOAT", ident);
            add_error(line_num, "SEMANTICA", desc);
        }
    } else if (isalpha((unsigned char)*val_ptr) || *val_ptr == '_') {
        char ref_ident[64] = {0};
        k = 0;
        while (*val_ptr && (isalnum((unsigned char)*val_ptr) || *val_ptr == '_') && k < 63) {
            ref_ident[k++] = *val_ptr++;
        }
        ref_ident[k] = '\0';

        Symbol *ref_sym = find_symbol(ref_ident);
        if (!ref_sym) {
            char desc[256];
            snprintf(desc, sizeof(desc), "Variavel '%s' utilizada antes de ser declarada", ref_ident);
            add_error(line_num, "SEMANTICA", desc);
        } else {
            mark_symbol_used(ref_ident, line_num);
            if (strcmp(var_type, ref_sym->type) != 0) {
                char desc[256];
                snprintf(desc, sizeof(desc), "Variavel '%s' (%s) nao e compatível com '%s' (%s)",
                         ident, var_type, ref_ident, ref_sym->type);
                add_error(line_num, "SEMANTICA", desc);
            }
        }
    }
}

static void extract_identifier_and_expr(const char *line_no_comments, char *ident_val, char *expr, int max_size) {
    char *ptr = (char *)line_no_comments;
    while (isspace((unsigned char)*ptr)) ptr++;

    if (strncmp(ptr, "int", 3) == 0 && (ptr[3] == ' ' || ptr[3] == '\t' || ptr[3] == '\0')) {
        ptr += 3;
    } else if (strncmp(ptr, "float", 5) == 0 && (ptr[5] == ' ' || ptr[5] == '\t' || ptr[5] == '\0')) {
        ptr += 5;
    } else if (strncmp(ptr, "char", 4) == 0 && (ptr[4] == ' ' || ptr[4] == '\t' || ptr[4] == '\0')) {
        ptr += 4;
    }

    while (isspace((unsigned char)*ptr)) ptr++;

    char *ident_start = ptr;
    while (*ptr && (isalnum((unsigned char)*ptr) || *ptr == '_')) ptr++;
    int ident_len = ptr - ident_start;
    strncpy(ident_val, ident_start, ident_len < max_size - 1 ? ident_len : max_size - 1);
    ident_val[ident_len < max_size - 1 ? ident_len : max_size - 1] = '\0';

    char *eq = strchr(line_no_comments, '=');
    if (eq) {
        eq++;
        while (isspace((unsigned char)*eq)) eq++;
        char *semi = strchr(eq, ';');
        if (semi) *semi = '\0';
        strncpy(expr, eq, max_size - 1);
        expr[max_size - 1] = '\0';
        trim_whitespace(expr);
    }
}

static void check_line_optimization(const char *line, int line_num) {
    (void)line_num;

    char cleaned[MAX_LINE_LENGTH];
    strncpy(cleaned, line, MAX_LINE_LENGTH - 1);
    cleaned[MAX_LINE_LENGTH - 1] = '\0';
    trim_whitespace(cleaned);

    if (cleaned[0] == '\0' || is_comment_line(cleaned))
        return;

    char line_no_comments[MAX_LINE_LENGTH];
    strncpy(line_no_comments, cleaned, MAX_LINE_LENGTH - 1);
    line_no_comments[MAX_LINE_LENGTH - 1] = '\0';
    strip_comments(line_no_comments);
    trim_whitespace(line_no_comments);

    if (line_no_comments[0] == '\0')
        return;

    char *ptr = line_no_comments;
    while (isspace((unsigned char)*ptr)) ptr++;

    if (!(strncmp(ptr, "int", 3) == 0 || strncmp(ptr, "float", 5) == 0 || strncmp(ptr, "char", 4) == 0))
        return;

    if (strchr(line_no_comments, '=') == NULL) return;

    char ident_val[64] = {0};
    char expr[MAX_LINE_LENGTH] = {0};
    extract_identifier_and_expr(line_no_comments, ident_val, expr, sizeof(expr));

    if (expr[0] == '\0') return;

    char op1[64] = {0}, op2[64] = {0}, oper[16] = {0};

    char *plus = strstr(expr, " + ");
    char *minus = strstr(expr, " - ");
    char *mul = strstr(expr, " * ");
    char *div = strstr(expr, " / ");

    if (plus) {
        strncpy(oper, "+", 15);
        oper[15] = '\0';
        strncpy(op1, expr, plus - expr);
        op1[plus - expr] = '\0';
        strcpy(op2, plus + 3);
        trim_whitespace(op1);
        trim_whitespace(op2);
    } else if (minus) {
        strncpy(oper, "-", 15);
        oper[15] = '\0';
        strncpy(op1, expr, minus - expr);
        op1[minus - expr] = '\0';
        strcpy(op2, minus + 3);
        trim_whitespace(op1);
        trim_whitespace(op2);
    } else if (mul) {
        strncpy(oper, "*", 15);
        oper[15] = '\0';
        strncpy(op1, expr, mul - expr);
        op1[mul - expr] = '\0';
        strcpy(op2, mul + 3);
        trim_whitespace(op1);
        trim_whitespace(op2);
    } else if (div) {
        strncpy(oper, "/", 15);
        oper[15] = '\0';
        strncpy(op1, expr, div - expr);
        op1[div - expr] = '\0';
        strcpy(op2, div + 3);
        trim_whitespace(op1);
        trim_whitespace(op2);
    } else {
        return;
    }

    int is_num1 = 1, is_num2 = 1;
    for (int i = 0; op1[i]; i++) {
        if (!isdigit((unsigned char)op1[i]) && op1[i] != '.' && op1[i] != '-') {
            is_num1 = 0;
            break;
        }
    }
    for (int i = 0; op2[i]; i++) {
        if (!isdigit((unsigned char)op2[i]) && op2[i] != '.' && op2[i] != '-') {
            is_num2 = 0;
            break;
        }
    }

    if (is_num1 && is_num2) {
        double n1 = atof(op1), n2 = atof(op2);
        double result;
        if (strcmp(oper, "+") == 0) result = n1 + n2;
        else if (strcmp(oper, "-") == 0) result = n1 - n2;
        else if (strcmp(oper, "*") == 0) result = n1 * n2;
        else result = n1 / n2;

        char original[256], optimized[256];
        snprintf(original, sizeof(original), "%s %s %s", op1, oper, op2);
        if ((int)result == result) {
            snprintf(optimized, sizeof(optimized), "%d", (int)result);
        } else {
            snprintf(optimized, sizeof(optimized), "%.2f", result);
        }

        char full_original[256], full_optimized[256];
        snprintf(full_original, sizeof(full_original), "%s = %s", ident_val, original);
        snprintf(full_optimized, sizeof(full_optimized), "%s = %s", ident_val, optimized);

        add_optimization(0, "constant folding", full_original, full_optimized);
    } else if (strcmp(oper, "+") == 0 && strcmp(op2, "0") == 0) {
        char full_original[256], full_optimized[256];
        snprintf(full_original, sizeof(full_original), "%s = %s + 0", ident_val, op1);
        snprintf(full_optimized, sizeof(full_optimized), "%s = %s", ident_val, op1);

        add_optimization(0, "simplificacao algebrica", full_original, full_optimized);
    } else if (strcmp(oper, "+") == 0 && strcmp(op1, "0") == 0) {
        char full_original[256], full_optimized[256];
        snprintf(full_original, sizeof(full_original), "%s = 0 + %s", ident_val, op2);
        snprintf(full_optimized, sizeof(full_optimized), "%s = %s", ident_val, op2);

        add_optimization(0, "simplificacao algebrica", full_original, full_optimized);
    }
}

static void print_report(const char *filename) {
    (void)filename;

    printf("======================================\n");
    printf("RELATORIO DO COMPILADOR\n");
    printf("======================================\n");

    printf("\n[ANALISE LEXICA]\n");
    Error *err = error_head;
    while (err) {
        if (strcmp(err->phase, "LEXICA") == 0) {
            printf("Linha %d: ERRO LEXICO\n", err->line);
            printf("Lexema: %s\n", err->description);
            printf("Motivo: simbolo nao reconhecido pela linguagem.\n");
        }
        err = err->next;
    }

    printf("\n[ANALISE SINTATICA]\n");
    err = error_head;
    while (err) {
        if (strcmp(err->phase, "SINTATICA") == 0) {
            printf("Linha %d: ERRO SINTATICO\n", err->line);
            printf("Motivo: %s\n", err->description);
        }
        err = err->next;
    }

    printf("\n[ANALISE SEMANTICA]\n");
    err = error_head;
    while (err) {
        if (strcmp(err->phase, "SEMANTICA") == 0) {
            printf("Linha %d: ERRO SEMANTICO\n", err->line);
            printf("Motivo: %s\n", err->description);
        }
        err = err->next;
    }

    printf("\n[OTIMIZACAO]\n");
    Optimization *opt = optim_head;
    while (opt) {
        printf("Trecho: %s\n", opt->original);
        printf("Sugestao: %s\n", opt->suggestion);
        printf("Resultado otimizado: %s\n\n", opt->optimized);
        opt = opt->next;
    }

    printf("======================================\n");
    printf("RESUMO\n");
    printf("======================================\n");
    printf("Erros lexicos encontrados: %d\n", error_count_lexical);
    printf("Erros sintaticos encontrados: %d\n", error_count_syntax);
    printf("Erros semanticos encontrados: %d\n", error_count_semantic);
    printf("Oportunidades de otimizacao: %d\n", optimization_count);
}

int main(void) {
    FILE *fp;
    char filename[128];

    symbol_table = create_symbol_table();

    printf("Digite o nome do arquivo: ");
    scanf("%127s", filename);

    fp = fopen(filename, "r");

    if (fp) {
        char line[MAX_LINE_LENGTH];
        int line_num = 0;

        while (fgets(line, MAX_LINE_LENGTH, fp)) {
            line_num++;
            lexical_analysis(line, line_num);
        }

        rewind(fp);
        line_num = 0;

        while (fgets(line, MAX_LINE_LENGTH, fp)) {
            line_num++;
            int syntax_ok = check_line_syntax(line, line_num);
            if (syntax_ok) {
                check_line_semantics(line, line_num);
                check_line_optimization(line, line_num);
            }
        }

        print_report(filename);

        fclose(fp);
    } else {
        printf("O arquivo não existe.\n");
    }

    return 0;
}
