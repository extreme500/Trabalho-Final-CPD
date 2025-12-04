#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h> // Para a função fabsf (float absolute value) e fseek/ftell

#define COMMAND_MAX_SIZE 100
#define ORDEM 5 // Ordem B+ Tree (M=5). O nó de índice tem 4 chaves (m) e 5 ponteiros (p). O nó de dados tem 4 entradas (m).

/************************************************ ESTRUTURAS DO PARTICIPANTE ************************************************/

typedef struct {
    int qtd_registros;
} HeaderParticipantes;

// Estrutura simplificada para o foco na Árvore B+
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

/************************************************ ESTRUTURAS DA ÁRVORE B+ ADAPTADA ************************************************/

// Entrada de dados no nó folha (chave é a nota, valor é o índice do registro no arquivo binário)
typedef struct {
    float nota; // Chave (key) da B+ Tree: a nota do participante
    int indice_registro; // Ponteiro para o registro completo no "participantes.bin"
} EntradaIndiceNota;

// Nó de Dados (Folha)
typedef struct {
    int ppai; // Posição no arquivo de índice do nó pai
    int m; // Quantidade de entradas (máx 4)
    EntradaIndiceNota s[ORDEM - 1]; // Chaves/dados
    int prox; // Ponteiro para a próxima folha (implementação de lista encadeada)
} NoDados;

// Nó de Índice (Não-Folha)
typedef struct No {
    int ppai; // Posição no arquivo de índice do nó pai
    int m; // Quantidade de chaves (máx 4)
    float s[ORDEM - 1]; // Chaves (keys), agora floats
    int p[ORDEM]; // Ponteiros para filhos (posições no arquivo de índice ou dados)
    int flag_aponta_folha; // 1 se aponta para NoDados, 0 se aponta para No
} No;

// Estrutura de Metadados
typedef struct {
    int pont_raiz; // Posição do nó raiz no arquivo de índice/dados
    int flag_raiz_folha; // 1 se a raiz é folha (NoDados), 0 se é nó de índice (No)
} Metadados;

// Estrutura de Informação de Busca
typedef struct Info {
    int p_f_indice; // Posição do nó no arquivo de índice (pai)
    int p_f_dados; // Posição do nó de dados (folha)
    int pos_vetor_dados; // Posição que a chave está ou deveria estar no vetor de dados
    int encontrou; // 1 se achou uma entrada com a mesma chave (para B+ simples, isso é raro/opcional), ou 0 se achou o local de inserção
} Info;

/************************************************ HELPERS DA ÁRVORE B+ ************************************************/

long tamanho_participante() { return sizeof(Participante); }
long tamanho_header() { return sizeof(HeaderParticipantes); }
long tamanho_no() { return sizeof(No); }
long tamanho_no_dados() { return sizeof(NoDados); }
long tamanho_metadados() { return sizeof(Metadados); }

// Cria um nó de índice (No) vazio
No *cria_no() {
    No *n = (No *)malloc(tamanho_no());
    if (!n) { perror("Erro ao alocar No"); exit(1); }
    n->ppai = -1;
    n->m = 0;
    n->flag_aponta_folha = 0;
    for (int i = 0; i < ORDEM - 1; i++) {
        n->s[i] = -1.0;
        n->p[i] = -1;
    }
    n->p[ORDEM - 1] = -1;
    return n;
}

// Cria um nó de dados (NoDados) vazio
NoDados *cria_no_dados() {
    NoDados *nd = (NoDados *)malloc(tamanho_no_dados());
    if (!nd) { perror("Erro ao alocar NoDados"); exit(1); }
    nd->ppai = -1;
    nd->m = 0;
    nd->prox = -1;
    for (int i = 0; i < ORDEM - 1; i++) {
        nd->s[i].nota = -1.0;
        nd->s[i].indice_registro = -1;
    }
    return nd;
}

// Leitura/Escrita genérica de structs
Metadados *le_metadados(FILE *f) {
    Metadados *md = (Metadados *)malloc(tamanho_metadados());
    if (fseek(f, 0, SEEK_SET) != 0) return NULL;
    if (fread(md, tamanho_metadados(), 1, f) != 1) { free(md); return NULL; }
    return md;
}

