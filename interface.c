#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <math.h> // Para a função fabsf (float absolute value) e fseek/ftell

#define COMMAND_MAX_SIZE 100
// Ordem da Árvore B+ (512 para otimizar I/O em grandes volumes)
#define ORDEM 512
// O campo nu_seq tem 15 caracteres. O alfabeto é (0-9).
// O alfabeto tem 10 caracteres (0 a 9).
#define ALPHABET_SIZE 10
#define CHAVE_MAX_LENGTH 15
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))


/************************************************ ESTRUTURAS DE CONTROLE ************************************************/

// Nó da Lista Encadeada de Registros por Estado
typedef struct NoRegistro {
    int indice_registro; // Índice do Participante no participantes.bin
    int prox;            // Posição no arquivo do próximo NoRegistro (-1 se for o último)
} NoRegistro;

// Entrada da Tabela Hash Estática (no Cabeçalho)
// Guarda a posição inicial da lista encadeada no arquivo.
typedef struct {
    char estado[3]; // Sigla do Estado (Ex: "RS", "CE")
    int pont_lista; // Posição no arquivo do primeiro NoRegistro da lista (-1 se vazia)
} EntradaHashEstado;

// Cabeçalho do Arquivo de Registros Invertidos
#define QTD_ESTADOS 27 // 26 Estados + DF
typedef struct {
    EntradaHashEstado tabela_hash[QTD_ESTADOS];
    int qtd_nos; // Quantidade total de nós de lista encadeada (para cálculo de posição)
} HeaderRegistroEstado;

typedef struct {
    int qtd_registros;
} HeaderParticipantes;

// Estrutura para os dados de localização (tabela separada)
typedef struct {
    char cod_esc[15]; // Chave de busca (apenas para unicidade inicial)
    char cidade[40];
    char estado[20];
} Localizacao;

typedef struct {
    int qtd_registros;
} HeaderLocalizacao;

// Estrutura para os gabaritos das provas (tabela separada O(1))
// Cada registro armazena o código de UMA prova e seu gabarito.
typedef struct {
    char cod_prova[15]; // Chave de busca (Ex: código da prova de CN)
    char gabarito[60];
} Prova;

typedef struct {
    int qtd_registros;
} HeaderProva;

// Nó da Árvore Trie
typedef struct TrieNode {
    int filhos[ALPHABET_SIZE]; // Vetor de ponteiros (índices no arquivo) para os próximos nós (0-9)
    int is_fim_de_palavra;     // 1 se o nó representa o fim de um nu_seq
    int indice_registro;       // Índice do Participante (válido se is_fim_de_palavra == 1)
} TrieNode;

typedef struct {
    int pont_raiz; // Posição do nó raiz no arquivo (-1 se vazia)
    int qtd_nos;   // Contador de nós (para cálculo de posição)
} HeaderTrie;


// Estrutura simplificada para o foco na Árvore B+
typedef struct {
    char nu_seq[15];
    int ano;
    int indice_localizacao;
    int indice_gabarito_cn;
    int indice_gabarito_ch;
    int indice_gabarito_lc;
    int indice_gabarito_mt;
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

/************************************************ HELPERS DA ÁRVORE TRIE ************************************************/

// Tamanhos das novas estruturas
long tamanho_trie_node() { return sizeof(TrieNode); }
long tamanho_header_trie() { return sizeof(HeaderTrie); }

// Mapeia um caractere (0-9) para um índice (0-9)
int char_to_index(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return -1; // Caractere inválido
}

// Cria um nó da Trie vazio
TrieNode *cria_trie_node() {
    TrieNode *n = (TrieNode *)malloc(tamanho_trie_node());
    if (!n) { perror("Erro ao alocar TrieNode"); exit(1); }
    n->is_fim_de_palavra = 0;
    n->indice_registro = -1;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        n->filhos[i] = -1; // -1 significa NULL (ponteiro vazio)
    }
    return n;
}

// --- Funções de Manipulação do Arquivo da Trie ---

FILE *abrir_arquivo_trie(const char *nome, HeaderTrie *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo da Trie");
            return NULL;
        }

        // Inicializa o cabeçalho
        h->pont_raiz = -1;
        h->qtd_nos = 0;

        fwrite(h, tamanho_header_trie(), 1, fp);
        fflush(fp);

    } else {
        fread(h, tamanho_header_trie(), 1, fp);
    }

    return fp;
}

// Salva o cabeçalho
void salva_header_trie(FILE *fp, HeaderTrie *h) {
    fseek(fp, 0, SEEK_SET);
    fwrite(h, tamanho_header_trie(), 1, fp);
    fflush(fp);
}

// Busca um nó da Trie por índice no arquivo
TrieNode *buscar_trie_node(FILE *fp, int indice) {
    if (indice == -1) return NULL;

    TrieNode *node = cria_trie_node();
    long offset = tamanho_header_trie() + indice * tamanho_trie_node();

    if (fseek(fp, offset, SEEK_SET) != 0 || fread(node, tamanho_trie_node(), 1, fp) != 1) {
        free(node);
        return NULL;
    }
    return node;
}

// Salva/Atualiza um nó da Trie. Se pos == -1, insere no fim.
int salva_trie_node(FILE *fp, TrieNode *node, int pos) {
    long offset;
    int indice_salvo;

    if (pos == -1) { // Inserir no fim
        fseek(fp, 0, SEEK_END);
        offset = ftell(fp);
        indice_salvo = (offset - tamanho_header_trie()) / tamanho_trie_node();
    } else { // Atualizar existente
        indice_salvo = pos;
        offset = tamanho_header_trie() + pos * tamanho_trie_node();
        fseek(fp, offset, SEEK_SET);
    }

    fwrite(node, tamanho_trie_node(), 1, fp);
    fflush(fp);
    return indice_salvo;
}

// Insere uma nova chave (nu_seq) na Trie
void inserir_trie(FILE *fp_trie, HeaderTrie *h_trie, const char *nu_seq, int indice_registro) {
    int i;

    // 1. Inicialização: Se a Trie estiver vazia, cria a raiz
    if (h_trie->pont_raiz == -1) {
        TrieNode *raiz = cria_trie_node();
        int raiz_pos = salva_trie_node(fp_trie, raiz, -1);
        h_trie->pont_raiz = raiz_pos;
        h_trie->qtd_nos++;
        salva_header_trie(fp_trie, h_trie);
        free(raiz);
    }

    // 2. Traversal e Criação de Nós
    int p_atual = h_trie->pont_raiz;
    TrieNode *no_atual = NULL;

    for (i = 0; i < strlen(nu_seq); i++) {
        int index = char_to_index(nu_seq[i]);
        if (index == -1) {
            fprintf(stderr, "Aviso: Caractere invalido no nu_seq: %s\n", nu_seq);
            return;
        }

        no_atual = buscar_trie_node(fp_trie, p_atual);
        if (!no_atual) return; // Erro de leitura

        if (no_atual->filhos[index] == -1) {
            // Cria um novo nó, salva no fim, e atualiza o pai
            TrieNode *novo_no = cria_trie_node();
            int novo_pos = salva_trie_node(fp_trie, novo_no, -1);
            h_trie->qtd_nos++;

            no_atual->filhos[index] = novo_pos;
            salva_trie_node(fp_trie, no_atual, p_atual); // Atualiza o pai no disco
            p_atual = novo_pos;

            free(novo_no);
        } else {
            // Caminha para o próximo nó
            p_atual = no_atual->filhos[index];
        }

        free(no_atual); // Libera o nó lido/atualizado
    }

    // 3. Marcação de Fim de Palavra (Nó Final)
    no_atual = buscar_trie_node(fp_trie, p_atual);
    if (!no_atual) return;

    no_atual->is_fim_de_palavra = 1;
    no_atual->indice_registro = indice_registro; // Armazena o ponteiro de dados
    salva_trie_node(fp_trie, no_atual, p_atual); // Salva as alterações
    free(no_atual);

    salva_header_trie(fp_trie, h_trie); // Atualiza a contagem de nós no cabeçalho
}

