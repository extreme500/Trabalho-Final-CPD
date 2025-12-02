#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cliente.c"
#include "metadados.c"
#include "no.c"
#include "no_dados.c"


typedef struct {
    char cod_esc[15];
    char cidade[40];
    char estado[20];
} Local;

typedef struct {
    char cod_prova1[10];
    char ling_est;
    char cor[60];
    char gab_lc[50];
    char gab_ch[50];
} ProvaD1;

typedef struct {
    char cod_prova2[10];
    char cor[60];
    char gab_cn[50];
    char gab_mt[50];
} ProvaD2;


typedef struct {
    char nu_seq[15];
    char cod_esc[15];
    int ano;
    float nota_cn;
    float nota_ch;
    float nota_lc;
    float nota_mt;
    float nota_red;
    char resp_cn[50];
    char resp_ch[50];
    char resp_lc[50];
    char resp_mt[50];
    char cod_prova1[10];
    char cod_prova2[10];
    char ling_est;
} Participante;

Local ler_local(char *linha) {
    Local l;
    char *p = strtok(linha, ";");
    strcpy(l.cod_esc, p);
    p = strtok(NULL, ";");
    strcpy(l.cidade, p);
    p = strtok(NULL, ";");
    strcpy(l.estado, p);

    return l;
}

int main() {
    FILE *f = fopen("RESULTADOS_2024_simplificado.csv", "r");
    if (!f) {
        printf("Erro ao abrir arquivo!\n");
        return 1;
    }
    //printf("oi");

    char linha[1024];

    /* lê cada linha do CSV
    while (fgets(linha, sizeof(linha), f)) {

        // remove \n no final, se existir
        linha[strcspn(linha, "\n")] = '\0';

        printf("Linha lida: %s\n", linha);
    }*/

    while (fgets(linha, sizeof(linha), f))
        {
            linha[strcspn(linha, "\n")] = '\0';
            Local l = ler_local(linha);
            printf("Escola=%s, Cidade=%s, Estado=%s\n",l.cod_esc, l.cidade, l.estado);
        }


    fclose(f);
    return 0;
}