void salva_metadados(Metadados *md, FILE *f) {
    fseek(f, 0, SEEK_SET);
    fwrite(md, tamanho_metadados(), 1, f);
    fflush(f);
}

No *buscar_no(int pos, FILE *f) {
    if (pos == -1) return NULL;
    No *n = cria_no();
    if (fseek(f, tamanho_no() * pos, SEEK_SET) != 0) { free(n); return NULL; }
    if (fread(n, tamanho_no(), 1, f) != 1) { free(n); return NULL; }
    return n;
}

void salva_no(No *n, FILE *f, int pos) {
    if (pos == -1) {
        fseek(f, 0, SEEK_END);
    } else {
        fseek(f, tamanho_no() * pos, SEEK_SET);
    }
    fwrite(n, tamanho_no(), 1, f);
    fflush(f);
}

NoDados *buscar_no_dados(int pos, FILE *f) {
    if (pos == -1) return NULL;
    NoDados *nd = cria_no_dados();
    if (fseek(f, tamanho_no_dados() * pos, SEEK_SET) != 0) { free(nd); return NULL; }
    if (fread(nd, tamanho_no_dados(), 1, f) != 1) { free(nd); return NULL; }
    return nd;
}

void salva_no_dados(NoDados *nd, FILE *f, int pos) {
    if (pos == -1) {
        fseek(f, 0, SEEK_END);
    } else {
        fseek(f, tamanho_no_dados() * pos, SEEK_SET);
    }
    fwrite(nd, tamanho_no_dados(), 1, f);
    fflush(f);
}

// Função para iniciar o arquivo de metadados
void iniciar_arquivo_metadados(FILE *f) {
    Metadados md = { .pont_raiz = -1, .flag_raiz_folha = 1 };
    salva_metadados(&md, f);
}

// Atualiza o ponteiro raiz no metadados
void atualiza_arquivo_metadados(FILE *f_metadados, int nova_raiz_pos, int is_folha) {
    Metadados *md = le_metadados(f_metadados);
    md->pont_raiz = nova_raiz_pos;
    md->flag_raiz_folha = is_folha;
    salva_metadados(md, f_metadados);
    free(md);
}

// Insere uma chave e ponteiros em um nó de índice (mantendo a ordenação)
void inserir_chave_em_no(No *no, float chave, int p_esq, int p_dir) {
    int i = no->m - 1;
    while (i >= 0 && no->s[i] > chave) {
        no->s[i + 1] = no->s[i];
        no->p[i + 2] = no->p[i + 1];
        i--;
    }
    no->s[i + 1] = chave;
    if (p_esq != -1) {
        no->p[i + 1] = p_esq;
    }
    no->p[i + 2] = p_dir;
    no->m++;
}

// Insere uma entrada em um nó de dados (mantendo a ordenação)
NoDados *inserir_entrada_em_no_dado(NoDados *nd, EntradaIndiceNota entrada) {
    int i = nd->m - 1;
    while (i >= 0 && nd->s[i].nota > entrada.nota) {
        nd->s[i + 1] = nd->s[i];
        i--;
    }
    nd->s[i + 1] = entrada;
    nd->m++;
    return nd;
}

// Atualiza o ppai de um nó de dados (folha)
void atualiza_pai_de_no_dado(FILE *f_dados, int p_f_dados, int ppai) {
    NoDados *nd = buscar_no_dados(p_f_dados, f_dados);
    if (!nd) return;
    nd->ppai = ppai;
    salva_no_dados(nd, f_dados, p_f_dados);
    free(nd);
}

// Atualiza o ppai de um nó de índice
void atualiza_pai_de_no(FILE *f_indice, int p_f_indice, int ppai) {
    No *no = buscar_no(p_f_indice, f_indice);
    if (!no) return;
    no->ppai = ppai;
    salva_no(no, f_indice, p_f_indice);
    free(no);
}


/************************************************ FUNÇÕES PRINCIPAIS DA ÁRVORE B+ ************************************************/