// Busca a chave nu_seq na Trie e retorna o índice do Participante ou -1
int buscar_trie(FILE *fp_trie, HeaderTrie *h_trie, const char *nu_seq) {
    if (h_trie->pont_raiz == -1) return -1;

    int p_atual = h_trie->pont_raiz;
    TrieNode *no_atual;
    int i;

    for (i = 0; i < strlen(nu_seq); i++) {
        int index = char_to_index(nu_seq[i]);
        if (index == -1) return -1;

        no_atual = buscar_trie_node(fp_trie, p_atual);
        if (!no_atual) return -1; // Erro de leitura

        int p_proximo = no_atual->filhos[index];
        free(no_atual);

        if (p_proximo == -1) {
            return -1; // Caminho não existe
        }
        p_atual = p_proximo;
    }

    // Chegou ao fim do nu_seq. Checa se é um nó final de palavra.
    no_atual = buscar_trie_node(fp_trie, p_atual);
    if (!no_atual) return -1;

    int indice = -1;
    if (no_atual->is_fim_de_palavra) {
        indice = no_atual->indice_registro;
    }

    free(no_atual);
    return indice;
}

/************************************************ FUNÇÕES PRINCIPAIS DA HASH TABLE ************************************************/

long tamanho_no_registro() { return sizeof(NoRegistro); }
long tamanho_header_registro_estado() { return sizeof(HeaderRegistroEstado); }

// Constantes para os estados (para inicialização e hashing)
const char *SIGLAS_ESTADOS[QTD_ESTADOS] = {
    "AC", "AL", "AP", "AM", "BA", "CE", "DF", "ES", "GO", "MA",
    "MT", "MS", "MG", "PA", "PB", "PR", "PE", "PI", "RJ", "RN",
    "RS", "RO", "RR", "SC", "SP", "SE", "TO"
};

// Função de Hash simples (Índice no array SIGLAS_ESTADOS)
int funcao_hash_estado(const char *estado) {
    for (int i = 0; i < QTD_ESTADOS; i++) {
        if (strcmp(estado, SIGLAS_ESTADOS[i]) == 0) {
            return i;
        }
    }
    return -1; // Estado não encontrado (deve ser tratado na importação)
}

// --- FUNÇÕES DE MANIPULAÇÃO DO ARQUIVO INVERTIDO ---

FILE *abrir_arquivo_registro_estado(const char *nome, HeaderRegistroEstado *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo de registro por estado");
            return NULL;
        }

        // Inicializa o cabeçalho
        h->qtd_nos = 0;
        for (int i = 0; i < QTD_ESTADOS; i++) {
            strcpy(h->tabela_hash[i].estado, SIGLAS_ESTADOS[i]);
            h->tabela_hash[i].pont_lista = -1; // Lista vazia
        }

        fwrite(h, tamanho_header_registro_estado(), 1, fp);
        fflush(fp);

    } else {
        fread(h, tamanho_header_registro_estado(), 1, fp);
    }

    return fp;
}

// Função principal: Insere um novo índice de registro na lista encadeada do Estado
void inserir_indice_no_registro_estado(FILE *fp_reg_est, HeaderRegistroEstado *h_reg_est, const char *estado, int indice_participante) {

    int hash_index = funcao_hash_estado(estado);

    if (hash_index == -1) {
        fprintf(stderr, "ERRO: Sigla de estado '%s' nao reconhecida e nao inserida no indice invertido.\n", estado);
        return;
    }

    // 1. Cria o novo nó
    NoRegistro novo_no;
    novo_no.indice_registro = indice_participante;

    // 2. Obtém a posição da cabeça da lista atual para o estado
    int p_lista_atual = h_reg_est->tabela_hash[hash_index].pont_lista;

    // O novo nó é inserido SEMPRE no INÍCIO da lista (O(1))
    novo_no.prox = p_lista_atual;

    // 3. Salva o novo nó no FINAL do arquivo (O(1) para escrita em 'append')
    fseek(fp_reg_est, 0, SEEK_END);
    fwrite(&novo_no, tamanho_no_registro(), 1, fp_reg_est);
    int p_novo_no = (ftell(fp_reg_est) / tamanho_no_registro()) - 1;

    // A posição real do nó deve considerar o cabeçalho.
    // Vamos simplificar o cálculo de posição, assumindo que os nós começam após o header.
    // Posição no arquivo: header_size + p_novo_no * tamanho_no_registro()
    // No entanto, para fins de ponteiros internos, usaremos a indexação a partir do primeiro nó (posição 0).
    // O ponteiro `pont_lista` e `prox` armazenarão o ÍNDICE do nó (0, 1, 2...).

    // O cálculo da posição relativa é mais robusto:
    // Posição no arquivo: tamanho_header_registro_estado() + indice_no * tamanho_no_registro()

    // Calcula o índice real do novo nó
    int indice_novo_no = h_reg_est->qtd_nos;
    h_reg_est->qtd_nos++;

    // Reposiciona o novo nó no arquivo (offset: header + indice * size)
    long offset_novo_no = tamanho_header_registro_estado() + indice_novo_no * tamanho_no_registro();
    fseek(fp_reg_est, offset_novo_no, SEEK_SET);
    fwrite(&novo_no, tamanho_no_registro(), 1, fp_reg_est);

    // 4. Atualiza o Cabeçalho: A cabeça da lista agora é o novo nó
    h_reg_est->tabela_hash[hash_index].pont_lista = indice_novo_no;

    // 5. Salva o Cabeçalho atualizado
    fseek(fp_reg_est, 0, SEEK_SET);
    fwrite(h_reg_est, tamanho_header_registro_estado(), 1, fp_reg_est);
    fflush(fp_reg_est);
}

// Busca e retorna um nó da lista encadeada por índice
NoRegistro *buscar_no_registro(FILE *fp_reg_est, int indice) {
    if (indice < 0) return NULL;

    NoRegistro *no = (NoRegistro *)malloc(tamanho_no_registro());
    if (!no) { perror("Erro de alocacao NoRegistro"); return NULL; }

    long offset = tamanho_header_registro_estado() + indice * tamanho_no_registro();
    if (fseek(fp_reg_est, offset, SEEK_SET) != 0) {
        free(no);
        return NULL;
    }

    if (fread(no, tamanho_no_registro(), 1, fp_reg_est) != 1) {
        free(no);
        return NULL;
    }

    return no;
}

/************************************************ ESTRUTURAS DA ÁRVORE B+ ADAPTADA ************************************************/

// Entrada de dados no nó folha (chave é a nota, valor é o índice do registro no arquivo binário)
typedef struct {
    float nota; // Chave (key) da B+ Tree: a nota do participante
    int indice_registro; // Ponteiro para o registro completo no "participantes.bin"
} EntradaIndiceNota;

// Nó de Dados (Folha)
typedef struct {
    int ppai; // Posição no arquivo de índice do nó pai
    int m; // Quantidade de entradas (máx ORDEM-1)
    EntradaIndiceNota s[ORDEM - 1]; // Chaves/dados
    int prox; // Ponteiro para a próxima folha (Simplesmente encadeada)
    int ant;  // Ponteiro para a folha anterior (Duplamente encadeada)
} NoDados;

