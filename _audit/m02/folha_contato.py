"""MISSAO 02 (rodada 2) - junta capturas t*.png de uma pasta em folhas de contato (so leitura).

Uso: python -I folha_contato.py <pasta> [colunas=4] [por_folha=16] [largura=400]
Grava <pasta>/folha_NN.png com o nome de cada captura escrito em cima da miniatura.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

pasta = Path(sys.argv[1])
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 4
per = int(sys.argv[3]) if len(sys.argv) > 3 else 16
tw = int(sys.argv[4]) if len(sys.argv) > 4 else 400
files = sorted(p for p in pasta.glob("t*.png"))
th = tw * 9 // 16
for n in range(0, len(files), per):
    chunk = files[n:n + per]
    rows = (len(chunk) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * th), "black")
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(chunk):
        im = Image.open(f).convert("RGB").resize((tw, th))
        x, y = (i % cols) * tw, (i // cols) * th
        sheet.paste(im, (x, y))
        d.rectangle([x, y, x + 70, y + 14], fill="black")
        d.text((x + 2, y + 1), f.stem, fill="yellow")
    out = pasta / f"folha_{n // per:02d}.png"
    sheet.save(out)
    print(out, len(chunk))