// retorna informações sobre a busca (posição da folha onde deve estar ou ser inserido)
Info *busca(float x, FILE *f_metadados, FILE *f_indice, FILE *f_dados) {

    Info *info = (Info *)malloc(sizeof(Info));
    info->p_f_indice = -1;
    info->p_f_dados = -1;
    info->pos_vetor_dados = -1;
    info->encontrou = 0; // 0: não achou (ou achou local de inserção), 1: achou

    Metadados *md = le_metadados(f_metadados);
    int p_atual;

    if (md->pont_raiz == -1) {
        free(md);
        return info; // Árvore vazia
    }

    if (md->flag_raiz_folha == 1) {
        // Raiz é a própria folha de dados
        p_atual = md->pont_raiz;
        info->p_f_dados = p_atual;
        info->p_f_indice = -1;
        free(md);
    } else {
        // Começa a busca no nó de índice (raiz)
        p_atual = md->pont_raiz;
        info->p_f_indice = p_atual;
        free(md);

        while (p_atual != -1) {
            No *pag = buscar_no(p_atual, f_indice);
            info->p_f_indice = p_atual; // Salva a posição do nó de índice atual

            int proximo_p = -1;
            int i;
            for (i = 0; i < pag->m; i++) {
                if (x < pag->s[i]) {
                    proximo_p = pag->p[i];
                    break;
                }
            }
            if (proximo_p == -1) {
                // Se a chave for maior que todas, vai para o último ponteiro
                proximo_p = pag->p[pag->m];
            }

            if (pag->flag_aponta_folha) {
                // Chegou no nível que aponta para as folhas de dados
                info->p_f_dados = proximo_p;
                free(pag);
                break;
            }

            p_atual = proximo_p;
            free(pag);
        }
    }

    // Busca no nó de dados (folha)
    if (info->p_f_dados != -1) {
        NoDados *pag_dados = buscar_no_dados(info->p_f_dados, f_dados);
        if (!pag_dados) return info; // Erro ao ler nó de dados

        int i;
        for (i = 0; i < pag_dados->m; i++) {
            // Comparação de floats (tolerância para erros de representação)
            if (fabsf(pag_dados->s[i].nota - x) < 0.0001f) {
                info->encontrou = 1;
                info->pos_vetor_dados = i;
                free(pag_dados);
                return info;
            } else if (pag_dados->s[i].nota > x) {
                info->pos_vetor_dados = i;
                info->encontrou = 0;
                free(pag_dados);
                return info;
            }
        }
        // Se a chave for maior que todas no nó de dados, a posição de inserção é a próxima vazia
        info->pos_vetor_dados = pag_dados->m;
        info->encontrou = 0;
        free(pag_dados);
        return info;
    }

    return info;
}

