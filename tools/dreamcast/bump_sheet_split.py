#!/usr/bin/env python
"""Parte en páginas sueltas una hoja de relieves hecha con una herramienta externa.

Entrada: la hoja de relieves (la misma cuadrícula que TODAS_LAS_PAGINAS_*.png
pero ya procesada: mapa de alturas en gris o mapa de normales en RGB) y el
indice.txt que acompaña a la hoja. Salida: un PNG por página, con el nombre
de 8 cifras (el hash) que usa texvq, listo para build/bump_custom/<juego>/.

    python bump_sheet_split.py hoja_relieve.png indice.txt build/bump_custom/tr1
    python bump_sheet_split.py hoja_relieve.png indice.txt salida --columnas 13

El tamaño de cada celda se saca del ancho de la hoja (si la herramienta la
amplió al doble, las celdas salen de 512 y texvq las reduce a 256). Necesita
Pillow (pip install pillow).
"""
import argparse
import os
import re
import sys

from PIL import Image


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("hoja")
    ap.add_argument("indice")
    ap.add_argument("salida")
    ap.add_argument("--columnas", type=int, default=13)
    args = ap.parse_args()

    hashes = []
    for line in open(args.indice, encoding="utf-8"):
        m = re.search(r"([0-9a-f]{8})\.png", line)
        if m:
            hashes.append(m.group(1))
    if not hashes:
        sys.exit("el índice no tiene páginas")

    sheet = Image.open(args.hoja)
    cols = args.columnas
    rows = (len(hashes) + cols - 1) // cols
    cell_w = sheet.width // cols
    cell_h = sheet.height // rows
    if abs(cell_w * cols - sheet.width) > cols or abs(cell_h * rows - sheet.height) > rows:
        print(f"aviso: la hoja ({sheet.width}x{sheet.height}) no encaja justa en {cols}x{rows} celdas")
    os.makedirs(args.salida, exist_ok=True)
    for i, h in enumerate(hashes):
        x = (i % cols) * cell_w
        y = (i // cols) * cell_h
        sheet.crop((x, y, x + cell_w, y + cell_h)).save(os.path.join(args.salida, h + ".png"))
    print(f"{len(hashes)} páginas de {cell_w}x{cell_h} en {args.salida}")


if __name__ == "__main__":
    main()
