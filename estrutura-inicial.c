#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#define COMMAND_MAX_SIZE 100
#define ORDEM 100

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
    char nu_seq[15];
    int ano;
    char cod_esc[15];
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

    //além disso antes de adicionar um participante tem que ter criado aquele outro arquivo de tabela que relaciona cod_esc - cidade - estado
    // e outro arquivo que relaciona prova - gabarito (considerando a lingua estrangeira na comparação)
    return indice_registro;
}

typedef struct {
    int nu_seq;
    int indice_registro;
} EntradaIndice;

int importar_participantes_csv(char *nome_csv,
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
            "%14[^;];%d;%14[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%f;%f;%f;%f;%f;%f;%f;%f;%49[^;];%49[^;];%49[^;];%49[^;];%d;%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%f",
            &p.nu_seq,
            &p.ano,
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

        if (ok != 17) {
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

void ler_todos_participantes(const char *nome) {
    FILE *fp = fopen(nome, "rb");
    if (!fp) {
        perror("Erro ao abrir arquivo");
        return;
    }

    HeaderParticipantes h;

    fread(&h, sizeof(HeaderParticipantes), 1, fp);

    printf("Total de registros: %d\n\n", h.qtd_registros);

    for (int i = 0; i < h.qtd_registros; i++) {

        Participante p;

        fread(&p, sizeof(Participante), 1, fp);

        printf("Registro %d:\n", i);
        printf("  nu_seq: %s\n", p.nu_seq);
        printf("  ano: %d\n", p.ano);
        printf("  cod_esc: %s\n", p.cod_esc);
        printf("  nota_ch: %.2f\n", p.nota_ch);
        printf("  resp_cn: %s\n", p.resp_cn);
        printf("  ling_est: %d\n", p.ling_est);
        printf("  nota_red: %.2f\n", p.nota_red);
        printf("\n");
    }

    fclose(fp);
}

void to_lowercase(char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        s[i] = tolower(s[i]);
    }
}



int main(void) {
    bool sair = false;
    char nome_csv[100];
    const char *nome_bin = "participantes.bin";
    while(!sair)
    {

        char comando[COMMAND_MAX_SIZE];
        printf("Indique o que voce quer fazer:\nCLEAR - Limpa todo o banco de dados de registros\nREAD - Le um arquivo com registros\nSHOW - Mostra na tela os registros salvos\nEXIT - Sai do programa\n");

        fgets(comando, COMMAND_MAX_SIZE, stdin);
        size_t len = strlen(comando);
        if (len > 0 && comando[len-1] == '\n') {
            comando[len-1] = '\0';
        }
        to_lowercase(comando);

        if (strcmp(comando, "clear") == 0)
        {
            if (remove("participantes.bin") == 0)
            {
                printf("Arquivo antigo '%s' removido com sucesso.\n", "participantes.bin");
            }
            else
            {
            perror("Aviso: Nao foi possivel remover o arquivo antigo");
            }
        }
         else if (strcmp(comando, "read") == 0)
        {
            printf("\nEscreva o nome do arquivo csv que voce quer ler (incluindo a extensao)\n");
            fgets(nome_csv, 100, stdin);
            size_t len2 = strlen(nome_csv);
            if (len > 0 && nome_csv[len2-1] == '\n')
            {
                nome_csv[len2-1] = '\0';
            }
            importar_participantes_csv(nome_csv, nome_bin);
        }
        else if (strcmp(comando, "show") == 0)
        {
            ler_todos_participantes("participantes.bin");
        }
        else if (strcmp(comando, "exit") == 0)
        {
            printf("\nSaindo do programa...\n");
            sair = true;
        }
        else
        {
            printf("\nComando '%s' nao reconhecido. Tente usar algum dos comandos citados.\n", comando);
        }
    }
    //const char *nome_csv = "RESULTADOS_2024_simplificado.csv";
    //ler_todos_participantes("participantes.bin");
    return 0;
}