// Implementação da propagação de split
void inserir_em_arquivo_de_indice(float chave, int p_pai_original, int flag_aponta_folha, int p_filho_esq, int p_filho_dir, FILE *f_metadados, FILE *f_indice, FILE *f_dados) {

    Metadados *md = le_metadados(f_metadados);

    if (md->pont_raiz == -1) {
        // Este caso não deve ocorrer se a inserção de folha for feita corretamente
        // Mas se a árvore estiver vazia, cria a raiz como folha (NoDados)
        // Isso será tratado no 'inserir' principal.
        free(md);
        return;
    }

    if (p_pai_original == -1) { // Criação de uma nova raiz
        No *nova_raiz = cria_no();
        inserir_chave_em_no(nova_raiz, chave, p_filho_esq, p_filho_dir);
        nova_raiz->flag_aponta_folha = flag_aponta_folha;

        // Salva a nova raiz no fim do arquivo de índice
        salva_no(nova_raiz, f_indice, -1);
        int nova_raiz_pos = (ftell(f_indice) / tamanho_no()) - 1;

        // Atualiza os metadados
        atualiza_arquivo_metadados(f_metadados, nova_raiz_pos, 0);

        // Atualiza os pais dos filhos
        if (flag_aponta_folha) {
            atualiza_pai_de_no_dado(f_dados, p_filho_esq, nova_raiz_pos);
            atualiza_pai_de_no_dado(f_dados, p_filho_dir, nova_raiz_pos);
        } else {
            atualiza_pai_de_no(f_indice, p_filho_esq, nova_raiz_pos);
            atualiza_pai_de_no(f_indice, p_filho_dir, nova_raiz_pos);
        }

        free(nova_raiz);
        free(md);
        return;
    }

    // Nó pai existe e pode ter espaço
    No *no_pai = buscar_no(p_pai_original, f_indice);

    if (no_pai->m < ORDEM - 1) { // O nó tem espaço
        inserir_chave_em_no(no_pai, chave, -1, p_filho_dir); // -1 para p_esq pois o p_esq já está no nó
        salva_no(no_pai, f_indice, p_pai_original);

        // Atualização do pai do novo filho direito
        if (no_pai->flag_aponta_folha) {
            atualiza_pai_de_no_dado(f_dados, p_filho_dir, p_pai_original);
        } else {
            atualiza_pai_de_no(f_indice, p_filho_dir, p_pai_original);
        }

        free(no_pai);
        free(md);
        return;

    } else { // Nó de índice cheio (ORDEM - 1 = 4 chaves) -> Split

        // Cria um vetor auxiliar para 5 chaves e 6 ponteiros
        float chaves_aux[ORDEM];
        int ponteiros_aux[ORDEM + 1];

        // Copia as chaves e ponteiros existentes
        for (int i = 0; i < ORDEM - 1; i++) {
            chaves_aux[i] = no_pai->s[i];
            ponteiros_aux[i] = no_pai->p[i];
        }
        ponteiros_aux[ORDEM - 1] = no_pai->p[ORDEM - 1]; // Último ponteiro original

        // Insere a nova chave e ponteiro (chave é a que subiu do split da folha, p_filho_dir é o novo nó)
        int i = no_pai->m;
        while (i > 0 && chaves_aux[i - 1] > chave) {
            chaves_aux[i] = chaves_aux[i - 1];
            ponteiros_aux[i + 1] = ponteiros_aux[i];
            i--;
        }
        chaves_aux[i] = chave;
        ponteiros_aux[i + 1] = p_filho_dir;

        // Split
        float chave_subir = chaves_aux[ORDEM / 2]; // Chave do meio (índice 2)
        int p_pai_do_pai = no_pai->ppai;

        // Cria os novos nós (nó esquerdo substitui o nó_pai original)
        No *n1 = cria_no();
        n1->ppai = p_pai_do_pai;
        n1->flag_aponta_folha = no_pai->flag_aponta_folha;

        // Nó esquerdo (n1) - 2 chaves, 3 ponteiros
        n1->m = ORDEM / 2 - 1; // 2 chaves
        for (int j = 0; j < n1->m; j++) {
            n1->s[j] = chaves_aux[j];
            n1->p[j] = ponteiros_aux[j];
        }
        n1->p[n1->m] = ponteiros_aux[n1->m]; // Último ponteiro do n1

        // Nó direito (n2) - 2 chaves, 3 ponteiros
        No *n2 = cria_no();
        n2->ppai = p_pai_do_pai;
        n2->flag_aponta_folha = no_pai->flag_aponta_folha;
        n2->m = ORDEM / 2 - 1; // 2 chaves
        for (int j = 0; j < n2->m; j++) {
            n2->s[j] = chaves_aux[j + ORDEM / 2 + 1];
            n2->p[j] = ponteiros_aux[j + ORDEM / 2 + 1];
        }
        n2->p[n2->m] = ponteiros_aux[n2->m + ORDEM / 2 + 1]; // Último ponteiro do n2

        // Salva o nó esquerdo (n1) na posição original
        salva_no(n1, f_indice, p_pai_original);

        // Salva o nó direito (n2) no fim do arquivo
        salva_no(n2, f_indice, -1);
        int n2_pos = (ftell(f_indice) / tamanho_no()) - 1;

        // Atualiza os pais dos filhos do n2
        for (int j = 0; j <= n2->m; j++) {
            if (n2->flag_aponta_folha) {
                atualiza_pai_de_no_dado(f_dados, n2->p[j], n2_pos);
            } else {
                atualiza_pai_de_no(f_indice, n2->p[j], n2_pos);
            }
        }

        free(n1);
        free(n2);
        free(no_pai);
        free(md);

        // Propaga a chave subida para o pai
        inserir_em_arquivo_de_indice(chave_subir, p_pai_do_pai, 0, p_pai_original, n2_pos, f_metadados, f_indice, f_dados);
    }
}