// Nó de Índice (Não-Folha)
typedef struct No {
    int ppai; // Posição no arquivo de índice do nó pai
    int m; // Quantidade de chaves (máx ORDEM-1)
    float s[ORDEM - 1]; // Chaves (keys), agora floats
    int p[ORDEM]; // Ponteiros para filhos (posições no arquivo de índice ou dados)
    int flag_aponta_folha; // 1 se aponta para NoDados, 0 se aponta para No
} No;

// Estrutura de Metadados
typedef struct {
    int pont_raiz; // Posição do nó raiz no arquivo de índice/dados
    int flag_raiz_folha; // 1 se a raiz é folha (NoDados), 0 se é nó de índice (No)
    int pont_primeira_folha; // Posição da primeira folha
    int pont_ultima_folha; // Posição da última folha
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
long tamanho_localizacao() { return sizeof(Localizacao); }
long tamanho_header_localizacao() { return sizeof(HeaderLocalizacao); }
long tamanho_prova() { return sizeof(Prova); }
long tamanho_header_prova() { return sizeof(HeaderProva); }
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
    nd->ant = -1; // Inicializa o ponteiro anterior
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
    Metadados md = { .pont_raiz = -1, .flag_raiz_folha = 1, .pont_primeira_folha = -1, .pont_ultima_folha = -1 };
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
        no->p[i + 2] = no->p[i + 1]; // Desloca ponteiro a direita
        i--;
    }
    no->s[i + 1] = chave;

    // Ajusta o ponteiro esquerdo/anterior
    if (p_esq != -1) {
        no->p[i + 1] = p_esq;
    }
    // Ajusta o ponteiro direito/posterior
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
    info->encontrou = 0;

    Metadados *md = le_metadados(f_metadados);
    int p_atual;

    if (md->pont_raiz == -1) {
        free(md);
        return info; // Árvore vazia
    }

    if (md->flag_raiz_folha == 1) {
        p_atual = md->pont_raiz;
        info->p_f_dados = p_atual;
        info->p_f_indice = -1;
        free(md);
    } else {
        p_atual = md->pont_raiz;
        info->p_f_indice = p_atual;
        free(md);

        while (p_atual != -1) {
            No *pag = buscar_no(p_atual, f_indice);
            if (!pag) { p_atual = -1; break; }

            info->p_f_indice = p_atual;

            int proximo_p = -1;
            int i;
            for (i = 0; i < pag->m; i++) {
                if (x < pag->s[i]) {
                    proximo_p = pag->p[i];
                    break;
                }
            }
            if (proximo_p == -1) {
                proximo_p = pag->p[pag->m];
            }

            if (pag->flag_aponta_folha) {
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
        if (!pag_dados) return info;

        int i;
        for (i = 0; i < pag_dados->m; i++) {
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
        info->pos_vetor_dados = pag_dados->m;
        info->encontrou = 0;
        free(pag_dados);
        return info;
    }

    return info;
}

// Insere chave no arquivo de índice e dá um pai para os nós esquerdo e direito (Propagação de Split)
void inserir_em_arquivo_de_indice(float chave, int p_pai_original, int flag_aponta_folha, int p_filho_esq, int p_filho_dir, FILE *f_metadados, FILE *f_indice, FILE *f_dados) {

    Metadados *md = le_metadados(f_metadados);

    if (p_pai_original == -1) { // Criação de uma nova raiz (apenas se for o primeiro split)
        No *nova_raiz = cria_no();
        inserir_chave_em_no(nova_raiz, chave, p_filho_esq, p_filho_dir);
        nova_raiz->flag_aponta_folha = flag_aponta_folha;

        salva_no(nova_raiz, f_indice, -1);
        int nova_raiz_pos = (ftell(f_indice) / tamanho_no()) - 1;

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
        inserir_chave_em_no(no_pai, chave, p_filho_esq, p_filho_dir);
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

    } else { // Nó de índice cheio (ORDEM - 1 chaves) -> Split

        // 1. Cria arrays auxiliares para ORDEM chaves e ORDEM+1 ponteiros
        float chaves_aux[ORDEM];
        int ponteiros_aux[ORDEM + 1];

        for(int i = 0; i < ORDEM - 1; i++){
            chaves_aux[i] = no_pai->s[i];
            ponteiros_aux[i] = no_pai->p[i];
        }
        ponteiros_aux[ORDEM - 1] = no_pai->p[ORDEM - 1];

        // 2. Insere a nova chave e ponteiro (p_filho_dir) no local correto, deslocando os demais
        int i = ORDEM - 2;
        while (i >= 0 && chaves_aux[i] > chave) {
            chaves_aux[i + 1] = chaves_aux[i];
            ponteiros_aux[i + 2] = ponteiros_aux[i + 1];
            i--;
        }
        chaves_aux[i + 1] = chave;
        ponteiros_aux[i + 2] = p_filho_dir;

        int j = i + 1;
        while(j > 0 && ponteiros_aux[j] != p_filho_esq){
            j--;
        }
        if(ponteiros_aux[j] != p_filho_esq){
            ponteiros_aux[0] = p_filho_esq;
        } else {
            ponteiros_aux[j] = p_filho_esq;
        }


        // 3. Define índices e chave para subir
        int chaves_por_no = (ORDEM - 1) / 2;
        int indice_chave_subir = chaves_por_no;
        float chave_subir = chaves_aux[indice_chave_subir];
        int indice_n2_inicio = indice_chave_subir + 1;
        int p_pai_do_pai = no_pai->ppai;

        // 4. Cria os novos nós (n1 e n2)
        No *n1 = cria_no(); n1->ppai = p_pai_do_pai; n1->flag_aponta_folha = no_pai->flag_aponta_folha;
        No *n2 = cria_no(); n2->ppai = p_pai_do_pai; n2->flag_aponta_folha = no_pai->flag_aponta_folha;

        // 5. Preenche n1 (nó esquerdo)
        n1->m = chaves_por_no;
        for (int j = 0; j < n1->m; j++) {
            n1->s[j] = chaves_aux[j];
            n1->p[j] = ponteiros_aux[j];
        }
        n1->p[n1->m] = ponteiros_aux[n1->m];

        // 6. Preenche n2 (nó direito)
        n2->m = (ORDEM - 1) - indice_chave_subir;
        for (int j = 0; j < n2->m; j++) {
            n2->s[j] = chaves_aux[indice_n2_inicio + j];
            n2->p[j] = ponteiros_aux[indice_n2_inicio + j];
        }
        n2->p[n2->m] = ponteiros_aux[ORDEM];

        // 7. Salva n1 na posição original e n2 no fim
        salva_no(n1, f_indice, p_pai_original);
        salva_no(n2, f_indice, -1);
        int n2_pos = (ftell(f_indice) / tamanho_no()) - 1;

        // 8. Atualiza os pais dos filhos do n2
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

        // 9. Propaga a chave subida para o pai
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
    int p_ultima_folha = md->pont_ultima_folha; // Guarda a posição da última folha antes da inserção/split

    if (md->pont_raiz == -1) { // Árvore vazia
        NoDados *nd = cria_no_dados();
        nd = inserir_entrada_em_no_dado(nd, nova_entrada);

        salva_no_dados(nd, f_dados, 0);
        int nd_pos = 0;

        md->pont_raiz = nd_pos;
        md->flag_raiz_folha = 1;
        md->pont_primeira_folha = nd_pos; // Primeira folha
        md->pont_ultima_folha = nd_pos;   // Última folha
        salva_metadados(md, f_metadados);

        free(nd);
        free(md);
        return;
    }

    Info *info = busca(nota, f_metadados, f_indice, f_dados);

    if (info->encontrou == 1) {
        //printf("Aviso: Chave (nota %.2f) já existe na arvore. Inserindo mesmo assim, mas B+ Tree original evita duplicatas.\n", nota);
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

        EntradaIndiceNota entradas_aux[ORDEM];
        for (int i = 0; i < ORDEM - 1; i++) {
            entradas_aux[i] = nd->s[i];
        }

        int i = ORDEM - 1;
        while (i > 0 && entradas_aux[i - 1].nota > nova_entrada.nota) {
            entradas_aux[i] = entradas_aux[i - 1];
            i--;
        }
        entradas_aux[i] = nova_entrada;

        int split_index = ORDEM / 2;

        NoDados *nd1 = cria_no_dados();
        NoDados *nd2 = cria_no_dados();

        // 1. Preenche nd1 (Nó esquerdo, que ocupa a posição original)
        for (int j = 0; j < split_index; j++) {
            inserir_entrada_em_no_dado(nd1, entradas_aux[j]);
        }

        // 2. Preenche nd2 (Nó direito, que é um novo nó)
        for (int j = split_index; j < ORDEM; j++) {
            inserir_entrada_em_no_dado(nd2, entradas_aux[j]);
        }

        // 3. Ajusta o encadeamento DUPLO da lista de folhas

        int p_proximo_original = nd->prox;

        nd2->prox = p_proximo_original; // nd2 aponta para quem vinha depois do nd original
        nd2->ant = p_f_dados_original;  // nd2 aponta para nd1 (posição original)

        nd1->prox = -1; // Sera atualizado após nd2 ser salvo
        nd1->ant = nd->ant; // nd1 herda o ponteiro anterior do nd original

        // 4. Salva nd1 na posição original
        nd1->ppai = nd->ppai;
        salva_no_dados(nd1, f_dados, p_f_dados_original);

        // 5. Salva nd2 no fim do arquivo
        nd2->ppai = nd->ppai;
        salva_no_dados(nd2, f_dados, -1);
        int nd2_pos = (ftell(f_dados) / tamanho_no_dados()) - 1;

        // 6. Finaliza encadeamento: Atualiza nd1->prox e o nó que vem depois (p_proximo_original)

        // Atualiza nd1->prox para nd2
        nd1->prox = nd2_pos;
        salva_no_dados(nd1, f_dados, p_f_dados_original);

        // Atualiza o ponteiro 'ant' do nó que vem depois (se ele existir)
        if (p_proximo_original != -1) {
            NoDados *nd_proximo = buscar_no_dados(p_proximo_original, f_dados);
            if (nd_proximo) {
                nd_proximo->ant = nd2_pos;
                salva_no_dados(nd_proximo, f_dados, p_proximo_original);
                free(nd_proximo);
            }
        }

        // 7. Atualiza o ponteiro da ÚLTIMA folha nos metadados, se nd2 se tornou a nova última folha
        if (p_f_dados_original == p_ultima_folha) {
             Metadados *md_temp = le_metadados(f_metadados);
             md_temp->pont_ultima_folha = nd2_pos;
             salva_metadados(md_temp, f_metadados);
             free(md_temp);
        }


        // 8. Propaga a primeira chave de nd2 para o nó de índice pai
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
const char *nome_localizacao_bin = "localizacao.bin";
const char *nome_registro_estado_bin = "reg_por_estado.bin";
const char *nome_gabarito_bin = "gabarito_provas.bin";
const char *nome_trie_bin = "trie_nuseq.bin";
int REGPORPAG = 5;


void to_lowercase(char *s) {
    for (size_t i = 0; s[i] != '\0'; i++) {
        s[i] = tolower(s[i]);
    }
}

// --- FUNÇÕES DE MANIPULAÇÃO DO ARQUIVO DE LOCALIZAÇÃO (O(1)) ---

FILE *abrir_arquivo_localizacao(const char *nome, HeaderLocalizacao *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo de localizacao");
            return NULL;
        }

        h->qtd_registros = 0;
        fwrite(h, tamanho_header_localizacao(), 1, fp);
        fflush(fp);

    } else {
        fread(h, tamanho_header_localizacao(), 1, fp);
    }

    return fp;
}

// Salva e retorna o índice onde a Localizacao foi salva.
int salvar_localizacao(FILE *fp_loc, HeaderLocalizacao *h_loc, Localizacao *loc) {
    int indice_registro = h_loc->qtd_registros;
    long offset = tamanho_header_localizacao() + indice_registro * tamanho_localizacao();
    fseek(fp_loc, offset, SEEK_SET);
    fwrite(loc, tamanho_localizacao(), 1, fp_loc);

    h_loc->qtd_registros++;
    fseek(fp_loc, 0, SEEK_SET);
    fwrite(h_loc, tamanho_header_localizacao(), 1, fp_loc);
    fflush(fp_loc);

    return indice_registro;
}

// Busca de Localização por índice (O(1))
Localizacao *buscar_localizacao_por_indice(FILE *fp_loc, int indice) {
    Localizacao *loc = (Localizacao *)malloc(tamanho_localizacao());
    if (!loc) { perror("Erro de alocacao Localizacao"); return NULL; }

    long offset = tamanho_header_localizacao() + indice * tamanho_localizacao();
    if (fseek(fp_loc, offset, SEEK_SET) != 0) {
        free(loc);
        return NULL;
    }

    if (fread(loc, tamanho_localizacao(), 1, fp_loc) != 1) {
        free(loc);
        return NULL;
    }

    return loc;
}

// Busca Localização por código (Lento: O(N)) - Usado apenas durante a importação para garantir unicidade
int buscar_indice_localizacao_por_cod_esc(FILE *fp_loc, HeaderLocalizacao *h_loc, const char *cod_esc) {
    Localizacao temp_loc;

    // Inicia a busca após o header
    fseek(fp_loc, tamanho_header_localizacao(), SEEK_SET);

    for (int i = 0; i < h_loc->qtd_registros; i++) {
        if (fread(&temp_loc, tamanho_localizacao(), 1, fp_loc) != 1) {
            return -1; // Erro
        }
        if (strcmp(temp_loc.cod_esc, cod_esc) == 0) {
            return i; // Encontrou o índice
        }
    }

    return -1; // Não encontrou
}

// --- NOVAS FUNÇÕES DE MANIPULAÇÃO DO ARQUIVO DE GABARITO (O(1)) ---

FILE *abrir_arquivo_gabarito(const char *nome, HeaderProva *h) {
    FILE *fp = fopen(nome, "rb+");

    if (fp == NULL) {
        fp = fopen(nome, "wb+");
        if (fp == NULL) {
            perror("Erro ao criar arquivo de gabarito");
            return NULL;
        }

        h->qtd_registros = 0;
        fwrite(h, tamanho_header_prova(), 1, fp);
        fflush(fp);

    } else {
        fread(h, tamanho_header_prova(), 1, fp);
    }

    return fp;
}

// Salva e retorna o índice onde a Prova foi salva.
int salvar_gabarito(FILE *fp_gab, HeaderProva *h_gab, Prova *prova) {
    int indice_registro = h_gab->qtd_registros;
    long offset = tamanho_header_prova() + indice_registro * tamanho_prova();
    fseek(fp_gab, offset, SEEK_SET);
    fwrite(prova, tamanho_prova(), 1, fp_gab);

    h_gab->qtd_registros++;
    fseek(fp_gab, 0, SEEK_SET);
    fwrite(h_gab, tamanho_header_prova(), 1, fp_gab);
    fflush(fp_gab);

    return indice_registro;
}

// Busca de Gabarito por índice (O(1))
Prova *buscar_gabarito_por_indice(FILE *fp_gab, int indice) {
    Prova *prova = (Prova *)malloc(tamanho_prova());
    if (!prova) { perror("Erro de alocacao Prova"); return NULL; }

    long offset = tamanho_header_prova() + indice * tamanho_prova();
    if (fseek(fp_gab, offset, SEEK_SET) != 0) {
        free(prova);
        return NULL;
    }

    if (fread(prova, tamanho_prova(), 1, fp_gab) != 1) {
        free(prova);
        return NULL;
    }

    return prova;
}

// Busca Gabarito por código de prova (Lento: O(N)) - Usado apenas durante a importação para garantir unicidade
int buscar_indice_gabarito_por_cod_prova(FILE *fp_gab, HeaderProva *h_gab, const char *cod_prova) {
    Prova temp_prova;

    // Inicia a busca após o header
    fseek(fp_gab, tamanho_header_prova(), SEEK_SET);

    for (int i = 0; i < h_gab->qtd_registros; i++) {
        if (fread(&temp_prova, tamanho_prova(), 1, fp_gab) != 1) {
            return -1; // Erro
        }
        // Comparação usando o cod_prova
        if (strcmp(temp_prova.cod_prova, cod_prova) == 0) {
            return i; // Encontrou o índice
        }
    }

    return -1; // Não encontrou
}

// FIM DAS FUNÇÕES DE MANIPULAÇÃO DO ARQUIVO DE GABARITO

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

// Abre um arquivo B+ (leitura/escrita, cria se não existir)
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

    HeaderLocalizacao header_loc;
    FILE *fp_loc = abrir_arquivo_localizacao(nome_localizacao_bin, &header_loc);
    if (fp_loc == NULL) {
        fclose(fp_bin);
        return 1;
    }

    HeaderProva header_gab; // Cabeçalho do Gabarito
    FILE *fp_gab = abrir_arquivo_gabarito(nome_gabarito_bin, &header_gab); // Arquivo de Gabarito
    if (fp_gab == NULL) {
        fclose(fp_loc);
        fclose(fp_bin);
        return 1;
    }
    //Registro por Estado (Invertido)
    HeaderRegistroEstado header_reg_est;
    FILE *fp_reg_est = abrir_arquivo_registro_estado(nome_registro_estado_bin, &header_reg_est);
    if (fp_reg_est == NULL) {
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_bin);
        return 1;
    }

    //Trie para nu_seq
    HeaderTrie header_trie;
    FILE *fp_trie = abrir_arquivo_trie(nome_trie_bin, &header_trie);
    if (fp_trie == NULL) {
        fclose(fp_reg_est);
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_bin);
        return 1;
    }

    FILE *fp_csv = fopen(nome_csv, "r");
    if (fp_csv == NULL) {
        perror("Erro ao abrir CSV de participantes");
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_bin);
        return 1;
    }

    char linha[4096];

    // Ignora o cabeçalho
    if (fgets(linha, sizeof(linha), fp_csv) == NULL) {
        printf("CSV vazio.\n");
        fclose(fp_csv);
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_bin);
        return 1;
    }

    int linhas_lidas = 0;

    // Variáveis temporárias lidas do CSV
    char temp_cod_esc[15];
    char temp_cidade[40];
    char temp_estado[20];

    // VARIÁVEIS TEMPORÁRIAS PARA OS 4 CÓDIGOS E OS 4 GABARITOS
    float temp_cod_prova_f[4];
    char temp_gab_str[4][60];

    // --- CORREÇÃO FINAL DO SCANF E ORDEM DOS ARGUMENTOS ---
    // A ordem dos especificadores no `csv_format_final` deve coincidir *exatamente* com a ordem das colunas no CSV.
    // E a ordem dos argumentos no `sscanf` deve coincidir *exatamente* com a ordem dos especificadores.

    const char *csv_format_final =

        "%14[^;];%d;"               // 1-2: NU_SEQ, ANO
        "%14[^;];"                  // 3: COD_ESC (temp)
        "%*[^;];"                   // 4: Skip (CO_MUNICIPIO_RESIDENCIA)
        "%39[^;];"                  // 5: CIDADE (temp)
        "%*[^;];"                   // 6: Skip (CO_UF_RESIDENCIA)
        "%19[^;];"                  // 7: ESTADO (temp)
        "%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];" // Skips
        "%f;%f;%f;%f;"              // 9-12: COD_PROVA CN, CH, LC, MT (temp_cod_prova_f[0]...)
        "%f;%f;%f;%f;"              // 17-20: NOTAS CN, CH, LC, MT (p.nota_cn, ...)
        "%49[^;];%49[^;];%49[^;];%49[^;];" // 21-24: RESPOSTAS CN, CH, LC, MT (p.resp_cn, ...)
        "%d;"                       // 25: LING_EST (p.ling_est)
        "%59[^;];%59[^;];%59[^;];%59[^;];" // 13-16: GABARITOS CN, CH, LC, MT (temp_gab_str[0]...)
        "%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];%*[^;];" // Skips (36-41)
        "%f";                       // 42: NOTA_RED (p.nota_red)

    while (fgets(linha, sizeof(linha), fp_csv) != NULL) {
        Participante p;
        memset(&p, 0, tamanho_participante());
        memset(temp_cod_esc, 0, sizeof(temp_cod_esc));
        memset(temp_cidade, 0, sizeof(temp_cidade));
        memset(temp_estado, 0, sizeof(temp_estado));

        // 23 argumentos a serem lidos: 3 loc strings, 4 code floats, 4 gab strings, 4 nota floats, 4 resp strings, 1 ling int, 1 red float
        int ok = sscanf(
            linha,
            csv_format_final,
            p.nu_seq,
            &p.ano,
            temp_cod_esc, // 3: COD_ESC
            temp_cidade,  // 5: CIDADE
            temp_estado,  // 7: ESTADO
            &temp_cod_prova_f[0], // 9: COD_PROVA CN (temp)
            &temp_cod_prova_f[1], // 10: COD_PROVA CH (temp)
            &temp_cod_prova_f[2], // 11: COD_PROVA LC (temp)
            &temp_cod_prova_f[3], // 12: COD_PROVA MT (temp)
            &p.nota_cn, // 17: NOTA CN
            &p.nota_ch, // 18: NOTA CH
            &p.nota_lc, // 19: NOTA LC
            &p.nota_mt, // 20: NOTA MT
            p.resp_cn, // 21: RESPOSTA CN
            p.resp_ch, // 22: RESPOSTA CH
            p.resp_lc, // 23: RESPOSTA LC
            p.resp_mt, // 24: RESPOSTA MT
            &p.ling_est, // 25: LING_EST
            temp_gab_str[0], // 13: GABARITO CN (temp)
            temp_gab_str[1], // 14: GABARITO CH (temp)
            temp_gab_str[2], // 15: GABARITO LC (temp)
            temp_gab_str[3], // 16: GABARITO MT (temp)
            &p.nota_red // 42: NOTA_RED
        );

        // O número esperado de argumentos lidos é 23
        if (ok != 23) {
             //printf("Linha ignorada (formato inesperado). Lidos = %d. Esperado = 23\n", ok);
             continue;
        }

        // --- 1. PROCESSAMENTO DE LOCALIZAÇÃO (O(1)) ---

        int indice_loc = buscar_indice_localizacao_por_cod_esc(fp_loc, &header_loc, temp_cod_esc);

        if (indice_loc == -1) {
            Localizacao nova_loc;
            strcpy(nova_loc.cod_esc, temp_cod_esc);
            strcpy(nova_loc.cidade, temp_cidade);
            strcpy(nova_loc.estado, temp_estado);
            indice_loc = salvar_localizacao(fp_loc, &header_loc, &nova_loc);
        }

        p.indice_localizacao = indice_loc;

        // --- 2. PROCESSAMENTO DE GABARITO (O(1)) ---

        // Ponteiros para os campos de índice na struct Participante
        int *indices_gab_ptr[] = {
            &p.indice_gabarito_cn,
            &p.indice_gabarito_ch,
            &p.indice_gabarito_lc,
            &p.indice_gabarito_mt
        };

        for (int i = 0; i < 4; i++) {
            char cod_prova_str[15];
            sprintf(cod_prova_str, "%.0f", temp_cod_prova_f[i]);

            // Busca o índice do gabarito para este código de prova
            int indice_gab = buscar_indice_gabarito_por_cod_prova(fp_gab, &header_gab, cod_prova_str);

            if (indice_gab == -1) {
                // Se NOVO: Salva o novo registro (código de prova individual + gabarito)
                Prova nova_gab;
                strcpy(nova_gab.cod_prova, cod_prova_str);
                strcpy(nova_gab.gabarito, temp_gab_str[i]);
                indice_gab = salvar_gabarito(fp_gab, &header_gab, &nova_gab);
            }

            // Armazena o índice no campo específico do Participante
            *indices_gab_ptr[i] = indice_gab;
        }

        // --- 3. INSERIR PARTICIPANTE E ÍNDICES B+ ---
        //inserir_participante(fp_bin, &header, &p);
        int indice_registro = inserir_participante(fp_bin, &header, &p);

        // --- 4. INSERIR NO ARQUIVO INVERTIDO DE ESTADO
        // Nota: O `temp_estado` é a sigla lida do CSV (Ex: "RS")
        inserir_indice_no_registro_estado(fp_reg_est, &header_reg_est, temp_estado, indice_registro);

        // --- 5. INSERIR NA ÁRVORE TRIE
        inserir_trie(fp_trie, &header_trie, p.nu_seq, indice_registro);

        linhas_lidas++;
    }

    printf("Importacao concluida.\n");
    printf("Linhas validas inseridas (Participantes): %d\n", linhas_lidas);
    printf("Total de registros unicos de Localizacao: %d\n", header_loc.qtd_registros);
    printf("Total de registros unicos de Gabarito de Provas: %d\n", header_gab.qtd_registros);
    printf("Total de nós do índice invertido por Estado: %d\n", header_reg_est.qtd_nos);
    printf("Total de nós na Arvore Trie: %d\n", header_trie.qtd_nos); // Nova saída

    fclose(fp_csv);
    fclose(fp_gab);
    fclose(fp_loc);
    fclose(fp_reg_est);
    fclose(fp_trie);
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

    // Abertura dos arquivos de metadados O(1)
    HeaderLocalizacao header_loc;
    FILE *fp_loc = abrir_arquivo_localizacao(nome_localizacao_bin, &header_loc);

    HeaderProva header_gab;
    FILE *fp_gab = abrir_arquivo_gabarito(nome_gabarito_bin, &header_gab);

    if (fp_loc == NULL || fp_gab == NULL) {
        fclose(fp);
        if (fp_loc) fclose(fp_loc);
        if (fp_gab) fclose(fp_gab);
        return;
    }


    printf("Total de registros: %d\n\n", h.qtd_registros);

    for (int i = 0; i < h.qtd_registros; i++) {
        Participante p;
        if (fread(&p, tamanho_participante(), 1, fp) != 1) {
             perror("Erro de leitura");
             break;
        }

        // Busca O(1) e exibe a Localização
        Localizacao *loc = buscar_localizacao_por_indice(fp_loc, p.indice_localizacao);
        char cidade_estado[65] = "Nao Encontrada";
        char cod_esc_temp[15] = "N/A";

        if (loc) {
            sprintf(cidade_estado, "%s/%s", loc->cidade, loc->estado);
            strcpy(cod_esc_temp, loc->cod_esc);
            free(loc);
        }

        // Busca O(1) e exibe os Gabaritos
        char gab_cn[55] = "N/A", gab_ch[55] = "N/A";
        char cod_cn[15] = "N/A", cod_ch[15] = "N/A";

        // Busca Gabarito CN
        Prova *p_cn = buscar_gabarito_por_indice(fp_gab, p.indice_gabarito_cn);
        if (p_cn) {
            strcpy(gab_cn, p_cn->gabarito);
            strcpy(cod_cn, p_cn->cod_prova);
            free(p_cn);
        }

        // Busca Gabarito CH
        Prova *p_ch = buscar_gabarito_por_indice(fp_gab, p.indice_gabarito_ch);
        if (p_ch) {
            strcpy(gab_ch, p_ch->gabarito);
            strcpy(cod_ch, p_ch->cod_prova);
            free(p_ch);
        }

        printf("Registro %d (Indice: %d):\n", i + 1, i);
        printf("  nu_seq: %s\n", p.nu_seq);
        printf("  ano: %d\n", p.ano);
        printf("  cod_esc: %s (Localizacao: %s)\n", cod_esc_temp, cidade_estado);
        // Exibindo o código da prova recuperado da tabela de gabaritos (O(1))
        printf("  Provas (CN/CH): %s / %s | Gabaritos (CN/CH): %.50s / %.50s\n",
               cod_cn, cod_ch, gab_cn, gab_ch);
        printf("  Nota CN: %.2f | Nota CH: %.2f | Nota LC: %.2f | Nota MT: %.2f | Nota RED: %.2f\n",
               p.nota_cn, p.nota_ch, p.nota_lc, p.nota_mt, p.nota_red);
        printf("\n");
    }

    fclose(fp_gab);
    fclose(fp_loc);
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

