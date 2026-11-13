# Plano do projeto — Banjo-Tooie XBLA PT-BR Recompiled

**Atualizado em 2026-11-12**, incorporando o encerramento M00 e o estado da validação M03.
Este é o plano vivo: registra o que foi verificado, o que falta e a ordem de continuação.
A auditoria M00 tem `PASS_FINAL`; M03 segue `BLOCKED`, sem aprovação runtime da matriz de vídeo
(0/9 combinações). Esta atualização muda somente documentação; não conclui M03.

## Em poucas palavras

- O port nativo do Windows foi auditado em M00 (`PASS_FINAL`). Há captura guest em 3840×2160 e
  medições curtas de 59,4 FPS/5 s (M00) e 60,0 FPS/5 s (rebuild M03); isso **não comprova 60 FPS
  sustentados em todo o jogo**. A verificação PT-BR segue parcial.
- M03 implementa ajustes à janela, sonda e runner da matriz de vídeo. No `rebuild_check` desta
  atualização passaram build, captura guest 4K e core loop do auto-teste; o relatório M03 anterior
  registra self-test 10/10. O gate Sol3 aprovou somente revisão estática.
- M03 continua **BLOCKED**: 0/9 combinações 1080p/1440p/4K × Janela/SemBordas/TelaCheia foram
  validadas no binário final. Faltam também reabertura/persistência, dimensões físicas, entrada
  após as trocas e captura/apresentação confiável. Consulte a atualização M03 abaixo.
- A organização da pausa foi ajustada: CLASSIFICAÇÕES saiu da pausa e CONFIGURAÇÕES DE PC fica
  dentro de AJUDA E OPÇÕES; a pausa termina em SAIR DO JOGO. A entrada não está mais na 7ª posição.

## Linha de base local (último estado Git comprovado)

| Item | Estado registrado |
|---|---|
| Raiz Git do produto | `W:\Decomp\Banjo 3\app`; branch `port-pc-4k60` |
| Baseline aprovado M00 | HEAD `2ccad376fda092ef4550abbcb066d5c8ec4ad7df`; `master` estava 18 commits atrás. Referência: `_audit/mission00/REPORT.md`, snapshot anterior à M03. |
| SDK no baseline M00 | `rexglue-sdk` em `23633e1a3077593de25e2f1b768b1ac63d804a9a`; outputs `18/18` foram verificados/restaurados antes da M03. |
| Estado depois do baseline | M03 alterou seis arquivos de produto e não criou commit. O plano atualizado também ficará como mudança local. |
| Estado Git atual | Não revalidado nesta atualização: `exec_command` falhou antes de iniciar. O relatório M03 registra remote local vazio; nenhuma operação remota foi feita aqui. |
| Dados fora do Git | Preservar os originais do jogo, binários/capturas proprietários e `.understory*`; não alegar hashes novos dos originais após os builds M03. |

## Atualização de 2026-11-12 — M00 e M03

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

## Matriz de requisitos

| Requisito | Estado | Prova / o que falta |
|---|---|---|
| Port nativo Windows 11 x64 | **IMPLEMENTADO** | exe nativo, sem emulador; build reprodutível (agora também em UTF-8) |
| 4K (3840×2160) | **IMPLEMENTADO no framebuffer/guest** | captura M00 e rebuild M03 em 3840×2160; isso não valida cliente físico ou Janela 4K, que seguem na matriz M03 pendente |
| 60 FPS | **PARCIAL** | M00 mediu 59,4 FPS por 5 s e o rebuild M03 60 FPS por 5 s; falta duração longa e cobertura de cenas/mundos. Cenas e abertura seguem a 30 como no original (`scenes60=false`) |
| Menu START (menu do próprio jogo) | **IMPLEMENTADO no caminho testado** | pausa com 5 entradas, sem CLASSIFICAÇÕES e terminando em SAIR DO JOGO; CONFIGURAÇÕES DE PC está dentro de AJUDA E OPÇÕES, por último e com ícone do Kazooie; não aparece como 7ª entrada sob o HUD (`MECHANICS.md` §17) |
| Menu principal da camada de PC (pedido de 2026-11-11) | **IMPLEMENTADO** | JOGAR, CONQUISTAS, AJUDA E OPÇÕES, **CONFIGURAÇÃO DA RECOMPILAÇÃO** (abre o menu do F1), **SAIR PARA O DESKTOP** (confirmação "VOCÊ TEM CERTEZA?" como no original; SIM fecha o programa); CLASSIFICAÇÕES saiu do título. Sonda `--probe-titlemenu` no jogo real: ordem, entrar/voltar em AJUDA, menu de PC aberto e fechado, saída com código 0 (`MECHANICS.md` §14) |
| Vídeo 1080p/1440p/4K × Janela/SemBordas/TelaCheia (M03) | **BLOCKED — 0/9** | Build/captura guest/self-test aprovados; sonda e runner finais ainda não executados. Ver matriz e lista de verificações acima e [relatório M03](../_audit/mission03/REPORT.md) |
| Sistema de log completo | **PENDENTE** (pedido de 2026-11-11) | item 2 abaixo; segue no backlog após a M03 ativa |
| PT-BR | **PARCIAL** | abertura, menus, seleção, introdução, pausa, submenus, mensagem do sistema e fala do Bottles conferidos (15/15); HUD e diálogos das fases ainda não vistos |
| Draw distance | **PARCIAL** | 3 perfis (Original/Longe/Total) no código e sondas `--probe-dd*`; comparação visual e custo em FPS não medidos nesta rodada |
| Gamepad | **PARCIAL** | controle virtual (SDL) conduz o jogo real; controle físico XInput não testado aqui (a pessoa relata que funciona, `DECISOES.md` nº 1) |
| Saves | **IMPLEMENTADO** (gravar + backup) / **PARCIAL** (carregar) | o jogo gravou `savegame.dat` + backups no perfil isolado; carregar esse save ainda não é testado automaticamente |
| QoL | **IMPLEMENTADO** (conjunto inicial) | menu de PC (60 FPS/cenas, velocidade de diálogo, início rápido, contador de FPS, pausa ao perder foco, backups, câmera/controle) |
| Configurações de PC **dentro do menu do jogo** (pedido de 2026-11-12) | **IMPLEMENTADO** | página 7 do próprio menu (raiz + 5 seções, 51 opções), F1 abre no título, na pausa e no jogo livre; janela sobreposta só como reserva (Shift+F1). Provas: `--self-test` 10/10, `--self-test-f1`, `--probe-pcpages`, `--probe-titlemenu` (`MECHANICS.md` §18) |
| Testes automáticos | **PARCIALMENTE VERIFICADOS** | M00 `PASS_FINAL`; rebuild M03 e auto-teste 10/10 aprovados. As verificações de M00 36/36 originais e SDK 18/18 pertencem ao checkpoint pré-M03; ver histórico e relatórios |