// Insere uma entrada (nota + índice) na Árvore B+
void inserir_bmais(float nota, int indice_registro, FILE *f_metadados, FILE *f_indice, FILE *f_dados) {

    rewind(f_metadados);
    rewind(f_indice);
    rewind(f_dados);

    Metadados *md = le_metadados(f_metadados);
    EntradaIndiceNota nova_entrada = { .nota = nota, .indice_registro = indice_registro };

    if (md->pont_raiz == -1) { // Árvore vazia
        NoDados *nd = cria_no_dados();
        nd = inserir_entrada_em_no_dado(nd, nova_entrada);

        salva_no_dados(nd, f_dados, 0); // Salva na posição 0
        int nd_pos = 0;

        md->pont_raiz = nd_pos;
        md->flag_raiz_folha = 1;
        salva_metadados(md, f_metadados);

        free(nd);
        free(md);
        return;
    }

    Info *info = busca(nota, f_metadados, f_indice, f_dados);

    if (info->encontrou == 1) {
        printf("Aviso: Chave (nota %.2f) já existe na arvore. Inserindo mesmo assim, mas B+ Tree original evita duplicatas.\n", nota);
    }

    int p_f_dados_original = info->p_f_dados;
    NoDados *nd = buscar_no_dados(p_f_dados_original, f_dados);

    if (nd->m < ORDEM - 1) { // Nó de dados tem espaço
        nd = inserir_entrada_em_no_dado(nd, nova_entrada);
        salva_no_dados(nd, f_dados, p_f_dados_original);
        free(nd);
        free(md);
        free(info);
        return;
    } else { // Nó de dados cheio -> Split

        // Cria um vetor auxiliar com 5 entradas ordenadas
        EntradaIndiceNota entradas_aux[ORDEM];
        for (int i = 0; i < ORDEM - 1; i++) {
            entradas_aux[i] = nd->s[i];
        }

        // Ordenação por inserção da nova entrada
        int i = ORDEM - 1;
        while (i > 0 && entradas_aux[i - 1].nota > nova_entrada.nota) {
            entradas_aux[i] = entradas_aux[i - 1];
            i--;
        }
        entradas_aux[i] = nova_entrada;

        // Split: nd1 (primeiras 2), nd2 (últimas 3)
        NoDados *nd1 = cria_no_dados();
        NoDados *nd2 = cria_no_dados();

        // Nd1 (primeiras 2 entradas)
        for (int j = 0; j < ORDEM / 2; j++) {
            inserir_entrada_em_no_dado(nd1, entradas_aux[j]);
        }

        // Nd2 (últimas 3 entradas)
        for (int j = ORDEM / 2; j < ORDEM; j++) {
            inserir_entrada_em_no_dado(nd2, entradas_aux[j]);
        }

        // Ajusta ponteiros da lista encadeada de folhas
        nd2->prox = nd->prox;
        nd1->prox = p_f_dados_original; // nd1 assume a posição original, nd2 vem depois
        // nd1 (que está na posição original) deve apontar para nd2

        // Salva nd1 na posição original
        nd1->ppai = nd->ppai;
        salva_no_dados(nd1, f_dados, p_f_dados_original);

        // Salva nd2 no fim do arquivo
        nd2->ppai = nd->ppai;
        salva_no_dados(nd2, f_dados, -1);
        int nd2_pos = (ftell(f_dados) / tamanho_no_dados()) - 1;

        // Atualiza o ponteiro 'prox' do nd1 para nd2
        nd1->prox = nd2_pos;
        salva_no_dados(nd1, f_dados, p_f_dados_original); // Salva novamente com o prox atualizado

        // Propaga a primeira chave de nd2 para o nó de índice pai
        inserir_em_arquivo_de_indice(nd2->s[0].nota, nd->ppai, 1, p_f_dados_original, nd2_pos, f_metadados, f_indice, f_dados);

        free(nd);
        free(nd1);
        free(nd2);
        free(md);
        free(info);
    }
}

/************************************************ FUNÇÕES DE ARQUIVO PRINCIPAL ************************************************/

typedef struct {
    char nome[100];
    FILE *f_metadados;
    FILE *f_indice;
    FILE *f_dados;
} ArvoreBmais;

ArvoreBmais arvores[5];
const char *nome_participantes_bin = "participantes.bin";