void buscar_participante_por_nuseq(const char *nu_seq) {
    // 1. Abertura dos arquivos
    HeaderTrie h_trie;
    FILE *fp_trie = abrir_arquivo_trie(nome_trie_bin, &h_trie);
    if (!fp_trie) {
        perror("Erro ao abrir arquivo da Trie");
        return;
    }

    // 2. Busca na Trie (O(L))
    int indice_registro = buscar_trie(fp_trie, &h_trie, nu_seq);

    if (indice_registro == -1) {
        printf("Participante com NU_SEQ '%s' nao encontrado.\n", nu_seq);
        fclose(fp_trie);
        return;
    }

    // 3. Recuperação do Registro Principal (O(1))
    FILE *fp_participantes = fopen(nome_participantes_bin, "rb");
    if (!fp_participantes) {
        perror("Erro ao abrir arquivo de participantes");
        fclose(fp_trie);
        return;
    }

    Participante *p = ler_participante_por_indice(fp_participantes, indice_registro);

    if (p) {
        printf("------------------------------------------------------------------------\n");
        printf("Registro encontrado (Busca O(L) via Trie, acesso O(1) ao dado):\n");
        printf("  NU_SEQ: %s\n", p->nu_seq);
        printf("  Indice de Registro: %d\n", indice_registro);
        printf("  Nota CN: %.2f | Nota RED: %.2f\n", p->nota_cn, p->nota_red);
        printf("------------------------------------------------------------------------\n");
        free(p);
    } else {
        printf("Erro ao ler o registro do participante no indice %d.\n", indice_registro);
    }

    fclose(fp_participantes);
    fclose(fp_trie);
}

