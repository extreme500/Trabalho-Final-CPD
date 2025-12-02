from pathlib import Path
import pandas as pd

pasta = Path("C:/Users/luck1/Desktop/TrabalhoCPD")
arquivo_origem = pasta / "RESULTADOS_2024.csv"
arquivo_saida = pasta / "RESULTADOS_2024_LIMPO.csv"

ENCODING = "latin1"
ENCODING_SAIDA = "utf-8-sig"
SEPARADOR = ";"

INDICES_COLUNAS = [0, 2, 4, 6, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 41]

USECOLS = INDICES_COLUNAS
NLINHAS = 4000000

def limpar(df: pd.DataFrame) -> pd.DataFrame:

    # transforma strings vazias e espaços em NaN
    df = df.replace(r'^\s*$', pd.NA, regex=True)

    # dropa qualquer linha que tenha NaN em qualquer coluna
    df_limpo = df.dropna(how="any")

    return df_limpo


def filtrar_e_limpar():
    if not arquivo_origem.exists():
        print(f"Arquivo de origem não encontrado: {arquivo_origem}")
        return

    print("Lendo arquivo")
    df = pd.read_csv(
        arquivo_origem,
        encoding=ENCODING,
        sep=SEPARADOR,
        usecols=USECOLS,
        nrows=NLINHAS,
        na_values=["", " ", "NA", "NaN"]
    )

    print(f"Formato dos dados lidos: {df.shape}")

    print("Limpando linhas com valores vazios nas colunas de dados...")
    df_limpo = limpar(df)

    print(f"Formato após limpeza: {df_limpo.shape}")

    print("Salvando arquivo filtrado e limpo...")
    df_limpo.to_csv(
        arquivo_saida,
        index=False,
        encoding=ENCODING_SAIDA,
        sep=SEPARADOR
    )

    print("\nConcluído!")

if __name__ == "__main__":
    filtrar_e_limpar()