FILE *abrir_arquivo_participantes(const char *nome, HeaderParticipantes *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo de participantes");
            return NULL;
        }

        h->qtd_registros = 0;
        fwrite(h, tamanho_header(), 1, fp);
        fflush(fp);

    } else {
        fread(h, tamanho_header(), 1, fp);
    }

    return fp;
}

// Abre um arquivo (leitura/escrita, cria se não existir)
FILE *abrir_arquivo_bmais(const char *nome, long tamanho_struct) {
    FILE *f = fopen(nome, "r+b");
    if (f == NULL) {
        f = fopen(nome, "w+b");
        if (f == NULL) {
            perror("Erro ao criar arquivo B+ Tree");
            return NULL;
        }
        // Inicializa o arquivo com o metadados se for o arquivo de metadados
        if (tamanho_struct == tamanho_metadados()) {
            iniciar_arquivo_metadados(f);
        }
    }
    return f;
}

// Inicializa todas as 5 Árvores B+
void inicializar_arvores() {
    char *nomes[] = {"cn", "ch", "lc", "mt", "red"};
    long tamanhos_struct[] = {tamanho_metadados(), tamanho_no(), tamanho_no_dados()};

    for (int i = 0; i < 5; i++) {
        sprintf(arvores[i].nome, "nota_%s", nomes[i]);

        char nome_meta[120], nome_idx[120], nome_dados[120];
        sprintf(nome_meta, "%s_meta.dat", arvores[i].nome);
        sprintf(nome_idx, "%s_indice.dat", arvores[i].nome);
        sprintf(nome_dados, "%s_dados.dat", arvores[i].nome);

        arvores[i].f_metadados = abrir_arquivo_bmais(nome_meta, tamanho_metadados());
        arvores[i].f_indice = abrir_arquivo_bmais(nome_idx, tamanho_no());
        arvores[i].f_dados = abrir_arquivo_bmais(nome_dados, tamanho_no_dados());

        if (!arvores[i].f_metadados || !arvores[i].f_indice || !arvores[i].f_dados) {
            fprintf(stderr, "Erro ao inicializar as árvores B+.\n");
            // Ação de recuperação ou saída...
        }
    }
}

// Fecha todos os arquivos das Árvores B+
void fechar_arvores() {
    for (int i = 0; i < 5; i++) {
        if (arvores[i].f_metadados) fclose(arvores[i].f_metadados);
        if (arvores[i].f_indice) fclose(arvores[i].f_indice);
        if (arvores[i].f_dados) fclose(arvores[i].f_dados);
    }
}

int inserir_participante(FILE *fp_participantes, HeaderParticipantes *h, Participante *p) {

    // 1. Inserir no arquivo de dados principal (participantes.bin)
    int indice_registro = h->qtd_registros;

    long offset = tamanho_header() + indice_registro * tamanho_participante();
    fseek(fp_participantes, offset, SEEK_SET);
    fwrite(p, tamanho_participante(), 1, fp_participantes);

    h->qtd_registros++;
    fseek(fp_participantes, 0, SEEK_SET);
    fwrite(h, tamanho_header(), 1, fp_participantes);
    fflush(fp_participantes);

    // 2. Inserir a entrada (Nota + Índice) nas 5 Árvores B+

    // CN
    inserir_bmais(p->nota_cn, indice_registro, arvores[0].f_metadados, arvores[0].f_indice, arvores[0].f_dados);
    // CH
    inserir_bmais(p->nota_ch, indice_registro, arvores[1].f_metadados, arvores[1].f_indice, arvores[1].f_dados);
    // LC
    inserir_bmais(p->nota_lc, indice_registro, arvores[2].f_metadados, arvores[2].f_indice, arvores[2].f_dados);
    // MT
    inserir_bmais(p->nota_mt, indice_registro, arvores[3].f_metadados, arvores[3].f_indice, arvores[3].f_dados);
    // RED (Redação)
    inserir_bmais(p->nota_red, indice_registro, arvores[4].f_metadados, arvores[4].f_indice, arvores[4].f_dados);

    return indice_registro;
}