void listar_por_estado(const char* estado_sigla) {

    // 1. Abertura dos arquivos
    FILE *fp_reg_est = fopen(nome_registro_estado_bin, "rb");
    if (!fp_reg_est) {
        perror("Erro ao abrir arquivo de registro por estado");
        return;
    }

    FILE *fp_participantes = fopen(nome_participantes_bin, "rb");
    if (!fp_participantes) {
        perror("Erro ao abrir arquivo de participantes");
        fclose(fp_reg_est);
        return;
    }

    HeaderRegistroEstado h_reg_est;
    fread(&h_reg_est, tamanho_header_registro_estado(), 1, fp_reg_est);

    // 2. Busca o ponto de início na Tabela Hash
    int hash_index = funcao_hash_estado(estado_sigla);

    if (hash_index == -1) {
        printf("Estado '%s' nao reconhecido.\n", estado_sigla);
        fclose(fp_participantes);
        fclose(fp_reg_est);
        return;
    }

    int p_atual_no = h_reg_est.tabela_hash[hash_index].pont_lista;

    if (p_atual_no == -1) {
        printf("Nenhum participante encontrado para o Estado: %s\n", estado_sigla);
        fclose(fp_participantes);
        fclose(fp_reg_est);
        return;
    }

    printf("------------------------------------------------------------------------\n");
    printf("Listando participantes do Estado: %s\n", estado_sigla);
    printf("------------------------------------------------------------------------\n");

    int contador = 0;
    while (p_atual_no != -1) {

        // 3. Busca o Nó da Lista Encadeada no Arquivo
        NoRegistro *no_atual = buscar_no_registro(fp_reg_est, p_atual_no);
        if (!no_atual) break;

        // 4. Acessa o registro do Participante por índice (O(1))
        Participante *p = ler_participante_por_indice(fp_participantes, no_atual->indice_registro);

        if (p) {
            printf("%d. Estado: %s | Nota CN: %.2f | Nota RED: %.2f | Seq: %s\n",
                   ++contador, estado_sigla, p->nota_cn, p->nota_red, p->nu_seq);
            free(p);
        }

        p_atual_no = no_atual->prox; // Próximo nó na lista encadeada
        free(no_atual);
    }

    printf("Total de registros encontrados: %d\n", contador);
    printf("------------------------------------------------------------------------\n");

    fclose(fp_participantes);
    fclose(fp_reg_est);
}

