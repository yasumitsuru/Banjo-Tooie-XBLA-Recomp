# MISSÃO 02 — Integridade da tradução PT-BR (relatório)

Data: 2026-11-10. Branch `missao-02-traducao`. Escopo: preservar os textos e recursos do Banjo-Tooie
XBLA **com a tradução PT-BR já aplicada pela pessoa**, sem retraduzir, sem usar outra tradução e
sem publicar conteúdo do jogo. Nenhum arquivo do jogo foi alterado (36/36 sha256 iguais antes e
depois de cada teste).

**Resultado em uma frase:** a tradução está íntegra em tudo o que foi possível medir; não foi
encontrada nenhuma incompatibilidade de leitura/extração, então **nenhuma correção nos dados ou no
port foi necessária**. O que foi feito é a **automação** (inspeção, linha de base, regressão visual
no jogo real, teste negativo) e as **opções de teste** do port que a tornam possível.

**Rodada 2 (2026-11-10, 15:40–20:35):** o teste automático passou a entrar na **parte jogável**
(Spiral Mountain) e conferir o **menu de pausa (START)**, três submenus, uma **mensagem do sistema**
e uma **fala do Bottles com ícone de botão** — 6 telas novas, todas em PT-BR com acentos
(§6.2). A regressão visual ganhou nota **por linha de texto** (um item de menu ou o fim de uma frase
sumido agora reprova), o teste negativo foi de 11/11 para **22/22** e a regra real de idioma, lida
no código do jogo, virou a verificação **L1** (§3, `MECHANICS.md` §11). Continua sem nenhuma correção
nos dados ou no port: só ferramentas de teste mudaram.

## 1. Ponto de restauração (rollback)

| O quê | Referência |
|---|---|
| Instantâneo de toda a árvore antes de qualquer mudança | `refs/checkpoints/missao-02-inicio-20261110` = `3590fff` |
| Base limpa (trabalho anterior sem commit, sem mudança de conteúdo) | commit `97232bf` |
| Trabalho desta missão | commit `59eb610` no branch `missao-02-traducao` |
| Instantâneo antes de fechar a rodada 2 (disco inteiro, sem a pasta não versionada do Codex) | `refs/checkpoints/m02r2-20261110-2020` = `e3dcdaf` |
| Rodada 2 | commit seguinte a `59eb610` no mesmo branch |

Desfazer tudo desta missão: `git checkout 97232bf -- .` (ou voltar o branch para `97232bf`). As
mudanças no port são opções de teste desligadas por padrão; sem elas o executável se comporta como
na Missão 01.

## 2. Arquivos traduzidos fornecidos e como o port os carrega