int importar_participantes_csv(char *nome_csv, const char *nome_bin) {
    HeaderParticipantes header;
    FILE *fp_bin = abrir_arquivo_participantes(nome_bin, &header);
    if (fp_bin == NULL) return 1;

    FILE *fp_csv = fopen(nome_csv, "r");
    if (fp_csv == NULL) {
        perror("Erro ao abrir CSV de participantes");
        fclose(fp_bin);
        return 1;
    }

    char linha[2048];

    // Ignora o cabeçalho
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
        perror("Erro ao abrir arquivo de participantes");
        return;
    }

    HeaderParticipantes h;
    fread(&h, tamanho_header(), 1, fp);

    printf("Total de registros: %d\n\n", h.qtd_registros);

    for (int i = 0; i < h.qtd_registros; i++) {
        Participante p;
        if (fread(&p, tamanho_participante(), 1, fp) != 1) {
             perror("Erro de leitura");
             break;
        }

        printf("Registro %d (Indice: %d):\n", i + 1, i);
        printf("  nu_seq: %s\n", p.nu_seq);
        printf("  ano: %d\n", p.ano);
        printf("  cod_esc: %s\n", p.cod_esc);
        printf("  Nota CN: %.2f\n", p.nota_cn);
        printf("  Nota CH: %.2f\n", p.nota_ch);
        printf("  Nota LC: %.2f\n", p.nota_lc);
        printf("  Nota MT: %.2f\n", p.nota_mt);
        printf("  Nota RED: %.2f\n", p.nota_red);
        printf("\n");
    }

    fclose(fp);
}

// Busca e lê um participante por índice
Participante *ler_participante_por_indice(FILE *fp_participantes, int indice) {
    Participante *p = (Participante *)malloc(tamanho_participante());
    if (!p) { perror("Erro de alocacao"); return NULL; }

    long offset = tamanho_header() + indice * tamanho_participante();
    if (fseek(fp_participantes, offset, SEEK_SET) != 0) {
        free(p);
        return NULL;
    }

    if (fread(p, tamanho_participante(), 1, fp_participantes) != 1) {
        free(p);
        return NULL;
    }

    return p;
}

void listar_ordenado(const char* tipo_nota) {
    int index = -1;
    if (strcmp(tipo_nota, "cn") == 0) index = 0;
    else if (strcmp(tipo_nota, "ch") == 0) index = 1;
    else if (strcmp(tipo_nota, "lc") == 0) index = 2;
    else if (strcmp(tipo_nota, "mt") == 0) index = 3;
    else if (strcmp(tipo_nota, "red") == 0) index = 4;

    if (index == -1) {
        printf("Tipo de nota '%s' nao reconhecido.\n", tipo_nota);
        return;
    }

    FILE *f_metadados = arvores[index].f_metadados;
    FILE *f_dados = arvores[index].f_dados;
    FILE *fp_participantes = fopen(nome_participantes_bin, "rb");

    if (!fp_participantes) {
        perror("Erro ao abrir arquivo de participantes para leitura");
        return;
    }

    Metadados *md = le_metadados(f_metadados);
    if (!md || md->pont_raiz == -1) {
        printf("A arvore de nota_%s esta vazia.\n", tipo_nota);
        free(md);
        fclose(fp_participantes);
        return;
    }

    // Encontrar o primeiro nó folha (o que tem a menor chave)
    int p_atual = md->pont_raiz;
    No *no_idx = NULL;
    while(md->flag_raiz_folha == 0) { // Se não for folha
        no_idx = buscar_no(p_atual, arvores[index].f_indice);
        if (no_idx->flag_aponta_folha) {
            p_atual = no_idx->p[0]; // Primeiro ponteiro aponta para a primeira folha
            free(no_idx);
            break;
        }
        p_atual = no_idx->p[0]; // Primeiro ponteiro aponta para o próximo nó de índice
        free(no_idx);
    }

    // Agora p_atual é a posição do primeiro nó de dados (folha)
    printf("------------------------------------------------------------------------\n");
    printf("Listando participantes ordenados pela NOTA %s (do menor para o maior):\n", tipo_nota);
    printf("------------------------------------------------------------------------\n");

    int contador = 0;
    while (p_atual != -1) {
        NoDados *nd = buscar_no_dados(p_atual, f_dados);

        for (int i = 0; i < nd->m; i++) {
            EntradaIndiceNota entrada = nd->s[i];
            Participante *p = ler_participante_por_indice(fp_participantes, entrada.indice_registro);

            if (p) {
                printf("%d. Nota %s: %.2f | Indice Registro: %d | Sequencial: %s\n",
                       ++contador, tipo_nota, entrada.nota, entrada.indice_registro, p->nu_seq);
                free(p);
            }
        }

        p_atual = nd->prox; // Próximo nó na lista encadeada
        free(nd);
    }

    free(md);
    fclose(fp_participantes);
    printf("------------------------------------------------------------------------\n");
}


