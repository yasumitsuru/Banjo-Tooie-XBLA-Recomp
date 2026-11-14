# Plano do projeto — Banjo-Tooie XBLA PT-BR Recompiled

**Atualizado em 2026-11-13.** Este é o plano vivo: registra o que está provado, o que falta e a
ordem de trabalho. Esta versão incorpora:

- a **auditoria somente leitura de 2026-11-13** (97 funcionalidades avaliadas; resultado na
  [matriz de estado](#matriz-de-estado-auditoria-somente-leitura-de-2026-11-13));
- a **missão nova de 2026-11-13**, "Correção Integral, Estabilização e Validação Automatizada",
  chamada aqui de **M04** (o texto da pessoa não traz número). Texto completo:
  [`MISSAO-04-CORRECAO-INTEGRAL.md`](MISSAO-04-CORRECAO-INTEGRAL.md).

**Andamento da M04 (2026-11-13):** etapas **A e B concluídas** (ver [Andamento](#andamento));
etapas C a F pendentes.

## Em poucas palavras

- **Pronto e provado (24 de 97):**
  - port nativo D3D12, sem emulador;
  - abertura até o jogo livre;
  - menu principal e pausa, com CONFIGURAÇÕES DE PC dentro de AJUDA E OPÇÕES;
  - 4K interno (3840×2160) a 60 FPS no início de Spiral Mountain, medido por até 30 s;
  - tradução íntegra: 36/36 arquivos iguais à linha de base em 2026-11-13 e 15 telas PT-BR
    conferidas;
  - saves: gravação, carga e backup, uma pasta por conta em Documentos\My Games, testes
    separados dos saves da pessoa.
- **Com defeito na auditoria (3; C-75 corrigido na etapa B):**
  - save inválido apagado sem cópia ao restaurar (C-75);
  - detecção automática de resolução pega o monitor errado (C-29);
  - CONQUISTAS chama uma função do Xbox que não existe no PC (C-6).
- **O resto** está distribuído nas etapas da M04 abaixo: 34 sem validação, 8 parciais,
  13 não implementados e 15 bloqueados ou não verificáveis.
- **M03** (matriz 3 resoluções × 3 modos, hoje 0/9) deixa de ser missão separada. Entra na
  etapa D da M04, com os mesmos critérios.
- **Maior risco técnico:** o 60 FPS segue o método do patch Xenia (a lógica roda a 60 com 17
  constantes trocadas). Nada mediu ainda se física, cronômetros e minijogos ficaram na velocidade
  original.

## Missão ativa — M04: correção integral, estabilização e validação automatizada

### Regras que valem em toda a M04

1. **Só a versão PT-BR** (`W:\Decomp\Banjo 3\Banjo Tooie BR`). A global
   (`Banjo-Tooie (World) (XBLA)`) só entra para conferir que o código compartilhado não regrediu.
2. **Tradução preservada:** nada de reconstruir nem trocar textos. Os 36 arquivos são conferidos
   contra `_audit/mission00/assets/inventory.json` no início e no fim de cada etapa.
3. **Saves da pessoa intocáveis:** `Documentos\My Games\Banjo-Tooie XBLA` e as cópias antigas em
   `out\craft-runtime\user` e `Documentos\banjotooie`. Todo teste usa pasta isolada. Antes e
   depois de cada bateria, a pasta da pessoa é fotografada (lista de arquivos + SHA-256).
4. **Edição de saves autorizada, só em cópias de teste:** mundos, habilidades, colecionáveis,
   saves de início/meio/fim/quase 100% e corrupção simulada. Cada save de teste guarda a origem e
   a transformação feita. Saves da internet só de origem verificável; preferir os gerados pelo
   próprio projeto.
5. **Menu fixo:** START → AJUDA E OPÇÕES → CONFIGURAÇÕES DE PC, posição aprovada pela pessoa.
   Não mover nem redesenhar.
6. **Fora da missão:** DLSS, DLAA, geração de quadros e ultrawide.
7. **Prova antes de "pronto":**
   - build não é prova;
   - controle virtual não prova controle físico;
   - as tolerâncias de cada teste ficam justificadas no próprio resultado;
   - requisito sem hardware ou sem cenário fica BLOQUEADO, nunca "concluído".
8. **Git:** checkpoint (`refs/checkpoints/m04-<etapa>`) antes de cada etapa e commit local por
   correção. Nada remoto sem pedido da pessoa.
9. **Verificação do Craft** (`--self-test`, limite de 80 s): continua passando em toda etapa.
   As baterias longas ficam em sondas e scripts próprios.

### Pastas da M04

| O quê | Onde |
|---|---|
| Jogo PT-BR (alvo) | `W:\Decomp\Banjo 3\Banjo Tooie BR` |
| Saves e opções da pessoa (nunca usar em testes) | `Documentos\My Games\Banjo-Tooie XBLA` |
| Verificações do Craft | `out\craft-check\user` (esvaziada a cada execução) |
| Saves de teste e resultados da M04 | `_audit\out\m04\<etapa>\` (fora do Git: contém dados do jogo) |
| Jogo / lançador do Craft / seletor de versão | `out\native\banjotooie.exe` / `Build\banjotooie.exe` / `Seletor\BanjoTooie_Seletor.exe` |

### Etapas, em ordem de dependência

Os códigos C-nn são os da [matriz de estado](#matriz-de-estado-auditoria-somente-leitura-de-2026-11-13).
Uma etapa só avança para o que depende dela depois que suas mudanças críticas estiverem provadas.

#### Andamento

| Etapa | Estado | Prova |
|---|---|---|
| A — Preparação | **FEITA** (2026-11-13) | checkpoint `refs/checkpoints/m04-inicio` (app `423524b`, rexglue-sdk `88a105a`); 36/36 arquivos PT-BR iguais; foto da pasta da pessoa (15 arquivos); alvo PT-BR confirmado no log ("JOGAR"). Saídas em `_audit/out/m04/A/` |
| B — Segurança dos saves | **FEITA** (2026-11-13) | `python -I tools/test_saves.py --corpus _audit/out/m04/B/corpus-reais`: **18/18 PASS** (`_audit/out/m04/B/run-20261113-205157/resultado.json`); 207 saves reais aceitos e 14 estragados recusados pelo código do port; gravação real do jogo com o registro do runtime; pasta da pessoa igual antes e depois. Detalhes abaixo |
| C — Infraestrutura | pendente (C-95 e C-90 já provados na etapa B) | — |
| D, E, F | pendentes | — |

**O que a etapa B mudou** (formato do save em `MECHANICS.md` §19):

| ID | Antes | Agora | Prova (cenário de `tools/test_saves.py`) |
|---|---|---|---|
| C-74 | só tamanho e "não é tudo 0xDE" | as regras do próprio jogo (`pc_save_format.cpp`): globais com versão 9, blocos com "KHJC", seções e versão 0x1F, somas | s03–s06, s15 (207 reais aceitos, 14 estragados recusados) |
| C-75 | save inválido era sobrescrito sem cópia | cópia exata em `backups/corrupt-*.dat` antes de qualquer restauração | s03–s06, s08 (byte a byte) |
| — | a conferência rodava depois de o jogo começar (corrida com a leitura do jogo) | roda antes de o jogo começar (`OnPreLaunchModule`) | todos |
| C-72 | rodízio por quantidade: arquivos inválidos empurravam os bons para fora | conta só os válidos; quando uma gravação tem menos jogos, a anterior vai para `keep-*` (fora do rodízio) | s11, s12, s13 |
| C-76 | o runtime apagava o save e criava outro: uma queda no meio deixava sem save | o runtime guarda o conteúdo anterior em `savegame.dat.port-prev` até fechar o arquivo novo (rexglue-sdk); a abertura usa essa cópia se ela sobrou; o port escreve o save sempre por arquivo temporário + troca | s16, s17, s18 (gravação real) |
| C-73, C-77 | só "restaurar o último" | linha RESTAURAR : <data> no menu do jogo: esquerda/direita escolhem o backup, A confirma, A restaura; o save atual é guardado antes | s14 (pela mesma linha do menu) |
| — | save preso por outro programa na abertura era tratado como estragado e sobrescrito | espera ~2 s e, se continuar preso (ex.: OneDrive), não mexe em nada; cópias de conflito ao lado do save vão para o log | s09 |
| C-95, C-90 | sonda de save avançado quebrada desde a reforma da pausa (contava 5 toques até SAIR DO JOGO) | a sonda acha a entrada pelo texto; save com 100% dos colecionáveis gravado pelo jogo e carregado pelo menu: **54 contagens do jogo = rastreador, 0 diferenças**, 59,6 FPS | s18 + `--probe-advanced-save` (`_audit/out/m04/B/probe-advanced/load.json`); save guardado em `_audit/out/m04/saves/` |

#### Etapa A — Preparação

| Passo | Pronto quando |
|---|---|
| Checkpoint Git (inclui o submódulo `rexglue-sdk`) | ref `refs/checkpoints/m04-inicio` criado e `git status` registrado |
| Linha de base dos 36 arquivos PT-BR | 36/36 iguais ao inventário |
| Foto da pasta de saves da pessoa | lista + SHA-256 guardadas em `_audit\out\m04\A\` |
| Ambiente de testes isolado | toda sonda roda com `--user_data_root` dentro de `_audit\out\m04\` ou pelo lançador (pasta de verificação) |
| Confirmar o alvo PT-BR | o relatório de boot mostra a pasta `Banjo Tooie BR` e o texto 0 = "JOGAR" |

#### Etapa B — Segurança dos saves

| ID | O que fazer | Pronto quando |
|---|---|---|
| C-75 | guardar sempre uma cópia `corrupt-…`, byte a byte, antes de sobrescrever um save inválido | teste negativo com save truncado mostra a cópia e o backup restaurado |
| C-74 | descobrir no XEX como o jogo valida o save (soma, cabeçalho, campos) e usar a mesma regra | rejeita todos os saves alterados de propósito; aceita todos os reais e de teste |
| C-72 | retenção que nunca deixa só backups inválidos: manter sempre os últimos válidos pela regra nova | teste com mais gravações que o limite, incluindo inválidas |
| C-76 | proteger a gravação do jogo (escrever em temporário e trocar, ou detectar save parcial na partida) | teste que mata o processo durante a gravação e recupera o save |
| C-73 | restauração pelo menu e na partida | `backups.log` e save restaurado igual ao backup |
| C-77 | escolher um backup anterior (não só o último) na página de PC | escolha testada pelo controle virtual |
| — | conflito com sincronização de arquivos (OneDrive) | teste com arquivo bloqueado ou temporário da sincronização não perde o save |
| C-78, C-79 | isolamento e saves por conta continuam valendo | foto da pasta da pessoa igual antes e depois da etapa; 2ª conta do Windows: ver bloqueios |

#### Etapa C — Infraestrutura de testes e diagnóstico

| ID | O que fazer | Pronto quando |
|---|---|---|
| C-10 | log de sessão completo (proposta na seção [Sistema de log completo](#sistema-de-log-completo-item-2-pedido-de-2026-11-11-ainda-não-implementado)): versão do build, API gráfica, GPU, mapas, saves, erros, desempenho | sonda confere cabeçalho, eventos e bloco de desempenho; teste negativo |
| C-95, C-70 | saves de teste editados (início, meio, fim, quase 100%), cada um com origem e transformação registradas | `--make-advanced-save` + `--probe-advanced-save` passam neste build com os 4 estágios |
| C-94 | viagem automática a todos os mundos e áreas relevantes (rotina de viagem do jogo, `0x82248810`) | sonda chega a cada mapa da lista e registra o jogo livre |
| C-97 | bateria por mundo reaproveitando as sondas existentes (`--probe-*` em `pc_automation.cpp`) | um comando roda a bateria e grava JSON + capturas por cenário |
| C-7 | executor de sessão longa (≥ 1 h): FPS, tempo de quadro, memória, CPU/GPU, travamentos, exceções | relatório de uma sessão de 1 h |

#### Etapa D — Estabilização (60 FPS, vídeo, distância de desenho)

| ID | O que fazer | Pronto quando |
|---|---|---|
| C-32 | comparar 30 × 60 FPS com a mesma entrada: andar, pular, nadar, voar, colisões, câmera, animações | diferenças dentro de tolerância justificada em pelo menos 3 mundos; corrigir constantes ou ganchos onde não bater |
| C-36 | cronômetros, minijogos, chefes, inimigos | tempos iguais aos de 30 FPS |
| C-33 | cenas a 30 com tela a 60 em todo o jogo, e transições entre mundos | duração das cenas igual à original |
| C-35 | sincronia de áudio e vídeo | defasagem medida abaixo do limite |
| C-31 | 60 FPS sustentado por mundo e nas cenas, a 4K | p95 do tempo de quadro dentro do limite em cada mundo |
| C-34 | separar lógica e render só se C-32/C-36 mostrarem necessidade | decisão registrada com as medidas |
| C-22 | 1080p hoje renderiza a 1280×720: achar solução sem perder qualidade (ex.: escala 2× reduzida para 1080p) | medida mostra imagem interna de pelo menos 1080 linhas e saída 1920×1080 |
| C-23, C-21, C-24, C-27 | matriz automática 3 resoluções × 3 modos, com reabertura e persistência (critérios da M03, histórico abaixo) | 9/9, com interno, framebuffer, apresentação, janela e monitor medidos separadamente |
| C-25 | janela sem bordas ocupando o monitor escolhido inteiro | cliente = monitor em cada resolução |
| C-26 | regra para janela maior que a área útil (4K num monitor 4K) | regra documentada e medida |
| C-29 | detectar o monitor certo; vários monitores; escala DPI | teste com 2 monitores (se houver) e DPI 100% e 150% |
| C-28 | VSync sem tearing, incluindo monitores que não são de 60 Hz | confirmação visual da pessoa + medida |
| C-42–C-45 | distância de desenho: entrar nos mundos, câmera em pontos fixos, 3 perfis, capturas, objetos sumindo, pop-in, transparências, FPS, persistência; Total = maior alcance seguro validado; rever o perfil "Longe" (2× o original, criado pelo projeto) | sonda por mundo ESTÁVEL nos 3 perfis neste build |
| C-46–C-48, C-50 | FXAA, MSAA 2×, anisotropia: prova visual e de desempenho | capturas comparadas; valores sobrevivem ao reinício |

#### Etapa E — Funcionalidades

| ID | O que fazer | Pronto quando |
|---|---|---|
| C-38, C-52–C-56 | controle físico: botões, direcional, analógicos, gatilhos, vibração, zona morta, sensibilidade, inversão, reconexão e desconexão, menus | sonda com controle real conectado; sem controle = BLOQUEADO |
| C-57–C-61 | remapeamento completo: teclado (com gatilhos, LB/RB, Back, L3/R3), mouse e controle; aviso de conflito; restaurar padrões | cada ação remapeada vale no jogo e persiste |
| C-65–C-67 | áudio: geral, silenciar, música e efeitos. Investigar antes os do menu original (textos 10 e 11) para não duplicar; sem cortes | nível medido por canal; persistência |
| C-62–C-64 | câmera: sensibilidade, inversão e suavização (mouse e controle), nos modos de câmera do jogo | rotação medida por ajuste; nos valores padrão = original |
| C-82, C-81, C-83 | opções num só lugar por conta, sem valor duplicado; recuperação pelo `.bak`; migração; restaurar padrões | teste com arquivo inválido, versão antiga e duas contas |
| C-84, C-85, C-86 | início rápido; pular cenas já vistas sem perder eventos | testes sem softlock com os saves de teste |
| C-91 | pausa ao minimizar ou perder o foco, e retomada (hoje o observador fica desligado nos testes automáticos) | sonda de foco passa |
| C-92, C-93 | contador de FPS e estatísticas desenhados; custo medido | captura + medida |
| C-90 | rastreador comparado automaticamente com a tela Totais do jogo | 0 diferenças nos saves de teste |
| C-6 | CONQUISTAS sem chamada vazia: tratamento próprio de PC, resto do menu preservado | nenhuma linha de stub no log; demais entradas iguais |
| C-13–C-17, C-19, C-87, C-88 | tradução e interface: diálogos de todos os mundos, HUD, mensagens, acentos raros, texturas, legibilidade em 1080/1440/4K, velocidade dos diálogos, trava de idioma | capturas com regressão por mundo; nenhuma frase em inglês ou cortada |

#### Etapa F — Homologação

| Passo | Pronto quando |
|---|---|
| Regressão completa: auto-teste, smoke, tradução, teste negativo, sondas, bateria por mundo | todos PASS, ou com bloqueio justificado |
| PT-BR | 36/36 + telas iguais à linha de base |
| Saves de início, meio, fim e quase 100% | carregam, gravam e fazem backup |
| Sessão longa (≥ 1 h) | sem queda e sem crescimento anormal de recursos |
| Relatório final (seção 21 da missão) | em `docs/`, com o JSON das execuções e a lista de commits e checkpoints |

### Bloqueios conhecidos (dependem de algo fora do código)

| Item | Depende de |
|---|---|
| Controle físico (C-38, C-52–C-56) | a pessoa conectar o controle durante as sondas |
| Vários monitores e escala DPI (C-29, parte de C-23) | um segundo monitor; trocar a escala DPI do Windows só com autorização |
| Saves por conta numa 2ª conta do Windows (C-79) | criar ou usar outra conta do Windows: mudança de sistema, pedir antes |
| Confirmação visual de tearing (C-28) | o olho da pessoa |
| Saves avançados da internet | só se os editados pelo projeto não bastarem, e só de origem verificável |

### Fora do escopo da M04

- **Versão global como alvo.** Os textos de PC que aparecem em PT-BR na global (C-18) ficam como
  estão; ela só não pode regredir.
- **AMD CAS/FSR (C-49).** Opcional, só se resolver o 1080p (C-22) sem dependência nova
  injustificada.
- **Remapear botões do mouse (C-58).** Entra junto com C-57–C-61 se houver ação útil; não é
  bloqueante.
- **DLSS, DLAA, geração de quadros e ultrawide.**

## Matriz de estado (auditoria somente leitura de 2026-11-13)

Legenda dos estados:

| Sigla | Estado |
|---|---|
| **OK** | concluído e validado |
| **NV** | implementado, mas não validado |
| **PARCIAL** | parcialmente implementado |
| **NÃO IMPL.** | não implementado |
| **DEFEITO** | com defeito |
| **BLOQ.** | bloqueado |
| **NÃO VERIF.** | não verificável |

A coluna Etapa diz onde a M04 resolve o item. "manter" = já provado; só não pode regredir.

**Total na auditoria:** 24 OK, 34 NV, 8 PARCIAL, 13 NÃO IMPL., 3 DEFEITO, 1 BLOQ., 14 NÃO VERIF.
**Total depois da etapa B (2026-11-13):** 32 OK, 30 NV, 7 PARCIAL, 12 NÃO IMPL., 2 DEFEITO, 1 BLOQ., 13 NÃO VERIF.

**Limite das evidências:** as provas de distância de desenho, controle e save avançado que
existem vêm do projeto vizinho `W:\Decomp\Banjo` (build de 08–09/11) e não valem para este build.
Os testes deste build cobrem só título, introdução e o início de Spiral Mountain (mapa 0xA1).

| # | Funcionalidade | Estado | Etapa |
|---|---|---|---|
| 1 | Executável nativo Win11 x64, sem emulador | OK | manter |
| 2 | Renderizador D3D12 | OK | manter |
| 3 | Inicialização até o jogo livre | OK | manter |
| 4 | Menu principal (CONFIGURAÇÃO DA RECOMPILAÇÃO, SAIR PARA O DESKTOP com confirmação) | OK | manter |
| 5 | Encerramento limpo (64/64) | OK | manter (repetir em F) |
| 6 | CONQUISTAS (stub do Xbox) | OK (M04-E: lista de conquistas do PC no menu do jogo) | manter |
| 7 | Estabilidade em sessões longas | NÃO VERIF. | C/F |
| 8 | Versão global no mesmo port | OK | só regressão |
| 9 | Seletor PT-BR/Global | OK | manter |
| 10 | Sistema de log completo | OK (M04-C: registro de sessão; falta só o teste com pasta sem permissão) | manter |
| 11 | Integridade dos arquivos PT-BR (36/36) | OK | A e F |
| 12 | PT-BR em abertura, menus, seleção, introdução, pausa (15 telas) | OK | manter |
| 13 | Diálogos das fases | NV | E |
| 14 | HUD | NV | E |
| 15 | Glifos raros (Û, Â, º) | NV | E |
| 16 | Textos em texturas | NÃO VERIF. | E |
| 17 | Mensagens do sistema | NV | E |
| 18 | Textos de PC no idioma do jogo (global) | PARCIAL | fora do escopo |
| 19 | Trava de idioma (`user_language`) | NÃO IMPL. | E |
| 20 | 4K interno 3840×2160 | OK | manter |
| 21 | 1440p interno | OK (M04-D: matriz, imagem interna 2560×1440) | manter |
| 22 | 1080p (hoje interno 1280×720) | OK (M04-D: desenha a 2× = 2560×1440 e reduz para 1920×1080) | manter |
| 23 | Matriz 3 resoluções × 3 modos (M03) | OK (M04-D: 9/9 estados + 9/9 reaberturas; janela 4K num monitor 4K = limite físico, C-26) | manter |
| 24 | Troca de modo sem queda | OK (M04-D: 8 trocas por estado sem queda, 72 no total) | manter |
| 25 | Sem bordas (hoje só a área útil) | OK (M04-D: monitor inteiro, 3 monitores) | manter |
| 26 | Janela em 4K num monitor 4K | OK (regra: área útil em 16:9, 3612×2032 medido) | manter |
| 27 | Proporção em monitor que não é 16:9 | OK (M04-D: ultrawide 2560×1080 sem bordas, imagem confere) | manter |
| 28 | VSync sem tearing | NV | D |
| 29 | Detecção automática da resolução | OK (M04-D: monitor escolhido/cursor; 3 monitores) | manter |
| 30 | 60 FPS no início de Spiral Mountain | OK | manter |
| 31 | 60 FPS sustentado (mundos, cenas, horas) | NÃO VERIF. | D |
| 32 | Física e velocidade iguais às de 30 FPS | PARCIAL (M04-D: andar, correr, pulo, deslize e colisões medidos 30×60; natação/voo não) | D |
| 33 | Cenas a 30 com tela a 60 | NV | D |
| 34 | Separação lógica / render / tempo | NÃO IMPL. | D (decisão) |
| 35 | Sincronia de áudio e vídeo | NÃO VERIF. | D |
| 36 | Cronômetros, minijogos, eventos por quadro | NÃO VERIF. | D |
| 37 | CONFIGURAÇÕES DE PC na pausa nativa (START → AJUDA E OPÇÕES) | OK | manter |
| 38 | Abrir com START num controle físico | NV | E |
| 39 | Entradas originais da pausa | OK | manter |
| 40 | Navegação e retorno (B, F1) | OK | manter |
| 41 | Categorias (5 seções, 51 linhas) | OK | manter |
| 42 | Distância de desenho: perfis e limites por mapa | NV | D |
| 43 | Distância de desenho: pop-in e geometria por mundo | NÃO VERIF. | D |
| 44 | Distância de desenho: custo em FPS a 4K | NÃO VERIF. | D |
| 45 | Distância de desenho: persistência | NV | D |
| 46 | FXAA | NV | D |
| 47 | MSAA 2× | NV | D |
| 48 | Anisotropia | NV | D |
| 49 | AMD CAS/FSR | NÃO IMPL. | opcional |
| 50 | Persistência das opções gráficas | NV | D/E |
| 51 | Detecção de controle físico (SDL, logs de 11–12/11) | OK | manter |
| 52 | Uso do controle físico | NV | E |
| 53 | Zona morta | NV | E |
| 54 | Vibração | NV | E |
| 55 | Reconexão e desconexão | NV | E |
| 56 | Sensibilidade e inversão do analógico direito | NV | E |
| 57 | Remapeamento de teclado (17 ações) | PARCIAL | E |
| 58 | Remapear botões do mouse | NÃO IMPL. | E (não bloqueante) |
| 59 | Remapear botões do controle | NÃO IMPL. | E |
| 60 | Aviso de conflito entre teclas | NÃO IMPL. | E |
| 61 | Restaurar controles padrão | NÃO IMPL. | E |
| 62 | Câmera por mouse/teclado | NV | E |
| 63 | Suavização da câmera no controle | NÃO IMPL. | E |
| 64 | Câmera nos modos de câmera do jogo | NÃO VERIF. | E |
| 65 | Volume geral e SILENCIAR TUDO | NV | E |
| 66 | Música e efeitos separados | NÃO IMPL. | E |
| 67 | Áudio: distorção e cenas | NÃO VERIF. | E |
| 68 | Gravação do save | OK | manter |
| 69 | Carregar save existente | OK | manter |
| 70 | Saves com progresso diferente | PARCIAL (jogo novo, save real com 1 jogo, 100% dos colecionáveis; falta a história avançada) | C |
| 71 | Backups automáticos | OK | manter |
| 72 | Retenção dos backups | OK (M04-B) | manter |
| 73 | Restaurar backup | OK (M04-B) | manter |
| 74 | Proteção contra save corrompido | OK (M04-B) | manter |
| 75 | Guardar o save inválido antes de restaurar | OK (M04-B, corrigido) | manter |
| 76 | Gravação interrompida | OK (M04-B) | manter |
| 77 | Escolher um backup anterior | OK (M04-B) | manter |
| 78 | Saves de teste separados dos da pessoa | OK | manter |
| 79 | Save por conta em Documentos\My Games | OK (conta atual) | B (2ª conta) |
| 80 | `pc_settings.toml` próprio, gravação atômica + `.bak` | OK | manter |
| 81 | Arquivo de opções ausente ou inválido | NV | E |
| 82 | Opções divididas entre conta e máquina | PARCIAL (M04-D: vídeo só na conta; FXAA/MSAA/anisotropia/VSync ainda no arquivo do runtime) | E |
| 83 | Migração de versão do arquivo de opções | OK (M04: versão 3→4 testada com cópia das opções da pessoa) | manter |
| 84 | Início rápido | NV | E |
| 85 | Pular cenas já vistas | NÃO IMPL. | E |
| 86 | Softlocks ao pular | NÃO VERIF. | E |
| 87 | Legibilidade em 1080p, 1440p e 4K | NÃO VERIF. | E |
| 88 | Velocidade dos diálogos | NV | E |
| 89 | Rótulos de PC cabem nas telas do jogo | OK | manter |
| 90 | Rastreador de colecionáveis | OK (M04-B: 0 diferenças com a tela Totais no save 100%) | manter (texto só PT-BR: C-18) |
| 91 | Pausa ao minimizar ou perder o foco | NV | E |
| 92 | Contador de FPS e estatísticas | NV | E |
| 93 | Impacto do monitoramento no desempenho | NÃO VERIF. | E |
| 94 | Viagem automática a mapas | NV | C |
| 95 | Save avançado de teste | OK (M04-B) | manter |
| 96 | Testes automáticos existentes | OK | manter |
| 97 | Testes que faltam (30×60, controle físico, mundos, vídeo, áudio, câmera, saves, cenas, longa duração) | NÃO IMPL. | C/F |

## Linha de base local (início da M04)

| Item | Estado |
|---|---|
| Raiz Git | `W:\Decomp\Banjo 3\app`, branch `port-pc-4k60` |
| Último commit de produto antes da M04 | `b3f46f0` (saves em Documentos\My Games; verificações isoladas) |
| `master` | 23 commits atrás de `port-pc-4k60`; integrar só com OK da pessoa |
| Submódulo `rexglue-sdk` | `88a105a` (`host_vsync`) |
| Arquivos do jogo PT-BR | 36/36 iguais a `_audit/mission00/assets/inventory.json` (recalculado em 2026-11-13) |
| Fora do Git | originais do jogo, capturas, `_audit/out/`, `.understory*`, `docs/evidencias/m00-atual-20261110/` |

As tabelas antigas "Matriz de requisitos" e "Próximos passos" (até 2026-11-12) foram substituídas
pela matriz acima e pelas etapas da M04; a versão anterior está no Git (`b3f46f0:docs/PLANO.md`).

# Histórico (rodadas anteriores à M04)

## Atualização de 2026-11-12 — M00 e M03

> Desde 2026-11-13 a M03 faz parte da etapa D da M04 (itens C-21 a C-29), com os mesmos critérios
> listados abaixo.

- **M00: `PASS_FINAL`**, revisão independente da auditoria e do baseline, não da M03. A revisão confirmou build fresco de 723/723 ações e auto-teste 10/10 com janela visível; captura guest 4K e amostra de 59,4 FPS por 5 s. Isso não comprova 60 FPS sustentados. Os originais 36/36 e os outputs do SDK 18/18 foram conferidos no checkpoint pré-M03; não são hashes novos após os builds M03. Evidência: [relatório M00](../_audit/mission00/REPORT.md).
- **M03: `BLOCKED`, sem `PASS_FINAL` runtime.** Foram alteradas cinco fontes C++ e criado `tools/test_display_modes.py`: janela pós-HWND, aplicação/restauração de modos, sonda `--probe-display`, dimensões separadas, JSON/saídas de falha e runner que exige nove estados iniciais únicos mais nove reaberturas. A escala interna exige reinício; valor salvo não prova aplicação no processo atual.
- O `rebuild_check` oficial executado após esta atualização aprovou o build, captura 3840×2160, core loop do auto-teste (movimento, pulo e HUD) e créditos. A verificação de mechanics encontrou 39 valores com fonte e nenhum estimado, mas declarou a comparação de comportamento incompleta por não haver medições runtime dos valores. Em chamada M03 anterior, o self-test marcou 10/10 e a amostra de 5 s marcou 60 FPS guest/lógica e host (16,67 ± 1,02 ms; máximo 23,3 ms); isso não comprova desempenho sustentado. Nenhum desses checkers chama `--probe-display` ou testa o ajuste inicial físico da janela.
- O gate `M03-GATE-20261112-SOL3` é **PASS somente para revisão estática e consistência documental**. Não é gate runtime. Fontes finais conferidas 7/7 contra o manifesto; backups prepatch 6/6; rollback não executado. Evidência: [relatório consolidado M03](../_audit/mission03/REPORT.md) e [gate Sol3](../_audit/mission03/gate_sol3/REPORT.md).

### Matriz runtime M03 no binário final

| Resolução | Janela | SemBordas | TelaCheia |
|---|---|---|---|
| 1920×1080 | PENDENTE | PENDENTE | PENDENTE |
| 2560×1440 | PENDENTE | PENDENTE | PENDENTE |
| 3840×2160 | PENDENTE | PENDENTE | PENDENTE |

**Aprovadas: 0/9.** A execução antiga `run-20261112T151436Z` cobriu apenas 1080p/Janela e sua reabertura, no EXE anterior ao final. As oito alternâncias não aprovam a matriz: movimento medido foi zero; JSON e `PrintWindow` falharam. Os contratos foram corrigidos em fonte, mas não houve rerun final. Não classificar essa rodada como regressão do baseline nem relaxar os critérios.

### Falta verificar para concluir M03

1. Executar no binário final os nove pares resolução/modo, com nove estados iniciais únicos e nove reaberturas; conferir também `resolution-save` e a persistência depois de iniciar um processo novo.
2. Em cada estado, comparar pedido, modo aplicado e salvo; medir separadamente cliente e retângulo externo da janela, área útil/monitor, resolução guest e backbuffer. Confirmar o limite físico da Janela 4K, que não coube na área útil 3840×2088 observada antes da correção.
3. Provar entrada e movimento depois de cada troca; validar JSON, código de saída e restauração de posição/estilo/monitor/DPI em todas as transições.
4. Resolver a captura: comparar conteúdo guest com conteúdo realmente apresentado e validar a captura após cada modo. A captura antiga `PrintWindow` deixou somente o logo 1280×720 no canto de uma imagem 1920×1080; o restante ficou preto. Isso sustenta uma hipótese de escala/contexto, mas não identifica o que o compositor apresentou. O backbuffer ainda não foi medido.
5. Repetir em múltiplos monitores e escalas DPI; registrar limitações físicas como `BLOCKED`, sem contar estados incompletos como aprovados.
6. Separadamente, medir desempenho sustentado em jogo/cenas e comparar em execução os 39 valores de `mechanics.json`; a amostra curta e o checker geral não fecham essas verificações.

O executor autorizado para a matriz não ficou disponível na rodada anterior: `exec_command` falhou antes de iniciar e a chamada genérica `build_command_run` exigiu aprovação que a política do host não oferece. O `rebuild_check` oficial continuou disponível. Detalhes e limites estão no relatório M03; não contornar a restrição com outro executor.

## Registro histórico — rodada geral de 2026-11-11

Saídas em `_audit/out/m00-r8/` (não versionadas; têm imagens do jogo). Usuário/cache sempre
isolados dentro de `app/`; os saves reais da pessoa não foram tocados.

| Verificação | Comando | Resultado |
|---|---|---|
| Arquivos do jogo (antes) | `python -I _audit\verify_baseline.py` | **PASS 36/36** iguais à linha de base |
| Build do Craft (em UTF-8, página 65001) | `npm run build` | antes da correção: **FAIL** (cmd comia a 1ª letra de cada linha); depois: **PASS**, exit 0 |
| Build do preset (exe dos testes oficiais) | `tools\build_port.cmd game` | **PASS**, exit 0; valores do `mechanics.json` conferidos contra `pc_hooks.toml` |
| Código gerado | busca em `generated\default\*.cpp` | 27 ganchos, incluindo `PcHook_PauseMenuBuilt` (a reforma do menu já passou pelo codegen) |
| Auto-teste do jogo em 4K | `banjotooie.exe ... --self-test` (perfil isolado, `resolution="2160p"`) | **PASS 10/10**: imagem 3840×2160; jogo livre em 25,3 s; andou 719,8 u; pulo 112,3 u; **60,0 quadros/s** e 60,0 atualizações/s; frametime **16,66 ± 0,89 ms**; HUD; rastreador; menu de PC aberto pela pausa; save + 2 backups criados |
| Gancho da entrada de PC | Ghidra (`native_function_pseudocode` 0x822DEE60) + bytes em 0x822DF100 | confere: `bl 0x822E1380` em 0x822DF11C registra a última entrada; 0x822DF120 = `lwz r11,0x154(r30)`; o gancho só age na pausa (`tela+0x198≠0`), como no original (`fnd_32`) |
| Smoke da Missão 01 | `pwsh -File tools\smoke_port.ps1` | **PASS 8/8** (preflight 0/2/3, jogo 3.835 quadros, captura 3840×2160, minidump, 36/36) |
| Regressão PT-BR da Missão 02 | `pwsh -File tools\test_traducao.ps1` | 1ª execução **FAIL** (falso: instrumento); depois do ajuste **PASS**: 29/29 verificações de memória/leitura, **15/15 telas** (IoU 0,85–0,98), 36/36 |
| Teste negativo da Missão 02 | `python -I tools\m02\teste_negativo.py` (Python 3.14, que tem Pillow) | **PASS 22/22**: com as capturas reduzidas o verificador ainda acusa item apagado, frase cortada, tela trocada e fala ausente |
| Checagem oficial do Craft | `rebuild_check` | **PASS 5/5**: build, imagem 3840×2160, auto-teste, 24 valores de mecânica com fonte, créditos |
| Arquivos do jogo (depois) | `verify_baseline.py` + passos 6 dos testes | **PASS 36/36** em todas as conferências |

## Sistema de log completo (item 2, pedido de 2026-11-11; ainda não implementado)

> Continua valendo: é a proposta que a M04 usa na etapa C (item C-10).

Objetivo da pessoa: um registro "o mais completo possível" do jogo, para depurar problemas e
apoiar implementações futuras.

**O que já existe hoje (conferido no código):**

- Log do runtime (`rexglue-sdk/src/core/logging.cpp`): `--log_level` (padrão `info`),
  `--log_file` (vazio = nome sequencial automático), `--log_noisy`, giro a cada
  `--log_max_file_size_mb` (5 MB) e até `--log_max_files` (20) arquivos.
- Relatório de boot em JSON (`--boot_report`; padrão `logs\banjotooie_boot.json` ao lado do
  exe): pré-checagem dos 36 arquivos, estágios com tempo (janela, XEX, primeiro quadro, saída,
  desligamento), quadros/FPS, código e motivo da saída.
- Falhas: minidump em `crashdumps\` com código, endereço e módulo (sem símbolos: não há PDB do
  `rexruntime.dll` nem do exe), e o estágio `crash` no relatório.
- Mensagens soltas da camada de PC (`REXLOG_INFO` nos ganchos: menus, saves, viagem, saída) e
  JSON próprios de cada sonda/auto-teste.

**O que falta (proposta, a confirmar com a pessoa antes de implementar):**

1. **Log de sessão sempre ligado**, na pasta de dados do usuário (`logs\sessao-AAAAMMDD-HHMMSS.txt`),
   com retenção das últimas N sessões e limite de tamanho; não depende de opções de linha de comando.
2. **Cabeçalho da sessão**: versão do exe (hash), commit Git, Windows, CPU, GPU e driver, monitor e
   resolução, opções do menu de PC em vigor, pasta do jogo e resultado da pré-checagem.
3. **Categorias com nível próprio** (ex.: `boot`, `video`, `audio`, `entrada`, `menus`,
   `saves`, `ganchos`, `estado_do_jogo`, `desempenho`, `desligamento`), configuráveis no menu
   de PC (Normal / Detalhado / Rastreio) e por linha de comando.
4. **Eventos do jogo** registrados pelos ganchos que já existem: mudança de mapa (`0x82673720`),
   estado de cena (`0x8266EF30`), menus abertos/fechados e entrada escolhida, gravação/carga de
   save e backups, viagem entre mundos, alterações de opções.
5. **Desempenho**: a cada N segundos, FPS apresentado e da lógica, frametime médio/p95/máximo,
   quadros repetidos — o mesmo que o auto-teste mede, mas durante o jogo normal.
6. **Desligamento passo a passo** (cada fase com tempo), para achar a causa do item 2b.
7. **Falhas com símbolos**: gerar PDB do exe e do `rexruntime.dll` no build e guardar junto, e
   anexar ao relatório de falha as últimas linhas do log e a pilha resolvida.
8. **Menu de PC**: nível de log e botão "Abrir pasta de logs"; nada pessoal no log (só caminhos
   e configurações técnicas).
9. **Prova automática**: sonda que roda o jogo real e confere que o log da sessão foi criado com
   cabeçalho, eventos (mapa, menu, saída) e bloco de desempenho; teste negativo (log desligado,
   pasta sem permissão) e teste de falha que confere a pilha com símbolos.

## Riscos e cuidados

- **Quebras de linha dos `.cmd`:** todos os `.cmd` do repositório estavam com LF no disco. Os dois
  scripts de build voltaram a CRLF (o conteúdo no Git não muda). Os `.cmd` de `_audit\` e das
  evidências continuam com LF (são auxiliares e registros); se forem usados, converter antes.
  Quem editar um `.cmd` deve salvá-lo com CRLF.
- **Python:** o `python` do PATH é o 3.15, sem Pillow; o comparador de telas funciona sem ela
  (mais lento), mas o teste negativo da Missão 02 precisa do Python 3.14
  (`%LOCALAPPDATA%\Programs\Python\Python314\python.exe`), que já a tem.
- **Testes de tela dependem do monitor:** a camada de PC escolhe a saída pela tela; o teste PT-BR
  agora reduz as capturas para 1280×720 antes de comparar.
- A mensagem `'vswhere.exe' não é reconhecido` durante o build vem do `vcvars64.bat` do Visual
  Studio e não afeta o resultado (exit 0).
- As duas pastas de build religam a mesma `rexruntime.dll` do SDK; para comparar binários entre
  missões, usar uma pasta só.
- `docs/evidencias/m00-atual-20261110/` contém conteúdo do jogo: não commitar nem apagar sem a
  pessoa pedir.

## Rodada 9 (2026-11-11, revalidação automática do Craft com o texto da Missão 00)

Sem funcionalidades novas e sem mudança no jogo ou na camada de PC; só documentação.

| Item | Resultado |
|---|---|
| Ponto de retorno | `refs/checkpoints/missao-00-rodada9-20261111` → `7afbc09` (árvore limpa antes da rodada) |
| `rebuild_check` (build real + imagem + auto-teste no jogo real) | **PASS 5/5**: build; imagem 3840×2160; auto-teste 10/10 (andou 719,8 u, pulo 112,3 u, 60,0 quadros/s, frametime 16,65 ± 0,87 ms, máx. 19,2 ms, 0 repetidos, menu de PC aberto pela pausa); 24 valores com fonte; créditos |
| Arquivos do jogo depois da execução | **PASS 36/36** iguais à linha de base (`_audit/out/m00-r9/integridade_pos_check.json`) |
| Pergunta aberta da sobreposição | **respondida** pela leitura do original (`MECHANICS.md` §13: `0x822E5740`, `0x822DEE60`, constantes em `0x82005AA0…`); a correção segue PENDENTE no item 1 |
| Não feito nesta rodada | smoke da M01 e regressão PT-BR da M02 (nada no código mudou desde a rodada 8, onde passaram) |

## Pausa e ícone do Kazooie (pedido de 2026-11-12)

Pedidos: trocar o sapo pelo Kazooie nas opções de configuração do port; tirar CLASSIFICAÇÕES da
pausa; pôr CONFIGURAÇÕES DE PC dentro de AJUDA E OPÇÕES; sair sempre por último. Detalhes e
endereços em `MECHANICS.md` §17; saídas em `_audit/out/m00-r12/`.

| Verificação | Resultado |
|---|---|
| `build_port.cmd codegen` + `npm run build` + `build_port.cmd game` | exit 0; 2 ganchos novos (`0x822DF04C`, `0x822DF2D0`) conferidos no código gerado e pelo gerador de valores |
| Auto-teste no jogo real (`--self-test`, 2 execuções) | **10/10**; `pcmenu`: pausa com 5 entradas, CLASSIFICAÇÕES ausente, SAIR DO JOGO por último; AJUDA E OPÇÕES com 4 entradas, CONFIGURAÇÕES DE PC por último com ícone 13 (Kazooie); A abre o menu de PC |
| Menu principal (`--probe-titlemenu`) | **PASS**: ordem igual, Kazooie em CONFIGURAÇÃO DA RECOMPILAÇÃO, confirmação de saída (NÃO volta, SIM fecha com código 0) |
| Regressão PT-BR (`test_traducao.ps1`) | **PASS** (29/29, 15/15). A etapa da pausa roda com `pc_menu_entry = false` para continuar conferindo os textos originais (CLASSIFICAÇÕES e a mensagem do Xbox LIVE) |

## Leitura Deep do menu e confirmação original de saída (2026-11-12)

O Craft pediu a decompilação do que foi construído. Lidas no original (Ghidra) as funções do menu
principal, da saída e do relógio do console (`MECHANICS.md` §15 e §16). Achado: no jogo original,
"VOLTAR À BIBLIOTECA DE JOGOS" **não sai na hora**, mas abre a confirmação "VOCÊ TEM CERTEZA?"
(SIM/NÃO). Decisão (a pessoa pediu para o agente decidir as perguntas): manter essa confirmação
em "SAIR PARA O DESKTOP" — SIM fecha o programa, NÃO volta ao menu.

| Verificação | Resultado |
|---|---|
| Build com os valores do original | `mechanics.json` ganhou o bloco `pc.menu` e 11 valores com fonte; o gerador cria as constantes e **para o build** se os 8 ganchos do menu divergirem dos endereços lidos; `pc_hooks.cpp` e a sonda usam as constantes |
| `build_port.cmd codegen` + `npm run build` + `build_port.cmd game` | exit 0 |
| `--probe-titlemenu` (jogo real) | **PASS**: caixa "SAIR PARA O DESKTOP" / "VOCÊ TEM CERTEZA?"; B (NÃO) volta ao menu com as 5 entradas; A (SIM) fecha com código 0 (`_audit/out/m00-r11/titlemenu4.json`) |
| `test_traducao.ps1` | **PASS** (29/29, 15/15 telas) |
| `rebuild_check` | build, imagem 3840×2160, auto-teste 10/10 e créditos PASS; "mecânicas" incompleta por limite da própria verificação |
| Arquivos do jogo | 36/36 |

## Desligamento ao fechar (2026-11-12)

Retomada depois de uma parada. Nenhuma mudança de comportamento do jogo; corrigido um defeito do
motor que aparecia ao fechar (inclusive por "SAIR PARA O DESKTOP"). Saídas em `_audit/out/m00-r10/`.

| Passo | Resultado |
|---|---|
| Linha de base: 16 aberturas e fechamentos curtos (`--screenshot`, 6 s) | 15 limpos, **1 queda** 0xC0000005 depois de `guest_thread_exit` (`stress/`) |
| Onde caiu (despejo lido com leitor próprio; nomes pelas exportações do `rexruntime.dll`) | thread `TimerQueue::TimerThreadMain` → callback do xboxkrnl → `Clock::QueryGuestUptimeMillis` → `UpdateGuestClock` (trava `tick_mutex_`) → ntdll escrevendo na pilha de outra thread; mesma assinatura da queda da rodada 9 |
| Causa | `KernelState::TerminateTitle` suspende e encerra com `TerminateThread` as threads do jogo; quem morre esperando a trava deixa o bloco de espera numa pilha que não existe mais (queda); quem morre segurando a trava trava os outros (travamento) |
| 1ª tentativa: trava que não dorme | queda sumiu, mas **4 travamentos em 83** (`stress_fix/`, `hang/`): o relógio congelava e a thread de vblank esperava para sempre um prazo; despejos dos processos travados mostram a thread principal em `GraphicsSystem::Shutdown` esperando essa thread |
| Correção final (`rexglue-sdk`): relógio calculado sem trava (base fixa + relógio do PC, `src/core/clock.cpp`) e laço do vblank que sai quando `vsync_worker_running_` cai (`src/graphics/graphics_system.cpp`) | **64/64** fechamentos limpos (código 0, estágio `shutdown`, captura gravada, máx. 9,2 s; `stress_fix2/`) |
| Regressões | sonda `--probe-titlemenu` PASS; `test_traducao.ps1` **PASS** (29/29, 15/15 telas; cenas no tempo certo); auto-teste 60,0/60,0 em 2 execuções locais |
| `rebuild_check` | build, imagem 3840×2160, auto-teste e créditos PASS; na 1ª execução o passo `fps` mediu 54,8 (soluço passageiro: 60,0 nas 2 repetições locais e na 2ª execução oficial). "Mecânicas" fica **incompleta** por limite da própria verificação (não compara valores em execução) |
| Arquivos do jogo | 36/36 iguais à linha de base |

A rede de segurança de 15 s (`RequestExitOnUiThread`) continua como proteção.

## Menu principal (pedido de 2026-11-11, depois da rodada 9)

Pedido: tirar CLASSIFICAÇÕES do menu principal, pôr "CONFIGURAÇÃO DA RECOMPILAÇÃO" (abre o
menu do F1) logo acima de "SAIR PARA O DESKTOP" (antigo "VOLTAR À BIBLIOTECA DE JOGOS", que
agora fecha o jogo). Detalhes técnicos em `MECHANICS.md` §14. Saídas em `_audit/out/m00-r9/`.

| Verificação | Resultado |
|---|---|
| `tools\build_port.cmd codegen` + `npm run build` + `tools\build_port.cmd game` | exit 0 nos três (ganchos conferidos no código gerado) |
| Sonda `--probe-titlemenu` (jogo real, controle virtual, perfil isolado) | **PASS**: ordem JOGAR / CONQUISTAS / AJUDA E OPÇÕES / CONFIGURAÇÃO DA RECOMPILAÇÃO / SAIR PARA O DESKTOP; CLASSIFICAÇÕES ausente; AJUDA entra e volta ao mesmo lugar; a entrada da recompilação abre o menu de PC e o menu do jogo fica onde estava; SAIR PARA O DESKTOP fecha o programa com código 0 (`titlemenu4.json`, relatório de boot) |
| Regressão PT-BR (`tools\test_traducao.ps1`) | 1ª execução FAIL por dois desligamentos (item 2b do plano, não ligado aos menus: telas certas); teste ajustado à ordem nova (2 toques até AJUDA) e referência nova `menu_principal_recomp` gravada do port; 2ª execução **PASS** em tudo (29/29, 15/15 telas) |
| Repetição das etapas 2 e 3 | 4/4 com código 0 |
| `rebuild_check` | **PASS 5/5** (auto-teste 10/10, imagem 3840×2160) |
| Arquivos do jogo | **PASS 36/36** (`integridade_final.json`) |

## Memória do projeto (Understory)

Registros locais em `.understory/queue/pending/m00-rodada8-verificacao-2026-11-11.json` e
`m00-rodada9-revalidacao-2026-11-11.json` (estado **SYNC_PENDING**; o servidor Understory não está
disponível nesta sessão, então nada foi publicado).

## Configurações de PC dentro do menu do jogo (pedido de 2026-11-12, noite)

- Feito: as opções do F1 saíram da janela sobreposta e viraram páginas do próprio menu do jogo
  (fonte, ícone, cursor, rolagem e avisos de botão do jogo). Detalhes e endereços: `MECHANICS.md` §18.
- Provas no jogo real: auto-teste 10/10 em 69 s (limite 80 s); F1 no jogo livre abre a pausa já na
  página de PC (2,3 s) e fecha; no menu principal as 51 linhas cabem sem quebrar.
- Corrigido junto: testes automáticos gravavam no `out/native/banjotooie.toml` opções só da sessão
  (`log_file`, `cache_path`, `audio_mute`). O arquivo foi restaurado sem essas 3 linhas (cópias em
  `_audit/out/menu-ingame/banjotooie.toml.*`); se o jogo estava mudo de propósito antes, basta
  ligar SILENCIAR TUDO de novo.
- Recado da pessoa (4K/60 interno): validado só o objetivo principal — renderização interna
  3840×2160 (escala 3×3) a 59,9 FPS em 30 s de jogo livre, tempo de quadro 16,69 ± 1,13 ms. A
  matriz 1080p/1440p/4K × Janela/Sem bordas/Tela cheia (M03) continua **PENDENTE (0/9)**.
- Checkpoint antes da mudança: `refs/checkpoints/f1-dentro-do-jogo-antes-20261112`.

## Tearing, queda em tela cheia e revisão dos logs (pedido de 2026-11-12, 22h)

Logs lidos: `out/native/logs/banjotooie_281..293.log`, `banjotooie_boot.json`,
`out/craft-runtime/user/crashes/crash-20261112-220036.txt` e o minidump da mesma hora.

| Problema (log) | Causa | Solução | Prova |
|---|---|---|---|
| Queda 0xC0000005 em `nvwgf2umx.dll` ao trocar MODO (sem bordas/tela cheia) durante o jogo | a troca era aplicada DENTRO do desenho do quadro (fila esvaziada em `OnDraw`, chamado por `D3D12Presenter::PaintAndPresentImpl`); a janela mudava de tamanho no meio da pintura | a fila de tarefas da interface agora roda no laço de mensagens (`CallInUIThreadDeferred`); a janela antiga também | sonda nova `--probe-fullscreen`: ANTES caiu na 1ª troca (mesma pilha do seu relatório); DEPOIS janela → sem bordas → tela cheia → janela, sempre apresentando (~58/s), sem falha (`_audit/out/fullscreen-vsync/`) |
| Tearing com VSYNC ligado (janela) | o apresentador D3D12 sempre usava `Present(0, ALLOW_TEARING)`; o VSYNC do runtime só controla o ritmo de 60 Hz do console | cvar novo `host_vsync` no rexglue-sdk (padrão ligado): `Present(1)` sem tearing, no retraço do monitor; a opção VSYNC do menu passa a ser ele | log: "present on the monitor vblank (sync interval 1, no tearing)"; auto-teste 59,9 FPS / 16,70 ± 1,26 ms em 20 s, 0 repetidos. A ausência visual de tearing precisa do seu olho (não há como fotografar a varredura) |
| VSYNC = NÃO quebraria o ritmo do jogo | o cvar `vsync` desligado faz o "retraço" do console bater a cada 1 ms | `vsync` fica sempre ligado (religado na partida, com aviso no log) | código `graphics_system.cpp` |
| `Flag 'present_effect': 'fsr'/'cas' not in allowed values` (45×) | este runtime não tem o AMD FidelityFX: só aceita `bilinear` | o menu só oferece valores aceitos pelo runtime (ESCALA : BILINEAR); FXAA e escala mostram "(REINICIE)" quando só valem ao reiniciar | `--probe-pcpages`: 0 avisos |
| `XamShowAchievementsUI STUB` (9×) | CONQUISTAS abre o painel do Xbox, que não existe no PC | **não corrigido**: decisão da pessoa (esconder CONQUISTAS como CLASSIFICAÇÕES, ou manter) | — |
| `gamecontrollerdb.txt` ausente (85×), `db360_TexFileList.txt` (94×), `ShaderDumpxe:` (94×), `Content\0000…` (31×) | avisos inofensivos: o SDL tem mapeamentos próprios (o controle Xbox foi reconhecido); arquivo opcional (`MECHANICS.md` §2); caminho de depuração do jogo; procura de conteúdo extra | nenhuma mudança | — |

## Versão global (inglês) e seletor de versão (pedido de 2026-11-13)

- **Compatibilidade:** o `default.xex` da pasta `Banjo-Tooie (World) (XBLA)` é o de varejo cifrado
  (sha256 `4b512ae9…`, o testado pelo projeto-base); decifrado, é a mesma imagem do XEX PT-BR, então
  o mesmo `banjotooie.exe` roda as duas. Diferem só `X360_strings.dat`, `db360.cmp` e
  `db360.textures.cmp` (textos e texturas traduzidos); os outros 8 arquivos são idênticos.
- **Testes na global (dados isolados, `_audit/out/global/`):** auto-teste 10/10 (com
  `--self-test-f1`), `smoke_port.ps1` 8/8 (arquivos 36/36 intactos), `--probe-titlemenu` (ordem,
  AJUDA, página de PC, saída com "ARE YOU SURE?") e `--probe-pcpages` (5 seções, cvar, F1) ok.
  `test_traducao.ps1` não se aplica (confere a tradução PT-BR). A PT-BR repetiu o auto-teste 10/10.
- **Ajuste:** a camada de PC escrevia rótulos em português num jogo em inglês. Agora detecta o
  idioma pelos textos que o jogo carregou (texto 0 = "JOGAR" ou "PLAY GAME"; `pc_lang.cpp`) e usa
  "PC SETTINGS", "RECOMPILATION SETTINGS", "EXIT TO DESKTOP" e as 51 opções em inglês. Os testes
  leem os textos esperados da própria tabela do jogo, então valem para as duas versões.
- **Seletor:** `Seletor\BanjoTooie_Seletor.exe` (fonte `tools/version_selector.cpp`), fora de
  `Build\` para não mudar o programa que o Craft verifica. `--auto ptbr|global` abriu cada versão e
  a tela de abertura mostrou "PRESSIONE START PARA JOGAR" / "PRESS START TO PLAY".
- Fica em PT-BR mesmo na global: o menu sobreposto de reserva (Shift+F1), o contador de FPS e o
  rastreador de itens (janelas do port, não do jogo).

## Check 15 (2026-11-13): verificações do Craft separadas dos saves da pessoa

- **Problema:** o lançador usado pelo Craft (`Build\banjotooie.exe`) usava a mesma pasta de saves e
  configurações que a pessoa usa para jogar (`out\craft-runtime\user`, também a do seletor). O
  auto-teste passou a carregar o save da pessoa (ovos 100/100, penas 99/100), o jogo regravou o
  save (10:03 e 10:05) e o teste do menu de PC deixou `show_fps = true` gravado (a última gravação,
  a que desliga, se perdeu quando o programa encerrou).
- **Correção:** com `--self-test`, `--screenshot`, `--probe-*` ou `--make-advanced-save`, o lançador
  usa `out\craft-check\user`, esvaziada no início (cada verificação começa de um jogo novo);
  jogar normalmente e o seletor continuavam em `out\craft-runtime` (no mesmo dia passaram para
  Documentos\My Games; ver a seção seguinte). Os testes esperam (até 2 s) a fila
  de gravação das opções antes de encerrar (`pc::menu::PendingUiWork`).
- **Dados da pessoa restaurados:** `savegame.dat` voltou ao backup das 09:21:57 (o último gravado
  por ela; depois disso só as verificações gravaram) e `pc_settings.toml` ao anterior
  (`show_fps = false`). Cópias de antes e depois em `_audit/out/save-protecao-20261113/`.
- **Prova:** auto-teste pelo lançador do Craft: jogo novo (ovos 0/100, andou 719,8), 10/10, pasta
  `out\craft-runtime\user` com os mesmos arquivos, tamanhos e datas antes e depois.

## Save em Documentos\My Games\Banjo-Tooie XBLA, um por conta do Windows (2026-11-13)

- **Pedido:** o save fica dentro dos Documentos do perfil, em `Documentos\My Games\Banjo-Tooie XBLA`;
  cada conta de usuário do Windows tem o seu próprio save.
- **Feito:** o próprio jogo escolhe a pasta quando não recebe `--user_data_root`
  (`src/pc/pc_userdir.cpp`, `OnConfigurePaths`): a pasta conhecida Documentos da conta atual
  (aqui ela está no OneDrive: `C:\Users\...\OneDrive\All\Documentos`). Save, opções de PC
  (`pc_settings.toml`), backups e relatórios de falha ficam lá. O lançador do Craft e o seletor
  não passam mais a pasta compartilhada `out\craft-runtime\user`. O cache de shaders (não pessoal)
  fica em `out\craft-runtime\cache`, fora do OneDrive. Execuções automáticas sem
  `--user_data_root` usam `out\craft-check\user-direct`, nunca a pasta da pessoa.
- **Cópia do save antigo:** na primeira abertura em cada conta, se a pasta nova não tem save nem
  opções, o jogo copia (não move nem apaga) o save mais recente entre `out\craft-runtime\user` e o
  padrão antigo `Documentos\banjotooie`, passando por uma pasta provisória. A origem ganha
  `MIGRADO-PARA-MY-GAMES.txt` e não é copiada de novo (outra conta começa um jogo novo).
- **Prova:** `--preflight_only` pelo lançador aponta para a pasta nova sem gravar nada; abertura
  normal copiou 15 arquivos de `out\craft-runtime\user` (mesmos SHA-256, origem inalterada), o jogo
  rodou e fechou com código 0; o check do Craft seguinte passou com jogo novo (ovos 0/100) e a pasta
  `My Games` ficou com os mesmos arquivos e datas. Evidências: `_audit/out/userdir-20261113/`.
- **Fica para a pessoa:** a pasta antiga `Documentos\banjotooie` (testes de 09-10/11) não foi usada nem
  apagada; o arquivo de opções do runtime (`out\native\banjotooie.toml`: atalhos e alguns ajustes
  de vídeo) continua ao lado do jogo, igual para todas as contas.
