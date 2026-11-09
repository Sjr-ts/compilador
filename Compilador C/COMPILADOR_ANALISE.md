# Compilador C - Implementação Profissional

## Visão Geral do Projeto

Este documento descreve a implementação de um compilador educacional em C que realiza as seguintes fases:

1. Análise Léxica (já implementada)
2. Análise Sintática (a implementar)
3. Análise Semântica (a implementar)
4. Otimização (a implementar)

## Arquivos Existentes

### 1. analisador_lexico-master/main.c
O lexer básico que identifica tokens:
- Lê arquivos C caractere por caractere
- Identifica: palavras reservadas, inteiros, floats, caracteres, strings, identificadores, operadores, delimitadores
- Usa funções do checkers.h para validação

### 2. analisador_lexico-master/checkers.c/checkers.h
Funções de verificação de tokens:
- check_reserved_word()
- check_integer()
- check_float()
- check_character()
- check_string()
- check_delimiter()
- check_arithmetic_operator()
- check_relational_operator()

### 3. codigo_teste_compilador.c
Arquivo de teste com múltiplos tipos de erros:
- Erros léxicos (@ símbolo não reconhecido)
- Erros sintáticos (faltando ;, ordem inválida)
- Erros semânticos (redeclaração, não declarada, tipos incompatíveis)
- Oportunidades de otimização (constant folding, simplificação algébrica)

## Specifications

### Estrutura Esperada de Saída

```
======================================
RELATÓRIO DO COMPILADOR
======================================

[ANALISE LEXICA]
Linha 18: ERRO LEXICO
Lexema: @
Motivo: simbolo nao reconhecido pela linguagem.

[ANALISE SINTATICA]
Linha 15: ERRO SINTATICO
Motivo: esperado ';' ao final da declaracao.

Linha 16: ERRO SINTATICO
Trecho: int = valor 20;
Motivo: declaracao nao segue a estrutura esperada:
       TIPO IDENTIFICADOR = VALOR ;

[ANALISE SEMANTICA]
Linha 11: ERRO SEMANTICO
Identificador: idade
Motivo: identificador ja declarado neste escopo.

Linha 12: ERRO SEMANTICO
Identificador: quantidade
Motivo: variavel utilizada antes de ser declarada.

Linha 13: Erro semântico
Identificador: nome
Motivo: variavel declarada como INT recebeu valor STRING.

[OTIMIZACAO]
Trecho: int resultado = 10 + 20;
Sugestao: constant folding
Resultado otimizado: int resultado = 30;

... (continua para 6 oportunidades de otimização) ...

======================================
RESUMO
======================================
Erros lexicos encontrados: 1
Erros sintaticos encontrados: 2
Erros semanticos encontrados: 3
Oportunidades de otimizacao: 6
```

## Arquitetura do Compilador

### 1. Análises Léxica e Sintática (Implementar)

#### Lexer (already done)
- Analisa o arquivo caractere por caractere
- Constrói lexemas (sequências alfanuméricas)
- Identifica tokens usando funções do checkers.h

#### Parser (to implement)
- Implementa a gramática: `tipo identificador = valor ;`
- Valida estrutura das declarações
- Detecta erros sintáticos

### 2. Análise Semântica (implementar)

#### Symbol Table
```c
typedef struct Symbol {
    char name[64];
    char type[16]; // "int", "float", "char"
    int declared;   // 1 = declared, 0 = used before declaration
    struct Symbol* next;
} Symbol;
```

#### Semantic Checks
- Detecção de redeclaração (mesmo identificador)
- Detecção de uso antes da declaração
- Verificação de compatibilidade de tipos
- Validação de escopo (global vs local)

### 3. Otimização (implementar)

#### Constant Folding
- Detecta expressões numéricas constantes
- Avalia matematicamente: `5 * 2 → 10`

#### Algebraic Simplification
- Identifica expressões triviais
- Simplifica: `resultado + 0 → resultado`

#### Dead Code Elimination
- Detecta atribuições não utilizadas
- Remove código morto

## Arquivo de Testes Detalhado

```c
/* ARQUIVO DE TESTE */

// LINHA 6-8: Declarações corretas
int idade = 25;           // OK
float salario = 3500.50;   // OK  
char letra = 'A';          // OK

// LINHA 10-12: Declarações corretas
int resultado = 10 + 20;   // OK - constante folding -> 30
int dobro = 5 * 2;        // OK - constante folding -> 10
int neutro = resultado + 0; // OK - simplificação algébrica -> resultado

// LINHA 14: ERRO SEMÂNTICO - redeclaração
int idade = 30;          // ERRO: idade já declarada (linha 6)

// LINHA 15: ERRO SEMÂNTICO - não declarada  
quantidade = 8;          // ERRO: quantidade nunca declarada

// LINHA 16: ERRO SEMÂNTICO - tipo incompatível
int nome = "Maria";      // ERRO: int não pode receber string

// LINHA 18: ERRO SINTÁTICO - falta ;
int total = 10 + 5       // ERRO: falta ; no final

// LINHA 19: ERRO SINTÁTICO - ordem inválida
int = valor 20;          // ERRO: ordem TIPO IDENTIFICADOR = VALOR inválida

// LINHA 21: ERRO LÉXICO - símbolo não reconhecido
int codigo = 10 @ 2;     // ERRO: @ não é operador válido

// LINHA 23-25: Oportunidades de otimização
int x = 10;              // OK
x = 20;                  // OK - código morto (primeiro valor não usado)
int y = 4 * 5;           // OK - constante folding -> 20
int z = y + 0;           // OK - simplificação algébrica -> y
```

## Próximos Passos

1. **Implementar o Parser Sintático**
   - Gramática baseada em funções recursivas
   - Validação linha-a-linha
   - Reportar erros com linhas exatas

2. **Implementar Symbol Table e Análise Semântica**
   - Gerenciamento de escopo
   - Verificação de tipos
   - Detecção de erros semânticos

3. **Implementar o Otimizador**
   - Constant folding para expressões aritméticas
   - Simplificação algébrica para operações triviais
   - Dead code elimination

4. **Testar com o arquivo de teste**
   - Verificar todas as contagens de erros
   - Validar saídas de otimização

5. **Gerar relatório final**
   - Formato especificado no README
   - Contagens de todos os erros e oportunidades