void to_lowercase(char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        s[i] = tolower(s[i]);
    }
}

void limpar_arquivos_bmais() {
    char *nomes[] = {"cn", "ch", "lc", "mt", "red"};
    int i;
    for (i = 0; i < 5; i++) {
        char nome_base[100];
        sprintf(nome_base, "nota_%s", nomes[i]);

        char nome_meta[120], nome_idx[120], nome_dados[120];
        sprintf(nome_meta, "%s_meta.dat", nome_base);
        sprintf(nome_idx, "%s_indice.dat", nome_base);
        sprintf(nome_dados, "%s_dados.dat", nome_base);

        remove(nome_meta);
        remove(nome_idx);
        remove(nome_dados);
    }
    printf("Arquivos das 5 Árvores B+ (metadados, indice, dados) removidos.\n");
}

int main(void) {
    bool sair = false;
    char nome_csv[100];

    // 1. Inicializa as 5 Árvores B+ (abre/cria os 15 arquivos)
    inicializar_arvores();

    while(!sair) {
        char comando[COMMAND_MAX_SIZE];
        printf("\n\n------------------------------------------------------------------------\n");
        printf("Indique o que voce quer fazer:\n");
        printf("CLEAR - Limpa todo o banco de dados de registros e indices\n");
        printf("READ - Le um arquivo CSV com registros e insere nos arquivos binarios e indices B+\n");
        printf("SHOW - Mostra na tela os registros salvos em ordem de insercao\n");
        printf("LIST <NOTA> - Lista registros ordenados por nota. <NOTA> pode ser: cn, ch, lc, mt, red\n");
        printf("EXIT - Sai do programa\n");
        printf("------------------------------------------------------------------------\n");
        printf("> ");

        if (fgets(comando, COMMAND_MAX_SIZE, stdin) == NULL) continue;

        size_t len = strlen(comando);
        if (len > 0 && comando[len-1] == '\n') {
            comando[len-1] = '\0';
        }

        char comando_base[COMMAND_MAX_SIZE], arg[COMMAND_MAX_SIZE] = "";
        if (sscanf(comando, "%s %s", comando_base, arg) < 1) continue;

        to_lowercase(comando_base);

        if (strcmp(comando_base, "clear") == 0) {
            fechar_arvores();
            limpar_arquivos_bmais();
            if (remove(nome_participantes_bin) == 0) {
                printf("Arquivo de dados principal '%s' removido com sucesso.\n", nome_participantes_bin);
            } else {
                perror("Aviso: Nao foi possivel remover o arquivo de participantes.bin");
            }
            // Reabre as árvores vazias
            inicializar_arvores();
        } else if (strcmp(comando_base, "read") == 0) {
            printf("\nEscreva o nome do arquivo csv que voce quer ler (incluindo a extensao)\n");
            if (fgets(nome_csv, 100, stdin) == NULL) continue;
            size_t len2 = strlen(nome_csv);
            if (len2 > 0 && nome_csv[len2-1] == '\n') {
                nome_csv[len2-1] = '\0';
            }
            importar_participantes_csv(nome_csv, nome_participantes_bin);
        } else if (strcmp(comando_base, "show") == 0) {
            ler_todos_participantes(nome_participantes_bin);
        } else if (strcmp(comando_base, "list") == 0) {
            if (arg[0] != '\0') {
                to_lowercase(arg);
                listar_ordenado(arg);
            } else {
                printf("Comando LIST requer um argumento (ex: LIST cn, LIST red).\n");
            }
        } else if (strcmp(comando_base, "exit") == 0) {
            printf("\nSaindo do programa...\n");
            sair = true;
        } else {
            printf("\nComando '%s' nao reconhecido. Tente usar algum dos comandos citados.\n", comando_base);
        }
    }

    // 2. Fecha todos os arquivos antes de sair
    fechar_arvores();

    return 0;
}