// Implementação para listar do menor para o maior (Forward traversal)
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

    // Abertura dos arquivos de metadados O(1)
    HeaderLocalizacao header_loc;
    FILE *fp_loc = abrir_arquivo_localizacao(nome_localizacao_bin, &header_loc);
    HeaderProva header_gab;
    FILE *fp_gab = abrir_arquivo_gabarito(nome_gabarito_bin, &header_gab);

    if (!fp_participantes || !fp_loc || !fp_gab) {
        perror("Erro ao abrir arquivo(s) para leitura");
        if (fp_participantes) fclose(fp_participantes);
        if (fp_loc) fclose(fp_loc);
        if (fp_gab) fclose(fp_gab);
        return;
    }

    Metadados *md = le_metadados(f_metadados);
    if (!md || md->pont_raiz == -1) {
        printf("A arvore de nota_%s esta vazia.\n", tipo_nota);
        free(md);
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_participantes);
        return;
    }

    // A busca começa diretamente pelo ponteiro da PRIMEIRA folha
    int p_atual = md->pont_primeira_folha;

    printf("------------------------------------------------------------------------\n");
    printf("Listando participantes ordenados pela NOTA %s (do menor para o maior):\n", tipo_nota);
    printf("------------------------------------------------------------------------\n");

    int contador = 0;
    while (p_atual != -1) {
        NoDados *nd = buscar_no_dados(p_atual, f_dados);
        if (!nd) break;

        for (int i = 0; i < nd->m; i++) {
            EntradaIndiceNota entrada = nd->s[i];
            Participante *p = ler_participante_por_indice(fp_participantes, entrada.indice_registro);

            if (p) {
                // Busca O(1) e exibe a Localização
                Localizacao *loc = buscar_localizacao_por_indice(fp_loc, p->indice_localizacao);
                char cidade_estado[65] = "Nao Encontrada";
                char cod_esc_temp[15] = "N/A";

                if (loc) {
                    sprintf(cidade_estado, "%s/%s", loc->cidade, loc->estado);
                    strcpy(cod_esc_temp, loc->cod_esc);
                    free(loc);
                }

                printf("%d. Nota %s: %.2f | Esc. %s (%s) | Seq: %s\n",
                       ++contador, tipo_nota, entrada.nota, cod_esc_temp, cidade_estado, p->nu_seq);
                free(p);
            }
        }

        p_atual = nd->prox; // Próximo nó na lista encadeada
        free(nd);
    }

    free(md);
    fclose(fp_gab);
    fclose(fp_loc);
    fclose(fp_participantes);
    printf("------------------------------------------------------------------------\n");
}

