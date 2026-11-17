# Revisão de dados sensíveis e direitos autorais + publicação no GitHub (2026-11-17)

Pedido: verificar o projeto e o Git local (inclusive o histórico) atrás de informação sensível e
de arquivos com direitos autorais, corrigir, criar o repositório "Banjo-Tooie XBLA Recomp" no
GitHub, enviar o projeto e gerar a build Release do launcher.

## O que foi verificado

- **Todas as versões de todos os arquivos** de todas as branches e referências (`git rev-list
  --objects --all`, 755 versões antes da correção) com `_audit/out/scripts/varre_historico.py`:
  nome de usuário do Windows, pastas pessoais (`C:\Users\…`), e-mails pessoais, tokens do GitHub,
  chaves de API e privadas, senhas, IPs privados, nomes de máquina e identificadores de console.
- **Arquivos binários** de todo o histórico (imagens, dados, executáveis, arquivos do jogo).
- **Mensagens e autores dos commits**, configuração local do Git, tags, stash e referências.
- O **submódulo `rexglue-sdk`** (14 commits locais por cima da base pública `035aa25`).

## O que foi encontrado e corrigido

| Achado | Onde | Correção |
|---|---|---|
| Nome de usuário do Windows em caminhos `C:\Users\…` | 13 arquivos do histórico (auditoria M00, logs de ferramentas, `preflight_*.json` da M01, logs do npm) | trocado por `<usuario>` em todas as versões |
| Capturas de tela do jogo (conteúdo com direito autoral) | 4 PNG em `docs/evidencias/check4/` (só no histórico) | pasta `check4` (sobras de revisões automáticas: logs do npm com caminhos da máquina) removida de todo o histórico |
| Fila local do Understory (`.understory/`, `.understory-project-id`) | 20 arquivos em commits de checkpoint | removida de todo o histórico |
| E-mail pessoal no autor dos commits do SDK | 14 commits do submódulo | trocado pelo endereço anônimo do GitHub (`…@users.noreply.github.com`) |
| Nada encontrado | tokens, senhas, chaves, e-mails nos commits do projeto (autor genérico), arquivos do jogo (`default.xex`, `RAWFiles`), código gerado do jogo (`generated/`, nunca versionado) | — |

O histórico foi reescrito com `git fast-export` → filtro (`_audit/out/scripts/reescreve_historico.py`)
→ `git fast-import` (mesmos 48 commits, mesmas mensagens; só mudam os arquivos acima e os
ponteiros do submódulo, remapeados para os commits reescritos do SDK). As referências de
checkpoint também foram reescritas, e os objetos antigos apagados do `.git` (`reflog expire` +
`gc --prune=now`). Nova varredura: nenhum dado pessoal e nenhuma imagem do jogo em nenhuma versão.

O `.gitignore` agora barra arquivos do jogo (`*.xex`, `*.cmp`, `*.ptc`, `RAWFiles/`, saves), a fila
do Understory, as sobras de rodadas automáticas e as notas locais de auditoria.

**Backup do histórico antigo** (contém os dados acima; fica só neste PC, fora do Git):
`_audit/out/backup-git/app-antes-da-reescrita.bundle` e `rexglue-sdk-ptbr-antes-da-reescrita.bundle`.
Apague quando não precisar mais.

## Publicação

- **Privada**, por decisão desta rodada (a pessoa deixou as escolhas para o agente): o programa do
  port contém código recompilado do jogo, a base (iron-jay/banjo_tooie_xbla_recomp) não declara
  licença e o projeto é de uso pessoal (`docs/DECISOES.md` nº 4). Dá para tornar público depois
  só o código-fonte, sem o ZIP.
- Projeto: `https://github.com/yasumitsuru/Banjo-Tooie-XBLA-Recomp` (branches `port-pc-4k60`
  — padrão —, `master`, `missao-01-port-nativo`, `missao-02-traducao`; os checkpoints locais não
  foram enviados).
- SDK modificado (BSD-3-Clause): `https://github.com/yasumitsuru/Banjo-Tooie-XBLA-Recomp-rexglue-sdk`
  (branch `ptbr-port-banjo3`), agora a URL do submódulo em `.gitmodules`.
- Release `launcher-v1.0.0` com o ZIP portátil do launcher (sem arquivos do jogo).

Para baixar e compilar: `git clone --recursive https://github.com/yasumitsuru/Banjo-Tooie-XBLA-Recomp.git`
(conta com acesso aos dois repositórios privados) e seguir "Como compilar e rodar" do `README.md`.

## Pontos que continuam com a pessoa

- A base do port não tem licença: publicar o código em público exige a permissão do autor.
- Textos curtos do jogo citados como evidência (por exemplo "PRESSIONE START PARA JOGAR" nos
  testes da tradução) e os nomes das conquistas traduzidos em `pc_achievements.cpp` continuam no
  código; são trechos curtos, mas são texto do jogo.
- Cópias do repositório feitas antes desta revisão (se houver) ainda têm o histórico antigo.
