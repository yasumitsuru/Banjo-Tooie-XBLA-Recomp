"""MISSAO 01 - troca links simbolicos do Git por copias do arquivo de destino.

No Windows sem Modo de Desenvolvedor o Git grava cada link simbolico (modo 120000) como um
arquivo de texto com o caminho do destino. O libmspack (dependencia do SDK) usa 14 links em
cabextract/mspack/*.c|*.h; compilados assim, falham ("expected identifier"). Este script, para o
repositorio dado e todos os submodulos, substitui cada link ainda em forma de texto pela copia do
arquivo de destino. Nao altera commits nem o indice; e idempotente (pula o que ja foi copiado).

Uso: python -I materialize_symlinks.py <raiz_do_repositorio>
"""
import os
import shutil
import subprocess
import sys


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout


def repos(root):
    out = [root]
    listing = git(root, "submodule", "foreach", "--quiet", "--recursive", "echo $displaypath")
    for line in listing.splitlines():
        line = line.strip()
        if line:
            out.append(os.path.join(root, *line.split("/")))
    return out


def main():
    root = os.path.abspath(sys.argv[1])
    fixed = skipped = missing = 0
    for repo in repos(root):
        for line in git(repo, "ls-files", "-s", "-z").split("\0"):
            if not line.startswith("120000 "):
                continue
            rel = line.split("\t", 1)[1]
            link = os.path.join(repo, *rel.split("/"))
            target_text = git(repo, "cat-file", "-p", line.split()[1])
            target = os.path.normpath(os.path.join(os.path.dirname(link), target_text.strip()))
            if not os.path.isfile(link) or not os.path.isfile(target):
                missing += 1
                print(f"[aviso] sem destino: {rel} -> {target_text.strip()}")
                continue
            with open(link, "rb") as f:
                current = f.read()
            if current.strip() != target_text.strip().encode("utf-8"):
                skipped += 1  # ja e uma copia (ou um link real)
                continue
            shutil.copyfile(target, link)
            fixed += 1
            print(f"copiado: {os.path.relpath(link, root)} <- {os.path.relpath(target, root)}")
    print(f"links copiados={fixed} ja_ok={skipped} sem_destino={missing}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
