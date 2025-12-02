#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int qtd_registros;
} HeaderParticipantes;

typedef struct {
    float cod_esc;
    char cidade[40];
    char estado[20];
} Local;

typedef struct {
    float cod_prova_ch;
    float cod_prova_lc;
    int ling_est;
    char gab_ch[50];
    char gab_lc[50];
} ProvaD1;

typedef struct {
    float cod_prova_cn;
    float cod_prova_mt;
    char gab_cn[50];
    char gab_mt[50];
} ProvaD2;

typedef struct {
    float nu_seq;
    float cod_esc;
    float cod_prova_cn;
    float cod_prova_ch;
    float cod_prova_lc;
    float cod_prova_mt;
    float nota_cn;
    float nota_ch;
    float nota_lc;
    float nota_mt;
    char resp_cn[50];
    char resp_ch[50];
    char resp_lc[50];
    char resp_mt[50];
    int ling_est;
    float nota_red;

} Participante;


FILE *abrir_arquivo_participantes(const char *nome, HeaderParticipantes *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo de participantes");
            return NULL;
        }

        h->qtd_registros = 0;
        fwrite(h, sizeof(HeaderParticipantes), 1, fp);
        fflush(fp);

    } else {
        fread(h, sizeof(HeaderParticipantes), 1, fp);
    }

    return fp;
}

int inserir_participante(FILE *fp, HeaderParticipantes *h, Participante *p) {

    int indice_registro = h->qtd_registros;

    long offset = sizeof(HeaderParticipantes) + indice_registro * sizeof(Participante);
    fseek(fp, offset, SEEK_SET);
    fwrite(p, sizeof(Participante), 1, fp);

    h->qtd_registros++;
    fseek(fp, 0, SEEK_SET);
    fwrite(h, sizeof(HeaderParticipantes), 1, fp);
    fflush(fp);

    return indice_registro;
}

typedef struct {
    int nu_seq;
    int indice_registro;
} EntradaIndice;

int importar_participantes_csv(const char *nome_csv,
                               const char *nome_bin) {
    HeaderParticipantes header;
    FILE *fp_bin = abrir_arquivo_participantes(nome_bin, &header);
    if (fp_bin == NULL) {
        return 1;
    }

    FILE *fp_csv = fopen(nome_csv, "r");
    if (fp_csv == NULL) {
        perror("Erro ao abrir CSV de participantes");
        fclose(fp_bin);
        return 1;
    }

    char linha[2048];

    if (fgets(linha, sizeof(linha), fp_csv) == NULL) {
        printf("CSV vazio.\n");
        fclose(fp_csv);
        fclose(fp_bin);
        return 1;
    }

    int linhas_lidas = 0;

    while (fgets(linha, sizeof(linha), fp_csv) != NULL) {
        Participante p;
        memset(&p, 0, sizeof(Participante));

        int ok = sscanf(
            linha,
            "%f;%f;%f;%f;%f;%f;%f;%f;%f;%f;%49[^;];%49[^;];%49[^;];%49[^;];%d;%f",
            &p.nu_seq,
            &p.cod_esc,
            &p.cod_prova_cn,
            &p.cod_prova_ch,
            &p.cod_prova_lc,
            &p.cod_prova_mt,
            &p.nota_cn,
            &p.nota_ch,
            &p.nota_lc,
            &p.nota_mt,
            p.resp_cn,
            p.resp_ch,
            p.resp_lc,
            p.resp_mt,
            &p.ling_est,
            &p.nota_red
        );

        if (ok != 16) {
            printf("Linha ignorada (formato inesperado). Lidos = %d\n", ok);
            printf("Linha: %s\n", linha);
            continue;
        }

        int indice = inserir_participante(fp_bin, &header, &p);

        printf("Linha: %s\n", linha);
        linhas_lidas++;
    }

    printf("Importacao concluida. Linhas validas inseridas: %d\n", linhas_lidas);
    printf("Total de registros no arquivo: %d\n", header.qtd_registros);

    fclose(fp_csv);
    fclose(fp_bin);

    return 0;
}

int main(void) {
    const char *nome_bin = "participantes.bin";
    const char *nome_csv = "struct_participante_10.csv";

    importar_participantes_csv(nome_csv, nome_bin);

    return 0;
}

