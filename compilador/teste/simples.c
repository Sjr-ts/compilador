#include <stdio.h>

int main() {
    int nota1 = 8;
    int nota2 = 6;
    float media;

    // Código tenta calcular a média de duas notas
    media = nota1 + nota2 / 2; 

    printf("A media das notas eh: %.2f\n", media);

    // Código tenta verificar se o aluno foi aprovado (média maior ou igual a 7)
    if (media >= 7); {
        printf("Aluno Aprovado!\n");
    }

    return 0;
}
