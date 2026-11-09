#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_FILE_SIZE 16384
#define MAX_LINE_LENGTH 256
#define MAX_TOKEN_LENGTH 64
#define MAX_SYMBOLS 100

// Tipos de tokens
#define TK_RESERVED_WORD 1
#define TK_INTEGER 2
#define TK_FLOAT 3
#define TK_CHARACTER 4
#define TK_STRING 5
#define TK_IDENTIFIER 6
#define TK_OPERATOR_ARITHMETIC 7
#define TK_OPERATOR_RELATIONAL 8
#define TK_ASSIGNMENT 9
#define TK_DELIMITER 10
#define TK_COMMENT 11
#define TK_ERROR 12

// Tipos de dados
#define TYPE_INT "int"
#define TYPE_FLOAT "float"
#define TYPE_CHAR "char"

// Estrutura de symbol table
struct Symbol {
    char name[64];
    char type[16];
    int line;           // Linha onde foi declarada
    int used_before_decl;  // 1 se usada antes da declaração
    struct Symbol* next;
};

struct SymbolTable {
    struct Symbol* head;
    int count;
};

// Representação AST para otimização
struct ASTNode {
    char op[32];        // Operador: "=", "+", "-", "*", "/", "==", ">", "<", ">=", "<=", "!="
    char value[64];     // Valor para literais (números, strings, caracteres)
    char type[16];      // Tipo para literais
    struct ASTNode* left;
    struct ASTNode* right;
    int line;           // Linha da expressão
    struct ASTNode* next;  // Lista de declarações/linked list
};

// Erros e oportunidades
struct Error {
    int line;
    char type[32];      // "lexico", "sintatico", "semantico"
    char description[128];
    struct Error* next;
};

struct Optimization {
    int line;
    char suggestion[128];
    char optimized[128];
    char trecho[256];
    struct Optimization* next;
};

// Protótipos
struct SymbolTable create_symbol_table();
void insert_symbol(struct SymbolTable* table, const char* name, const char* type, int line);
struct Symbol* find_symbol(struct SymbolTable* table, const char* name);
void mark_symbol_used(struct SymbolTable* table, const char* name);

int is_reserved_word(const char* token);
int is_delimiter(char c);
int is_arithmetic_operator(char c);
int is_relational_operator(const char* token);
int is_assignment(char c);

void print_token_info(const char* token, int line);
int tokenize_line(const char* line, char tokens[][MAX_TOKEN_LENGTH], int* token_count, int line_num);

int check_syntax(const char* line, int line_num, struct SymbolTable* symbol_table, struct Error** errors);
void check_semantics(const char* line, int line_num, struct SymbolTable* symbol_table, struct Error** errors);
void optimize_expression(const char* trecho, struct Optimization** optims);

void print_report(const char* filename);
