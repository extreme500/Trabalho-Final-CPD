from pathlib import Path
import pandas as pd

def mostrar_colunas(caminho_arquivo: Path) -> None:
    if not caminho_arquivo.exists():
        print(f"Arquivo não encontrado: {caminho_arquivo}")
        return

    amostra = pd.read_csv(caminho_arquivo, encoding="utf-8-sig", sep=";")

    total_colunas = amostra.shape[1]
    total_linhas = amostra.shape[0]
    print(f"\nTotal de colunas: {total_colunas}\n")
    print(f"\nTotal de linhas: {total_linhas}\n")
    print("Índice  ->  Nome da coluna")
    print("-" * 40)

    for i, nome in enumerate(amostra.columns):
        print(f"{i:3} -> {nome}")

if __name__ == "__main__":

    entrada = Path("C:/Users/luck1/Desktop/TrabalhoCPD/RESULTADOS_2024_LIMPO.csv")

    mostrar_colunas(entrada)