Pasta da pessoa: `W:\Decomp\Banjo 3\Banjo Tooie BR` (somente leitura). O port monta essa pasta
como `GAME:\` e o **próprio código do jogo** abre e lê os arquivos (rastro de E/S do runtime,
`--log_level trace`). O port não traduz nem converte nada.

| Arquivo (RAWFiles\\) | Papel | Indício de tradução | O jogo leu (rastro) |
|---|---|---|---|
| `X360_strings.dat` (427.089 B) | textos da camada XBLA: tela de abertura, menus, ajuda, configurações, mensagens do sistema; 1.521 strings × 6 idiomas | data de modificação 24/10 17:04 (os não traduzidos têm 13:47); PT-BR no espaço do inglês | cabeçalho + **só o bloco 0 (PT-BR)**: 46.080 + 445 B |
| `db360.cmp` (12.525.322 B) | banco de dados do jogo (diálogos e assets), XCTD/LZX | 24/10 14:19 | **inteiro** (96 leituras) |
| `db360.textures.cmp` (50.625.264 B) | texturas, XCTD/LZX | 24/10 14:22 | **inteiro** (387 leituras) |
| `xarialuni.ttf` (6.608.540 B) | fonte TrueType da camada XBLA | 13:47 (não modificada) | 289.604 B (tabelas usadas) |
| `banjo2_360.ctl/.tbl`, `ArcadeWide.ptc` | áudio e sobreposição do Arcade (sem texto) | 13:47 | sim |
| `ArcadeInfo.xml` (raiz) | descrição do painel do Xbox (pt-br) | — | não (é do painel, não do jogo) |

Detalhes do carregamento (medidos):

- **Idioma:** o jogo lê o idioma em `ExGetXConfigSetting` (auditoria, `0x825B1FA8`); o runtime
  responde com `user_language`, que vale **1 (inglês) por padrão** (`rexglue-sdk/src/kernel/xam/xam_user.cpp:31`).
  A tradução ocupa o espaço do inglês, por isso o PT-BR aparece sem nenhuma configuração.
  **Rodada 2 (código lido):** `0x8227D2D0` converte o idioma num código interno e `main`
  (`0x8227D9F0`) escolhe o bloco: inglês e **qualquer idioma que não seja JA/DE/FR/ES/IT** (códigos
  1 e 7–12) → bloco 0 (PT-BR); o carregador `0x822E9C90` lê só esse bloco. O mesmo código interno vai
  para a variável `0xC0D` do jogo, que escolhe o espaço de idioma dos diálogos e é regravada a cada
  carregamento do save (`MECHANICS.md` §11).
  Experimento (`_audit/out/m02/exp5_lang7`, `exp6_lang3`): com `--user_language 7` o jogo ainda lê o
  bloco 0 (PT-BR); com `--user_language 3` lê o bloco do alemão. **Risco:** mudar `user_language`
  tira o PT-BR (ver §8).
- **db360:** o leitor do jogo exige o cabeçalho XCTD `0x0FF512ED` versão `0x01000000` (`0x825B9218`);
  `0x82220790` guarda o buffer em `0x8269EE80` e a tabela de assets (buffer + 8) em `0x826CC034`.
  Na memória: 14.030 entradas, dados a partir de +0xDB40.
- **Diálogos:** registros com 6 espaços de idioma na ordem PT-BR(inglês), japonês, francês, alemão,
  italiano, espanhol; 2.356 registros (1.722 tipo A, 634 tipo B), 9.793 falas PT-BR.
- `db360_TexFileList.txt` falta e é opcional (auditoria, `fnd_18`); é a única abertura que falha.

## 3. Comparação de estrutura, codificação, ponteiros e tamanhos

A base (iron-jay) não traz dados do jogo; a referência é o **formato que o código do jogo lê** e
os **outros 4 idiomas latinos** do mesmo arquivo (a tradução foi feita sobre o inglês, que não está
nos arquivos). Verificações automáticas de `tools/m02/traducao.py`, todas **PASS** na rodada final:

| Id | Verificação | Resultado |
|---|---|---|
| X1–X4 | X360_strings: cabeçalho, soma dos comprimentos = tamanho de cada bloco, blocos dentro do arquivo, terminador | 1.521 × 6; PT-BR 46.525/46.525 B; 0 sem terminador |
| X5 | nenhuma string de 8 bits com 0 no meio (nada truncado) | 969 strings, 0 com 0 interno |
| X6 | mensagens do sistema UTF-16BE bem formadas | 66 strings, 0 com defeito |
| X7 | mesma codificação por string que FR/DE/ES/IT | 0 diferentes |
| X8 | nenhuma string vazia onde os outros idiomas têm texto | 1.035/1.035 com PT-BR |
| X9 | marcadores (`%d`/`%s`, `~`, ícones de botão) iguais aos de algum idioma | 0 diferentes de todos |
| T1–T2 | fonte íntegra (somas de verificação) e cobrindo todos os caracteres PT-BR da camada XBLA | 7 tabelas OK; 96 caracteres, 0 faltando |
| A1 | ArcadeInfo.xml válido com pt-br | UTF-16, 8 idiomas incluindo pt-br |
| C1–C2 | cabeçalhos XCTD de db360*.cmp que o leitor exige | magia/versão corretas, janela LZX 128 KiB |
| D1 | tabela de assets do db360 na memória coerente | 14.030 entradas (12.839 + 1.191 apelidos) |
| D2–D4 | diálogos PT-BR com a mesma estrutura (comandos, ordem, número de falas) e os mesmos marcadores de FR/DE/IT | 2.356/2.356 registros (2.349 iguais aos três); 0 marcadores divergentes |
| D5–D6 | nenhum diálogo em inglês no espaço PT-BR; nenhuma fala vazia onde os outros têm texto | 0; 3 falas vazias que nos outros idiomas são só um espaço |
| M1 | bloco PT-BR do X360_strings na memória do jogo idêntico ao arquivo | 46.525 B achados intactos |
| R1–R5 | o jogo lê o bloco PT-BR e nenhum outro idioma; lê os arquivos traduzidos; nenhuma abertura falha (fora a lista opcional); db360.cmp e db360.textures.cmp lidos por inteiro | PASS |
| B1 | regressão: hashes, contagens e hashes dos textos iguais à linha de base | 0 diferenças |
| L1 (rodada 2) | o `user_language` que o port entrega (`banjotooie.toml` ao lado do exe, senão o padrão do runtime), passado pela regra real do jogo (`0x825B1FA8` → `0x8227D2D0` → `0x8227D9F0`), escolhe o bloco 0 e o espaço 0 dos diálogos (PT-BR); os rótulos de idioma do verificador conferem com o código | user_language=1 → código interno 0 → bloco 0; rótulos 6/6 |

Observações (não são defeitos, nada foi alterado):
- `X360_strings.dat` tem 117.751 bytes depois dos 6 blocos (92,8% zeros) que o jogo não lê.
- Uma fala usa `Û`/`Â` de propósito (sotaque de personagem); é estilo da tradução.
- Caracteres acentuados usados: camada XBLA À Á Â Ã Ç É Ê Í Ó Ô Õ Ú à á ã ç é ê í ó º; diálogos
  À Á Â Ã Ç É Ê Í Ó Ô Õ Ú Û (contagens em `docs/evidencias/m02/linha_de_base_traducao.json`).

## 4. Correções

**Nenhuma.** Nenhuma incompatibilidade de leitura/extração foi comprovada: o jogo lê os arquivos
traduzidos por inteiro, a estrutura confere com os outros idiomas e o texto aparece no jogo com os
acentos. Portanto nenhum arquivo do jogo e nenhum código do jogo foram mudados.

## 5. O que mudou (arquivos)

| Arquivo | Mudança |
|---|---|
| `banjotooie-recomp/src/port_test_input.cpp/.h` (novo) | controle virtual **só para testes** (`--port_test_input "start@26,a@30"`): driver de entrada oficial do SDK, aperta botões em tempos fixos contados do início do jogo; sem a opção, nada muda |
| `banjotooie-recomp/src/banjotooie_app.cpp/.h` | opções de teste desligadas por padrão: `--port_test_input`, `--port_test_shots` (capturas extras sem sair), `--port_dump_db` (cópia do db360 como o jogo o tem na memória), `--port_dump_ranges` (cópia de faixas da memória, só leitura, só páginas mapeadas); opção inválida sai com código 6 antes de abrir a janela |
| `banjotooie-recomp/CMakeLists.txt` | compila `port_test_input.cpp` |
| `tools/test_traducao.ps1` (novo) | teste automático de ponta a ponta (§6) |
| `tools/m02/traducao.py` (novo) | inspeção e regressão dos arquivos traduzidos (§3) |
| `tools/m02/comparar_capturas.py` (novo) | regressão visual do texto (IoU dos pixels da cor do texto numa região) |
| `tools/m02/teste_negativo.py` (novo) | prova que o verificador acusa defeitos plantados em cópias (§7) |
| `docs/evidencias/m02/` (novo) | linha de base e resultados (só hashes, contagens e status; sem texto nem imagens do jogo) |
| `_audit/m02/*.py`, `_audit/DecompileAt.java`, `_audit/decompile_at.cmd` | scripts da investigação (Ghidra headless, rastro, tabelas) |

Os textos completos extraídos e as capturas do jogo ficam só em `_audit/out/` (fora do Git), por
serem conteúdo do jogo.

**Rodada 2** (nenhuma mudança no port nem nos dados):

| Arquivo | Mudança |
|---|---|
| `tools/test_traducao.ps1` | passo **4b** (parte jogável: pausa, submenus, mensagem do sistema, fala do Bottles); nota por linha (`-IouLinhaMinimo`, padrão 0,75); `-GravarReferenciasNovas` (grava só as referências que faltam, sem tocar nas já conferidas); `refn` (outra região da mesma tela usa a mesma referência); se o processo não fechar sozinho depois de pedir a saída, o passo é julgado pelas capturas e o travamento vira um AVISO separado |
| `tools/m02/comparar_capturas.py` | cor `cinza` (menu de pausa), IoU de cada linha de texto da referência (`iou_linha_min`, `linhas`), leitura rápida com Pillow quando instalado (mesmos valores RGB) |
| `tools/m02/teste_negativo.py` | casos 9: item de menu apagado, trecho de linha apagado, fim de frase cortado, 2 telas trocadas, fala ausente e 3 controles (§7) |
| `.gitignore` | ignora em `docs/evidencias/` as saídas do teste que são conteúdo do jogo (`*.bin`, `textos_*_ptbr.txt`, pastas `user/` e `cache/`) |
| `_audit/m02/explorar_jogo.ps1`, `_audit/m02/folha_contato.py` (novos) | exploração: capturas a cada N segundos com o controle virtual e folhas de miniaturas para achar as telas |
| `docs/evidencias/m02/*_r2.json` (novos) | resultados da rodada 2 (status, IoU, hashes; sem imagens nem textos do jogo) |
| `MECHANICS.md` §11, `mechanics.json` (novo) | regras reais lidas no `default.xex` (Ghidra): formato e carregador do `X360_strings.dat`, tabela idioma → bloco, variável `0xC0D` dos diálogos, carregador do `db360`, com endereço e evidência de cada uma |
| `tools/m02/traducao.py` | verificação **L1** com a tabela real de idioma do jogo e opção `--exe` |
| `tools/test_traducao.ps1` | passa `--exe` à inspeção (a L1 lê o `banjotooie.toml` ao lado do exe) |
| `tools/m02/teste_negativo.py` | casos `runtime_idioma_alemao` (`user_language = 3`) e `runtime_idioma_invalido` (`0`) |

## 6. Teste automático — `pwsh -File tools\test_traducao.ps1`

Sem ação manual. A janela abre atrás das outras, sem som e sem roubar o foco; cada execução usa
pastas de usuário e cache próprias dentro de `_audit/out`.

1. inspeção estática + regressão contra a linha de base (inclui o sha256 dos 36 arquivos ANTES);
2. jogo real até a abertura com rastro de E/S, cópia do db360 e da memória com o texto → inspeção
   completa (§3);
3. jogo real com controle virtual: abertura → START → menu principal → AJUDA E OPÇÕES → CONFIGURAÇÕES;
4. jogo real: menu → JOGAR → seleção de jogo → introdução com dois textos do db360;
4b. (rodada 2) jogo real, parte jogável: pula a introdução, abre a pausa e percorre VER TOTAIS,
   AJUDA E OPÇÕES, CLASSIFICAÇÕES (mensagem do sistema) e a confirmação de SAIR DO JOGO **sem
   confirmar**; fecha a pausa e captura a fala do Bottles (§6.2);
5. regressão visual do texto (rodada 2: 14 telas em 15 comparações) contra referências conferidas a olho;
6. sha256 dos 36 arquivos DEPOIS (têm de ser iguais aos de antes).

Tempos do passo 4 medidos em duas calibrações (`_audit/out/m02/calib4`, `calib4b`): texto 1 na tela
de 53 a 57 s, texto 2 de 65 a 69 s; o teste tira 3 capturas por texto e usa a melhor. A comparação
exige ao menos 300 pixels de texto dos dois lados (duas telas sem texto não contam como iguais).

### Execuções (2026-11-10)

| Execução | Pasta (`_audit/out/m02/`) | Resultado |
|---|---|---|
| gravação das referências e da linha de base (`-GravarReferencias -GravarLinhaDeBase`) | `teste-ref-20261110` | PASS; passo 5 = referências gravadas e conferidas a olho |
| regressão | `teste-regressao-20261110` | **PASS 7/7**; 8/8 telas com IoU 0,958–1,0 |
| final (ferramentas na versão do commit) | `teste-final-20261110` | **PASS 7/7** (§6.1) |
| rodada 2: gravação só das referências novas (`-GravarReferenciasNovas`) | `../m02r2/teste-ref-r2` | PASS; 8 telas antigas PASS, 6 referências novas gravadas e conferidas a olho |
| rodada 2: regressão (exe recompilado pelo Craft, `e69cd4ad…`) | `../m02r2/teste-regressao-r2` | **PASS 8/8**; 14/14 telas |
| rodada 2: final antes da descompilação | `../m02r2/teste-final-r2` | **PASS 8/8**; 15/15 comparações |
| rodada 2: final com a verificação L1 (port recompilado, ferramentas na versão do commit) | `../m02r2/teste-final-r2b` | **PASS 8/8**; 15/15 comparações; L1 PASS (§6.2) |

### 6.1 Rodada final (14:49–14:52, exe sha256 `e2cb7d92…`) — **PASS 7/7**

| Passo | Resultado |
|---|---|
| 1 inspeção estática antes | PASS: 16 PASS, 0 FAIL, 3 PENDENTE (os 3 pendentes precisam do jogo rodando e são cobertos no passo 2) |
| 2 abertura com rastro + inspeção completa | PASS: código 0, 29 s, 830 quadros; **28 PASS, 0 FAIL, 0 PENDENTE** (inclui M1, R1–R5 e B1 = 0 diferenças) |
| 3 menus com controle virtual | PASS: 8/8 apertos entregues ao jogo, 3 capturas |
| 4 seleção e introdução | PASS: 3/3 apertos entregues, 7 capturas |
| 5 regressão visual | PASS 8/8 telas: IoU 0,965–1,0 (mínimo 0,75), 636–82.959 pixels de texto |
| 6 arquivos do jogo | PASS: 36/36 com o mesmo sha256 antes e depois |

Evidências versionadas: `docs/evidencias/m02/resultado_teste_final.json`,
`relatorio_traducao_final.json`, `rastro_leituras_final.json`, `resultado_teste_negativo.json`,
`calibracao_dialogo.json`, `capturas_sha256.json` (hash de cada captura, que fica fora do Git) e
a linha de base `linha_de_base_traducao.json`. Logs completos em `_audit/out/m02/teste-final-20261110*`.

### Verificador do Craft Anything (`rebuild_check`, depois da rodada final)

build **PASS** (`npm run build` → CMake/Ninja real, com o código desta missão); captura **PASS**
(o jogo abre e desenha em 1280×720); autoteste andar/ação/interface **FALHA**, classificado como
**BLOQUEADO** nesta missão (exige instrumentar a jogabilidade; nada foi forjado).

### Exibição PT-BR no jogo (capturas reais, 1280×720, fora do Git)

| Tela | Texto conferido na captura |
|---|---|
| abertura | PRESSIONE START PARA JOGAR / © 2009 MICROSOFT - JOAOHRS2. / TODOS OS DIREITOS RESERVADOS. |
| menu principal | JOGAR / CLASSIFICAÇÕES / CONQUISTAS / AJUDA E OPÇÕES / VOLTAR À BIBLIOTECA DE JOGOS / TROCAR DISPOSITIVO DE ARMAZENAMENTO / SELECIONAR |
| ajuda e opções | COMO JOGAR / CONTROLES / CONFIGURAÇÕES / CRÉDITOS / VOLTAR |
| configurações | VOLUME DA MÚSICA / VOLUME DOS EFEITOS SONOROS / RESTAURAR CONFIGURAÇÕES PADRÃO / INVERTER EIXO Y : NÃO / AUMENTAR VOLUME / DIMINUIR VOLUME |
| seleção de jogo | PRESSIONE (A) PARA INICIAR O JOGO. / JOGO 1: VAZIO / SELECIONAR / VOLTAR |
| introdução 1 (db360) | DOIS ANOS TEM SE PASSADO DESDE QUE GRUNTILDA A BRUXA FOI DERROTADA POR BANJO E KAZOOIE . (+ PULAR) |
| introdução 2 (db360) | APÓS CAIR DO TOPO DE SUA TORRE ELA FOI ENTERRADA ABAIXO DO SOLO ONDE PERMANECE ATÉ O DIA DE HOJE . . . |
| pausa (START, no jogo) — rodada 2 | CONTINUAR JOGO / VER TOTAIS / AJUDA E OPÇÕES / CLASSIFICAÇÕES / CONQUISTAS / SAIR DO JOGO / SELECIONAR / VOLTAR |
| pausa → VER TOTAIS | OBJETOS E ITEMS / VER TOTAIS (grafia "ITEMS" é do tradutor; não alterada) |
| pausa → AJUDA E OPÇÕES | COMO JOGAR / CONTROLES / CONFIGURAÇÕES |
| pausa → CLASSIFICAÇÕES (mensagem do sistema) | ESTE RECURSO EXIGE UM PERFIL DE JOGADOR CONECTADO À XBOX LIVE / VOLTAR |
| pausa → SAIR DO JOGO | SAIR DO JOGO / VOCÊ TEM CERTEZA? / SIM / NÃO |
| fala do Bottles (db360, no jogo) | TERRA. PRESSIONE (ícone B) PARA INTERAGIR COM ELES |

### 6.2 Rodada 2 — parte jogável (20:45–20:50, exe sha256 `3e3af0c2…`) — **PASS 8/8**

Tempos medidos em seis execuções de calibração (`_audit/out/m02r2/exp1`–`exp6`, capturas a cada
0,5–3 s com `_audit/m02/explorar_jogo.ps1`): B em 58 s pula a introdução e o jogo começa em Spiral
Mountain por volta de 62 s; o START só abre a pausa sem diálogo na tela e alterna abrir/fechar, por
isso vai em 64 s, antes da primeira fala do Bottles (~66 s). Com a pausa aberta o jogo para, então os
tempos dos submenus ficam fixos. Cada tela tem 3 capturas e vale a melhor; a fala do Bottles rola
enquanto é escrita, então tem 21 capturas (a cada 0,25 s).

| Passo | Resultado |
|---|---|
| 1 inspeção estática antes | PASS: 17 PASS (inclui L1), 0 FAIL, 3 PENDENTE (cobertos no passo 2) |
| 2 abertura com rastro + inspeção completa | PASS: código 0, 30 s, 830 quadros; 29 PASS (inclui L1), 0 FAIL, 0 PENDENTE |
| 3 menus com controle virtual | PASS: 8/8 apertos entregues |
| 4 seleção e introdução | PASS: 3/3 apertos entregues |
| 4b parte jogável | PASS: 19/19 apertos entregues, 37 capturas gravadas, o jogo saiu sozinho (código 0) |
| 5 regressão visual | PASS 15/15: IoU da tela 0,958–1,0 e pior linha 0,952–1,0 (mínimos 0,75); 595–82.959 pixels de texto |
| 6 arquivos do jogo | PASS: 36/36 com o mesmo sha256 antes e depois |

Telas novas (IoU da tela / pior linha): pausa 1,0/1,0 (6 linhas); VER TOTAIS 1,0/1,0; AJUDA E OPÇÕES
1,0/1,0; mensagem do sistema 1,0/1,0 até x=680 e 1,0/1,0 no fim das duas linhas (x 820–890); SAIR DO
JOGO 1,0/1,0; fala do Bottles 0,999 (por linha não se aplica: o texto rola). Nas duas execuções
anteriores (`teste-regressao-r2`, `teste-final-r2`) as telas novas deram 1,0/1,0 e o Bottles 0,967 e
0,970. O executável foi recompilado duas vezes (pelo verificador do Craft e pelo `npm run build`
desta rodada), sem mudança de código, e as telas não mudaram.

A verificação **L1** (regra real de idioma, §3) passou nas duas inspeções: `user_language=1` (padrão
do runtime; não há `banjotooie.toml` ao lado do exe) → código interno 0 → bloco 0 (PT-BR).

Evidências: `docs/evidencias/m02/resultado_teste_final_r2.json`, `relatorio_traducao_final_r2.json`
(as 29 verificações, com a L1), `resultado_teste_regressao_r2.json`, `resultado_teste_referencias_r2.json`,
`resultado_teste_negativo_r2.json`, `capturas_sha256_r2.json` (51 imagens, que ficam fora do Git).
Logs em `_audit/out/m02r2/*.log`.

## 7. Teste negativo — `python -I tools\m02\teste_negativo.py`

Estraga **cópias** dos arquivos (numa pasta nova em `_audit/out/m02/negativo-20261110`; rodada 2:
`_audit/out/m02r2/negativo-final-r2b`, sobre as capturas da rodada final) e confere que o verificador
acusa cada defeito. Rodada 1: **PASS 11/11**. Rodada 2: **PASS 22/22** (os 11 de antes + os 11 abaixo).

| Caso | Defeito plantado | Acusado por |
|---|---|---|
| controle | nenhum | nada falha (correto) |
| string cortada | um 0 no meio de uma frase | X5 (+B1, T2) |
| texto perdido | uma frase esvaziada | X8 (+B1, X7) |
| marcador perdido | `%d`/`~` removido | X9, B1 |
| tamanho inconsistente | tamanho do bloco PT-BR errado no cabeçalho | X2 (+X4, X7, X9, B1) |
| acento trocado | uma letra acentuada virou sem acento (mesmo tamanho) | B1 (só a linha de base percebe) |
| caractere sem glifo | caractere que a fonte não tem | T2, B1 |
| diálogo cortado | um 0 no meio de uma fala do db360 | D3, B1 |
| 3 telas trocadas | captura de outra tela no lugar da certa | regressão visual (IoU 0,0–0,33 < 0,75) |
| idioma alemão no runtime (rodada 2) | `banjotooie.toml` ao lado do exe com `user_language = 3` | L1: código interno 3 → bloco 2 (alemão) |
| idioma inválido no runtime (rodada 2) | `user_language = 0` | L1: inválido, o jogo usaria a região do console |
| 3 controles visuais (rodada 2) | captura certa da pausa e da mensagem (duas regiões) | nada falha (IoU 1,0) |
| item de menu apagado (rodada 2) | 4ª linha da pausa (CLASSIFICAÇÕES) pintada de preto | **só a nota por linha**: tela 0,870 (passaria), linha 0,0 |
| trecho de linha apagado (rodada 2) | meio da 2ª linha da mensagem do sistema apagado | **só a nota por linha**: tela 0,841 (passaria), linha 0,668 |
| fim de frase cortado (rodada 2) | últimas letras de "XBOX LIVE" apagadas | tela 0,671, linha 0,380 (região x 820–890) |
| 2 telas trocadas (rodada 2) | pausa no lugar de AJUDA E OPÇÕES; SAIR DO JOGO no lugar da mensagem | IoU 0,170 e 0,015 |
| fala ausente (rodada 2) | tela sem a fala do Bottles | 161 pixels de texto (< 300) e IoU 0,007 |

Os dois casos marcados "só a nota por linha" mostram por que ela foi criada: com a regra da rodada 1
(só a tela inteira ≥ 0,75), um item de menu sumido ou um trecho de frase apagado **passaria**.

## 8. Matriz da tradução PT-BR

| Item | Estado | Evidência / causa |
|---|---|---|
| Arquivos traduzidos identificados e carregados pelo port | IMPLEMENTADO | rastro de E/S (R1–R5) |
| Integridade estrutural do X360_strings (camada XBLA) | IMPLEMENTADO | X1–X9 PASS |
| Fonte da camada XBLA cobre os acentos | IMPLEMENTADO | T1–T2 PASS |
| Integridade estrutural dos diálogos (db360) | IMPLEMENTADO | D1–D6 PASS, 2.356/2.356 |
| Texto PT-BR intacto na memória do jogo | IMPLEMENTADO | M1 PASS |
| Exibição PT-BR no jogo: abertura, menus XBLA, seleção, introdução | IMPLEMENTADO | 8 telas, regressão visual PASS |
| Exibição PT-BR no jogo: menu de pausa (START), VER TOTAIS, AJUDA E OPÇÕES, confirmação SAIR DO JOGO | IMPLEMENTADO (rodada 2) | 4 telas no jogo real, regressão visual PASS com nota por linha |
| Exibição no jogo de uma fala de fase com ícone de botão (Bottles) | IMPLEMENTADO (rodada 2) | 1 fala, IoU 0,999 |
| Testes automatizados de integridade, checksums e regressão | IMPLEMENTADO | `tools/test_traducao.ps1` (8 passos, 15 comparações), linha de base, verificação L1 de idioma, teste negativo 22/22 |
| Correções de leitura/extração | NÃO NECESSÁRIO | nenhuma incompatibilidade comprovada |
| Exibição no jogo dos demais diálogos e textos das fases | PENDENTE | exige jogar além do início de Spiral Mountain (andar, falar com personagens); a estrutura dos 2.356 registros é conferida estaticamente |
| Interface durante o jogo (HUD) | PARCIAL | vista nas capturas (favos de mel, contadores, ícones, SELECIONAR/VOLTAR) mas é quase só números e ícones; não há regressão própria |
| Glifos raros na fonte dos diálogos (bitmap dentro das texturas), ex. Û, Â, º | PENDENTE | a fonte dos diálogos é textura; no jogo foram vistos Ó, É (introdução) e À, Ê, Ã, Ç, Õ (camada XBLA) |
| Textos desenhados em texturas (`db360.textures.cmp`) | PENDENTE | o arquivo foi modificado pelo tradutor e é lido por inteiro, mas o conteúdo das texturas não foi decodificado |
| Mensagens do sistema na tela | PARCIAL (rodada 2) | 1 vista e conferida (perfil Xbox LIVE, pela pausa); as de armazenamento/erro não foram provocadas; estrutura das 66 UTF-16 PASS |
| PT-BR com outro `user_language` | RISCO (vigiado pela L1, rodada 2) | regra real do jogo (`0x8227D2D0`/`0x8227D9F0`): 1 e 7–12 → PT-BR; 2–6 → JA/DE/FR/ES/IT; 0 ou > 12 → região do console. A L1 reprova um `banjotooie.toml` que troque o idioma |
| Autoteste do verificador do Craft (andar/ação/interface) | BLOQUEADO | exige instrumentar a jogabilidade; fora do escopo desta missão |

## 9. Riscos e pendências

- **`user_language`:** se alguém configurar outro idioma no runtime (`banjotooie.toml` ou linha de
  comando), o jogo mostra outro idioma. Pelo código do jogo (rodada 2), isso só acontece com
  japonês, alemão, francês, espanhol ou italiano (2–6) ou com valor inválido (0 ou > 12, que faz o
  jogo usar a região do console); 1 e 7–12 mantêm o PT-BR. Hoje nada configura isso, e a verificação
  **L1** agora reprova se o `banjotooie.toml` ao lado do exe trocar o idioma (a linha de comando não
  é conferida). Uma trava no port (forçar 1) seria uma mudança de comportamento e fica como sugestão
  para uma missão futura.
- **Tempos do controle virtual:** os passos 3–4 dependem do tempo do jogo (medido duas vezes, com
  1,5 s de folga). Um PC muito mais lento pode deslocar as telas; o teste então falha (não passa
  por engano).
- **Capturas de referência** são conteúdo do jogo: ficam em `_audit/out/m02/referencias` (fora do
  Git). Para regravar: `pwsh -File tools\test_traducao.ps1 -GravarReferencias` e conferir as imagens;
  para gravar só as que faltam: `-GravarReferenciasNovas`.
- **Passo 4b (rodada 2):** depende do tempo do jogo e de um START fora de diálogo (a pausa alterna
  abrir/fechar). Num PC mais lento a pausa pode não abrir; o passo então falha (não passa por engano).
  O roteiro só **abre** a confirmação de SAIR DO JOGO e responde B (NÃO); nunca confirma a saída.
- **Parte não comparada automaticamente:** na mensagem do sistema, "ERFIL" e "XBOX L" ficam atrás do
  fantasma do Bottles (que brilha em branco e varia entre execuções); essa parte foi conferida a olho
  na referência, e o começo (até x=680) e o fim das duas linhas (x 820–890) são comparados.
- **Saídas do teste com conteúdo do jogo:** cada execução grava em `-OutDir` cópias da memória do
  jogo (`db_mem.bin`, 50 MB) e os textos completos extraídos. Uma execução interrompida de outra
  ferramenta (Codex, 16:28–16:45) usou uma pasta dentro de `docs/evidencias/m00-atual-20261110/`;
  nada disso foi commitado, a pasta foi mantida como estava e o `.gitignore` agora impede que esses
  arquivos entrem no Git. Recomenda-se usar sempre `_audit/out` como `-OutDir` (é o padrão).
- **Understory:** MCP indisponível nesta sessão (BLOCKED_MCP); registros locais em
  `.understory/queue/pending/m02-traducao-2026-11-10.json` e `m02-traducao-rodada2-2026-11-10.json`
  (SYNC_PENDING).
