# Plano do projeto — Banjo-Tooie XBLA PT-BR Recompiled

**Atualizado em 2026-11-11 (verificação geral, "rodada 8").** Este é o plano vivo do projeto: o que
já está pronto (com a prova), o que falta e em que ordem. Ele substitui a lista "Ordem de
implementação recomendada" da auditoria `docs/AUDITORIA-MISSAO-00-ATUAL-2026-11-11.md`, que fica
como registro daquela data. Nenhuma missão nova foi iniciada nesta verificação.

## Em poucas palavras

- O jogo roda nativo no Windows, em **4K a 60 quadros por segundo**, em PT-BR, a partir da cópia da
  pessoa (que nunca é alterada), e passa em todos os testes automáticos com o jogo rodando de verdade.
- Nesta verificação foram achados e corrigidos **dois problemas de ferramenta** (nada no jogo):
  1. os scripts de build tinham sido gravados com quebra de linha do tipo Unix e falhavam no
     Prompt do Windows em UTF-8;
  2. o teste de telas PT-BR da Missão 02 não tinha sido ajustado à camada de PC (capturas em 4K
     contra referências em 720p; log de rastreio girando) e acusava falha falsa.
- Foi achado **um defeito visual** ainda não corrigido: a entrada "CONFIGURAÇÕES DE PC" (7ª do
  menu de pausa) fica por baixo do contador de Jiggies do HUD. É o primeiro item da lista abaixo.

## Linha de base (Git)

| Item | Valor |
|---|---|
| Raiz Git | `W:\Decomp\Banjo 3\app` (branch `port-pc-4k60`, 12 commits à frente de `master` `1075821`) |
| Commits desta verificação | `3aa27aa` (entrada de PC acrescentada na pausa + valores reais do XEX no build, trabalho que estava só no disco, agora verificado) e `9f5b344` (teste PT-BR adaptado à camada de PC) |
| Ponto de retorno | `refs/checkpoints/missao-00-rodada8-20261111` → `110750f` (foto da árvore antes de tudo; não mexe em HEAD/índice). Diff arquivado em `_audit/out/m00-r8/trabalho_antes.diff` |
| Submódulo `rexglue-sdk` | `a0f108c` (fork iron-jay `035aa25` + 8 commits locais `ptbr-port-banjo3`) |
| Fora do Git, de propósito | `docs/evidencias/m00-atual-20261110/` (contém conteúdo do jogo), `.understory*` |
| Operações remotas | nenhuma |

## O que foi verificado agora (execuções reais, 2026-11-11)

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
| 4K (3840×2160) | **IMPLEMENTADO** | auto-teste e captura oficial em 3840×2160 |
| 60 FPS | **IMPLEMENTADO** | 60,0/60,0 em jogo livre, frametime 16,66 ms. Cenas e abertura seguem a 30 como no original (`scenes60=false`, opção experimental no menu de PC) |
| Menu START (menu do próprio jogo) | **IMPLEMENTADO**, com **defeito visual** | navegação automática título → JOGAR → JOGO 1 → jogo → pausa; a 7ª entrada "CONFIGURAÇÕES DE PC" se sobrepõe ao contador de Jiggies (item 1 abaixo) |
| PT-BR | **PARCIAL** | abertura, menus, seleção, introdução, pausa, submenus, mensagem do sistema e fala do Bottles conferidos (15/15); HUD e diálogos das fases ainda não vistos |
| Draw distance | **PARCIAL** | 3 perfis (Original/Longe/Total) no código e sondas `--probe-dd*`; comparação visual e custo em FPS não medidos nesta rodada |
| Gamepad | **PARCIAL** | controle virtual (SDL) conduz o jogo real; controle físico XInput não testado aqui (a pessoa relata que funciona, `DECISOES.md` nº 1) |
| Saves | **IMPLEMENTADO** (gravar + backup) / **PARCIAL** (carregar) | o jogo gravou `savegame.dat` + backups no perfil isolado; carregar esse save ainda não é testado automaticamente |
| QoL | **IMPLEMENTADO** (conjunto inicial) | menu de PC (60 FPS/cenas, velocidade de diálogo, início rápido, contador de FPS, pausa ao perder foco, backups, câmera/controle) |
| Testes automáticos | **IMPLEMENTADO** | auto-teste 10/10, smoke 8/8, regressão PT-BR + negativo, checagem do Craft 5/5 |

## Próximos passos, em ordem (cada um só quando for pedido)

| # | Passo | Por que nesta ordem | Estado |
|---|---|---|---|
| — | Fechar a reforma do menu de pausa (codegen + rebuild + auto-teste) | era o item 1 da auditoria de 2026-11-11 | **FEITO** (`3aa27aa`, auto-teste 10/10, gancho conferido no original) |
| — | Regressão completa depois da camada de PC | era o item 6 da auditoria | **FEITO** (smoke 8/8, PT-BR PASS, negativo 22/22) |
| 1 | **Corrigir a sobreposição** da entrada "CONFIGURAÇÕES DE PC" com o contador de Jiggies na pausa | aparece toda vez que o jogador pausa; mudança pequena e localizada na camada de PC (espaçamento das entradas ou posição), fácil de provar com captura | PENDENTE |
| 2 | **Carregar save automaticamente** (`--make-advanced-save` + `--probe-advanced-save` em perfil de teste) | fecha o requisito "saves" (hoje só a gravação tem prova); protege os saves da pessoa antes de qualquer outra mudança no jogo | PENDENTE |
| 3 | **Draw distance**: capturas dos 3 perfis (`--probe-drawdistance`, `--probe-dd-worlds`) e custo em FPS a 4K | já implementado, falta a prova visual e o efeito nos 60 FPS | PENDENTE |
| 4 | **PT-BR no HUD e nos diálogos das fases** (continuação da Missão 02) | cobertura de texto ainda parcial; usa a mesma regressão já ajustada | PENDENTE |
| 5 | **Controle físico (XInput)**: `--probe-gamepad` / `--probe-testpad` com um controle conectado | depende de a pessoa conectar um controle; até lá vale o relato dela | BLOQUEADO (precisa do controle conectado) |
| 6 | **Auto-detecção de resolução**: numa execução anterior escolheu 1080p porque o Windows reportou a tela Intel (2560×1080) | QoL; não impede 4K (o menu de PC fixa a resolução) | PENDENTE |
| 7 | **Levar `port-pc-4k60` para `master`** (local) depois dos itens 1–2 | hoje `master` está 12 commits atrás; integrar só com a camada de PC provada e com OK da pessoa | PENDENTE (decisão da pessoa) |
| 8 | **Conferir a atualidade das bases** (iron-jay, rexglue-sdk, XenonRecomp, MC_LA_Recompiled) | a comparação usa o retrato local de 2026-11-09; consultar remotos só com autorização | PENDENTE |

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

## Memória do projeto (Understory)

Registro local em `.understory/queue/pending/m00-rodada8-verificacao-2026-11-11.json`
(estado **SYNC_PENDING**; o servidor Understory não está disponível nesta sessão, então nada foi
publicado).