int obter_total_registros_participantes(const char *nome) {
    FILE *fp = fopen(nome, "rb");
    if (!fp) return 0;
    HeaderParticipantes h;
    if (fread(&h, sizeof(HeaderParticipantes), 1, fp) != 1) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    return h.qtd_registros;
}

// Implementação para listar do maior para o menor (Reverse traversal)
void listar_ordenado_reverso(const char* tipo_nota) {
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

    // Abertura dos arquivos de metadados O(1)
    HeaderLocalizacao header_loc;
    FILE *fp_loc = abrir_arquivo_localizacao(nome_localizacao_bin, &header_loc);
    HeaderProva header_gab;
    FILE *fp_gab = abrir_arquivo_gabarito(nome_gabarito_bin, &header_gab);

    if (!fp_participantes || !fp_loc || !fp_gab) {
        perror("Erro ao abrir arquivo(s) para leitura");
        if (fp_participantes) fclose(fp_participantes);
        if (fp_loc) fclose(fp_loc);
        if (fp_gab) fclose(fp_gab);
        return;
    }

    Metadados *md = le_metadados(f_metadados);
    if (!md || md->pont_raiz == -1) {
        printf("A arvore de nota_%s esta vazia.\n", tipo_nota);
        free(md);
        fclose(fp_gab);
        fclose(fp_loc);
        fclose(fp_participantes);
        return;
    }

    // A busca começa diretamente pelo ponteiro da ÚLTIMA folha (maior nota)
    int p_atual = md->pont_ultima_folha;
    NoDados *maximo = buscar_no_dados(p_atual, f_dados);
    int pagina = 0;
    int sair = 0;
    int max_paginas = (maximo->m - 1) / REGPORPAG;
    char comando[COMMAND_MAX_SIZE];
    do {
        printf("------------------------------------------------------------------------\n");
        printf("Listando participantes ordenados pela NOTA %s (do maior para o menor):\n", tipo_nota);
        printf("------------------------------------------------------------------------\n");
        printf("NU_SEQ | ANO | ESCOLA | CIDADE | ESTADO | NOTA CN | NOTA CH | NOTA LC | NOTA MT| NOTA RED\n");
        int p_atual = md->pont_ultima_folha;
        while (p_atual != -1) {
            NoDados *nd = buscar_no_dados(p_atual, f_dados);
            if (!nd) break;

            // Iteração reversa no vetor de entradas do nó de dados
            for (int i = nd->m - 1 - pagina*REGPORPAG; i >= 0 && i>nd->m - 1 - pagina*REGPORPAG - REGPORPAG; i--) {
                EntradaIndiceNota entrada = nd->s[i];
                Participante *p = ler_participante_por_indice(fp_participantes, entrada.indice_registro);

                if (p) {
                    // Busca O(1) e exibe a Localização
                    Localizacao *loc = buscar_localizacao_por_indice(fp_loc, p->indice_localizacao);
                    char cidade_temp[60] = "Nao Encontrada";
                    char estado_temp[20] = "Nao Encontrado";
                    char cod_esc_temp[15] = "N/A";

                    if (loc) {
                        strcpy(cidade_temp, loc->cidade);
                        strcpy(estado_temp, loc->estado);
                        strcpy(cod_esc_temp, loc->cod_esc);
                        free(loc);
                    }

                    printf("%s | %d | %s | %s | %s | %f | %f | %f | %f | %f\n", p->nu_seq, p->ano, cod_esc_temp, cidade_temp, estado_temp, p->nota_cn, p->nota_ch, p->nota_lc, p->nota_mt, p->nota_red);
                    free(p);
                }
            }

            p_atual = nd->ant; // Nó ANTERIOR na lista duplamente encadeada
            free(nd);
        }
        printf("\nRegistros de %d ate %d da lista ordenada\nPagina %d de %d\nDigite qual pagina voce gostaria de exibir (ou digite 'BACK' se quiser retornar)\n", maximo->m - 1 - pagina*REGPORPAG, MAX(0, maximo->m - 1 - pagina*REGPORPAG - REGPORPAG), pagina, max_paginas);
                if (fgets(comando, COMMAND_MAX_SIZE, stdin) == NULL) continue;
                size_t len = strlen(comando);
                if (len > 0 && comando[len-1] == '\n') {
                    comando[len-1] = '\0';
                }
                to_lowercase(comando);
                if (strcmp(comando, "back") == 0)
                {
                    sair = 1;
                }
                else
                {
                    char *endptr;
                    int nova_pagina = strtol(comando, &endptr, 10);
                    if (nova_pagina < 0)
                        {
                        printf("ERRO: O numero da pagina deve ser pelo menos 0.\n");
                        }
                    else if (nova_pagina > max_paginas)
                    {
                        printf("ERRO: A pagina %ld nao existe. A pagina maxima e %d.\n", nova_pagina, max_paginas);
                    }
                    else
                    {
                        pagina = nova_pagina;
                    }
                }

    } while (!sair);
    free(md);
    fclose(fp_gab);
    fclose(fp_loc);
    fclose(fp_participantes);
    printf("------------------------------------------------------------------------\n");
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
        printf("READ - Le um arquivo CSV com registros e faz toda a estruturacao\n");
        printf("SHOW - Mostra na tela os registros salvos em ordem de insercao, com todas informacoes\n");
        printf("LIST <NOTA> - Lista registros ordenados por nota. <NOTA>: CN, CH, LC, MT, RED\n");
        printf("FIND <NU_SEQ> - Busca um participante pela chave unica (Ex: FIND 0123456789)\n");
        printf("FILTER <ESTADO> - Lista todos os participantes de um Estado (ex: FILTER RS)\n");
        printf("CONFIG - Configura quantos registros devem aparecer por pagina\n");
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
            if (remove(nome_gabarito_bin) == 0) {
                 printf("Arquivo de Gabaritos '%s' removido com sucesso.\n", nome_gabarito_bin);
            } else {
                 perror("Aviso: Nao foi possivel remover o arquivo de gabaritos.bin");
            }
            if (remove(nome_localizacao_bin) == 0) {
                 printf("Arquivo de Localizacao '%s' removido com sucesso.\n", nome_localizacao_bin);
            } else {
                 perror("Aviso: Nao foi possivel remover o arquivo de localizacao.bin");
            }
            if (remove(nome_trie_bin) == 0) { // NOVO: Remove Trie
                 printf("Arquivo da Arvore Trie '%s' removido com sucesso.\n", nome_trie_bin);
            } else {
                 perror("Aviso: Nao foi possivel remover o arquivo trie_nuseq.bin");
            }
            if (remove(nome_participantes_bin) == 0) {
                printf("Arquivo de dados principal '%s' removido com sucesso.\n", nome_participantes_bin);
            } else {
                perror("Aviso: Nao foi possivel remover o arquivo de participantes.bin");
            }
            if (remove(nome_registro_estado_bin) == 0) { // NOVO: Remove Arquivo Invertido
                 printf("Arquivo Invertido por Estado '%s' removido com sucesso.\n", nome_registro_estado_bin);
            } else {
                 perror("Aviso: Nao foi possivel remover o arquivo reg_por_estado.bin");
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
        }
        else if (strcmp(comando_base, "filter") == 0) {
            if (arg[0] != '\0') {
                char estado_sigla[3];
                // Certifica-se que o estado está em maiúsculas para o hash
                estado_sigla[0] = toupper(arg[0]);
                estado_sigla[1] = toupper(arg[1]);
                estado_sigla[2] = '\0';
                listar_por_estado(estado_sigla);
            } else {
                printf("Comando FILTER requer um argumento (ex: FILTER RS).\n");
            }
        } else if (strcmp(comando_base, "list") == 0) {
            if (arg[0] != '\0') {
                to_lowercase(arg);
                printf("\nVoce quer ver os registros ordenados em ordem crescente (1) ou descrescente (2)?\n");
                if (fgets(comando, COMMAND_MAX_SIZE, stdin) == NULL) continue;
                size_t len = strlen(comando);
                if (len > 0 && comando[len-1] == '\n') {
                    comando[len-1] = '\0';
                }
                to_lowercase(comando);
                if (strcmp(comando, "crescente") == 0 || strcmp(comando, "1") == 0)
                {
                    listar_ordenado(arg);
                }
                else if (strcmp(comando, "decrescente") == 0 || strcmp(comando, "2") == 0)
                {
                    listar_ordenado_reverso(arg);
                }
                else if (strcmp(comando, "back") == 0 || strcmp(comando, "0") == 0)
                {
                }
                else
                {
                    printf("\nArgumento de ordenacao invalido '%s'", comando);
                }
            } else {
                printf("Comando LIST requer um argumento (ex: LIST cn, LIST red).\n");
            }
        } else if (strcmp(comando_base, "find") == 0) {
            if (arg[0] != '\0') {
                buscar_participante_por_nuseq(arg);
            } else {
                printf("Comando FIND requer o NU_SEQ (ex: FIND 1234567890123).\n");
            }
        } else if (strcmp(comando_base, "config") == 0) {
            printf("\nDigite quantos registros voce quer que aparecam por pagina\n");
            if (fgets(comando, COMMAND_MAX_SIZE, stdin) == NULL) continue;
                size_t len = strlen(comando);
                if (len > 0 && comando[len-1] == '\n') {
                    comando[len-1] = '\0';
                }
                char *endptr;
                int novo_REGPORPAGINA = strtol(comando, &endptr, 10);
                if (novo_REGPORPAGINA < 1)
                {
                    printf("ERRO: A quantidade de registros por pagina deve ser pelo menos 1.\n");
                }
                else if(novo_REGPORPAGINA >= 1)
                {
                    REGPORPAG = novo_REGPORPAGINA;
                }
                else
                {
                   printf("\nArgumento invalido '%s'", comando);
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