## Próximos passos, em ordem

| # | Passo | Por que nesta ordem | Estado |
|---|---|---|---|
| 0 — missão ativa | **Concluir a validação runtime M03** | É a única missão ativa; exige 9 estados únicos + 9 reaberturas, persistência, dimensões físicas, entrada e captura válidas, sem relaxar critérios | **BLOCKED** até haver executor autorizado; M03 `PASS_FINAL` ainda não existe |
| — | Fechar a reforma do menu de pausa (codegen + rebuild + auto-teste) | era o item 1 da auditoria de 2026-11-11 | **FEITO** (`3aa27aa`, auto-teste 10/10, gancho conferido no original) |
| — | Regressão completa depois da camada de PC | era o item 6 da auditoria | **FEITO** (smoke 8/8, PT-BR PASS, negativo 22/22) |
| 1 | **Corrigir a sobreposição** da entrada "CONFIGURAÇÕES DE PC" com o contador de Jiggies na pausa | resolvido de outro jeito pelo pedido de 2026-11-12: CLASSIFICAÇÕES saiu da pausa e CONFIGURAÇÕES DE PC foi para dentro de AJUDA E OPÇÕES, então a pausa tem 5 entradas e termina em SAIR DO JOGO, longe do contador (`MECHANICS.md` §17) | **FEITO** |
| — | **Menu principal**: CLASSIFICAÇÕES sai, CONFIGURAÇÃO DA RECOMPILAÇÃO acima de SAIR PARA O DESKTOP (que fecha o jogo) | pedido da pessoa (2026-11-11) | **FEITO** (sonda `--probe-titlemenu` PASS, regressão PT-BR PASS 15/15, `rebuild_check` 5/5, 36/36) |
| 2 | **Sistema de log completo** (pedido de 2026-11-11, detalhes na seção "Sistema de log completo" abaixo) | base para depurar e implementar tudo o que vem depois (carga de save, distância de desenho, controle físico) e para achar a causa do defeito intermitente de desligamento (item 2b); vem logo depois do item 1, que é pequeno e já tem a causa lida | PENDENTE |
| 2b | **Desligamento intermitente do runtime** (queda 0xC0000005 ou travamento ao fechar) | causa achada em 2026-11-12: ao fechar, `TerminateTitle` mata as threads do jogo em pontos aleatórios; se uma estava dentro da atualização do relógio do console (`UpdateGuestClock`, trava `tick_mutex_`), a trava ficava quebrada (queda na thread de temporizadores ou travamento em `GraphicsSystem::Shutdown`). Relógio refeito sem trava + thread de vblank sai do laço quando o desligamento é pedido (submódulo `rexglue-sdk`) | **FEITO** (antes: 1 queda em 16 fechamentos; 1ª tentativa: 4 travamentos em 83; depois: **64/64** limpos) |
| 3 | **Carregar save automaticamente** (`--make-advanced-save` + `--probe-advanced-save` em perfil de teste) | fecha o requisito "saves" (hoje só a gravação tem prova); protege os saves da pessoa antes de qualquer outra mudança no jogo | PENDENTE |
| 4 | **Draw distance**: capturas dos 3 perfis (`--probe-drawdistance`, `--probe-dd-worlds`) e custo em FPS a 4K | já implementado, falta a prova visual e o efeito nos 60 FPS | PENDENTE |
| 5 | **PT-BR no HUD e nos diálogos das fases** (continuação da Missão 02) | cobertura de texto ainda parcial; usa a mesma regressão já ajustada | PENDENTE |
| 6 | **Controle físico (XInput)**: `--probe-gamepad` / `--probe-testpad` com um controle conectado | depende de a pessoa conectar um controle; até lá vale o relato dela | BLOQUEADO (precisa do controle conectado) |
| 7 | **Auto-detecção de resolução**: numa execução anterior escolheu 1080p porque o Windows reportou a tela Intel (2560×1080) | QoL; não impede 4K (o menu de PC fixa a resolução) | PENDENTE |
| 8 | **Levar `port-pc-4k60` para `master`** (local) depois dos itens 1–3 | no baseline M00, `master` estava 18 commits atrás; o estado atual não foi revalidado. Integrar só com a camada de PC provada e com OK da pessoa | PENDENTE (decisão da pessoa) |
| 9 | **Conferir a atualidade das bases** (iron-jay, rexglue-sdk, XenonRecomp, MC_LA_Recompiled) | a comparação usa o retrato local de 2026-11-09; consultar remotos só com autorização | PENDENTE |

## Sistema de log completo (item 2, pedido de 2026-11-11; ainda não implementado)

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
