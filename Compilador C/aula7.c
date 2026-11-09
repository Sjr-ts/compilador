
#include <stdio.h>
#include <string.h>

#define MAX_ERROS 100

typedef struct {
    int linha;
    char categoria[30];
    char mensagem[150];
} Erro;

Erro listaErros[MAX_ERROS];
int totalErros = 0;

void registrarErro(int linha, const char *categoria,
                   const char *mensagem) {

    if (totalErros >= MAX_ERROS) return;

    listaErros[totalErros].linha = linha;

    strcpy(listaErros[totalErros].categoria, categoria);
    strcpy(listaErros[totalErros].mensagem, mensagem);

    totalErros++;
}

void mostrarErros() {
    printf("\n===== RELATORIO DE ERROS =====\n");

    for (int i = 0; i < totalErros; i++) {
        printf("\nLinha: %d\n", listaErros[i].linha);
        printf("Categoria: %s\n", listaErros[i].categoria);
        printf("Descricao: %s\n", listaErros[i].mensagem);
    }

    printf("\nTotal de erros: %d\n", totalErros);
}

int main() {

    // Simulacao de erros encontrados pelo compilador

    registrarErro(3, "LEXICO",
                  "Simbolo desconhecido.");

    // O processamento continua!

    registrarErro(5, "SINTATICO",
                  "Ponto e virgula ausente.");

    // Continua novamente!

    registrarErro(8, "SEMANTICO",
                  "Variavel nao declarada.");

    mostrarErros();

    return 0;
}
