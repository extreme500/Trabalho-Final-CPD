from pathlib import Path
import pandas as pd

pasta = Path("C:/Users/luck1/Desktop/TrabalhoCPD")
arquivo_origem = pasta / "RESULTADOS_2024_LIMPO.csv"
arquivo_saida = pasta / "struct_provaD2_10.csv"

ENCODING = "utf-8-sig"
SEPARADOR = ";"

    #Índice  ->  Nome da coluna
    # ----------------------------------------
    # 0 -> NU_SEQUENCIAL
    # 1 -> CO_ESCOLA
    # 2 -> NO_MUNICIPIO_ESC
    # 3 -> SG_UF_ESC
    # 4 -> CO_PROVA_CN
    # 5 -> CO_PROVA_CH
    # 6 -> CO_PROVA_LC
    # 7 -> CO_PROVA_MT
    # 8 -> NU_NOTA_CN
    # 9 -> NU_NOTA_CH
    # 10 -> NU_NOTA_LC
    # 11 -> NU_NOTA_MT
    # 12 -> TX_RESPOSTAS_CN
    # 13 -> TX_RESPOSTAS_CH
    # 14 -> TX_RESPOSTAS_LC
    # 15 -> TX_RESPOSTAS_MT
    # 16 -> TP_LINGUA
    # 17 -> TX_GABARITO_CN
    # 18 -> TX_GABARITO_CH
    # 19 -> TX_GABARITO_LC
    # 20 -> TX_GABARITO_MT
    # 21 -> NU_NOTA_REDACAO

    # Total: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21
    # Participante: 0, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 21
    # Local: 1, 2, 3
    # ProvaD1: 5, 6, 16, 18, 19
    # ProvaD2: 4, 7, 17, 20

INDICES_COLUNAS = [4, 7, 17, 20]

USECOLS = INDICES_COLUNAS
NLINHAS = 10

def filtrar_colunas():
    if not arquivo_origem.exists():
        print(f"Arquivo de origem não encontrado: {arquivo_origem}")
        return

    print("Lendo arquivo")
    df = pd.read_csv(
        arquivo_origem,
        encoding=ENCODING,
        sep=SEPARADOR,
        usecols=USECOLS,
        nrows=NLINHAS
    )

    print(f"Formato dos dados lidos: {df.shape}")

    print("Salvando arquivo filtrado...")
    df.to_csv(arquivo_saida, index=False, encoding="utf-8-sig", sep=SEPARADOR)

    print("\nConcluído!")

if __name__ == "__main__":
    filtrar_colunas()
