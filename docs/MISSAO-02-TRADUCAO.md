# MISSÃO 02 — Integridade da tradução PT-BR (relatório)

Data: 2026-11-10. Branch `missao-02-traducao`. Escopo: preservar os textos e recursos do Banjo-Tooie
XBLA **com a tradução PT-BR já aplicada pela pessoa**, sem retraduzir, sem usar outra tradução e
sem publicar conteúdo do jogo. Nenhum arquivo do jogo foi alterado (36/36 sha256 iguais antes e
depois de cada teste).

**Resultado em uma frase:** a tradução está íntegra em tudo o que foi possível medir; não foi
encontrada nenhuma incompatibilidade de leitura/extração, então **nenhuma correção nos dados ou no
port foi necessária**. O que foi feito é a **automação** (inspeção, linha de base, regressão visual
no jogo real, teste negativo) e as **opções de teste** do port que a tornam possível.

## 1. Ponto de restauração (rollback)

| O quê | Referência |
|---|---|
| Instantâneo de toda a árvore antes de qualquer mudança | `refs/checkpoints/missao-02-inicio-20261110` = `3590fff` |
| Base limpa (trabalho anterior sem commit, sem mudança de conteúdo) | commit `97232bf` |
| Trabalho desta missão | commit seguinte no branch `missao-02-traducao` |

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

## 6. Teste automático — `pwsh -File tools\test_traducao.ps1`

Sem ação manual. A janela abre atrás das outras, sem som e sem roubar o foco; cada execução usa
pastas de usuário e cache próprias dentro de `_audit/out`.

1. inspeção estática + regressão contra a linha de base (inclui o sha256 dos 36 arquivos ANTES);
2. jogo real até a abertura com rastro de E/S, cópia do db360 e da memória com o texto → inspeção
   completa (§3);
3. jogo real com controle virtual: abertura → START → menu principal → AJUDA E OPÇÕES → CONFIGURAÇÕES;
4. jogo real: menu → JOGAR → seleção de jogo → introdução com dois textos do db360;
5. regressão visual do texto de 8 telas contra referências conferidas a olho;
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

## 7. Teste negativo — `python -I tools\m02\teste_negativo.py`

Estraga **cópias** dos arquivos (numa pasta nova em `_audit/out/m02/negativo-20261110`) e confere
que o verificador acusa cada defeito. **PASS 11/11:**

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

## 8. Matriz da tradução PT-BR

| Item | Estado | Evidência / causa |
|---|---|---|
| Arquivos traduzidos identificados e carregados pelo port | IMPLEMENTADO | rastro de E/S (R1–R5) |
| Integridade estrutural do X360_strings (camada XBLA) | IMPLEMENTADO | X1–X9 PASS |
| Fonte da camada XBLA cobre os acentos | IMPLEMENTADO | T1–T2 PASS |
| Integridade estrutural dos diálogos (db360) | IMPLEMENTADO | D1–D6 PASS, 2.356/2.356 |
| Texto PT-BR intacto na memória do jogo | IMPLEMENTADO | M1 PASS |
| Exibição PT-BR no jogo: abertura, menus XBLA, seleção, introdução | IMPLEMENTADO | 8 telas, regressão visual PASS |
| Testes automatizados de integridade, checksums e regressão | IMPLEMENTADO | `tools/test_traducao.ps1`, linha de base, teste negativo 11/11 |
| Correções de leitura/extração | NÃO NECESSÁRIO | nenhuma incompatibilidade comprovada |
| Exibição no jogo dos demais diálogos, HUD, menu de pausa (START) e textos das fases | PENDENTE | exige jogar além da introdução; a estrutura de todos os 2.356 registros já é conferida estaticamente |
| Glifos raros na fonte dos diálogos (bitmap dentro das texturas), ex. Û, Â, º | PENDENTE | a fonte dos diálogos é textura; só os acentos da introdução (Ó, É) foram vistos no jogo |
| Textos desenhados em texturas (`db360.textures.cmp`) | PENDENTE | o arquivo foi modificado pelo tradutor e é lido por inteiro, mas o conteúdo das texturas não foi decodificado |
| Mensagens do sistema (66 UTF-16, ex. armazenamento/erro) na tela | PENDENTE | estrutura PASS; não foram provocadas no teste |
| PT-BR com outro `user_language` | RISCO | o PT-BR depende do padrão 1; `user_language=3` mostra alemão |
| Autoteste do verificador do Craft (andar/ação/interface) | BLOQUEADO | exige instrumentar a jogabilidade; fora do escopo desta missão |

## 9. Riscos e pendências

- **`user_language`:** se alguém configurar outro idioma no runtime (`banjotooie.toml` ou linha de
  comando), o jogo mostra outro idioma. Hoje nada configura isso. Uma trava no port (forçar 1) seria
  uma mudança de comportamento e fica como sugestão para uma missão futura.
- **Tempos do controle virtual:** os passos 3–4 dependem do tempo do jogo (medido duas vezes, com
  1,5 s de folga). Um PC muito mais lento pode deslocar as telas; o teste então falha (não passa
  por engano).
- **Capturas de referência** são conteúdo do jogo: ficam em `_audit/out/m02/referencias` (fora do
  Git). Para regravar: `pwsh -File tools\test_traducao.ps1 -GravarReferencias` e conferir as imagens.
- **Understory:** MCP indisponível nesta sessão (BLOCKED_MCP); registro local em
  `.understory/queue/pending/m02-traducao-2026-11-10.json` (SYNC_PENDING).
