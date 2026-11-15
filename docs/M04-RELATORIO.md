# M04 — Relatório da rodada de 2026-11-14

Pedido da pessoa: "verifique o plano se foi aplicado corretamente, corrija o que for necessário e
finalize o restante". Plano vivo: [`PLANO.md`](PLANO.md). Detalhes técnicos: `MECHANICS.md`
§20–§23. **A missão não está concluída**: várias exigências seguem pendentes (ver Pendências).

## Resumo executivo

- **Etapas A e B conferidas e corretas** no build atual: 36/36 arquivos PT-BR iguais à linha de
  base, bateria de saves 18/18, auto-teste 10/10, pasta de saves da pessoa idêntica antes e
  depois, contagens da matriz do plano conferidas.
- **Corrigido nesta rodada:** os 2 defeitos que restavam da auditoria (monitor errado C-29;
  CONQUISTAS sem efeito C-6), um defeito grave achado agora (4K saindo em 2560×1440 esticada com
  a escala do Windows a 150%), 1080p renderizando a 1280×720 (C-22), sem bordas só na área útil
  (C-25), opções de vídeo da conta sobrepostas pelo arquivo do runtime (C-82), 3 sondas que davam
  falso resultado (matriz de vídeo, pausa ao minimizar, escolha de backup).
- **Implementado:** registro de sessão (C-10), linha MONITOR, lista de CONQUISTAS do PC, sonda de
  física 30×60, sonda de sessão longa, bateria única de regressão.
- **Matriz do plano:** 46 OK (eram 32), 0 defeitos, 0 bloqueados; 23 NV, 9 parciais,
  10 não implementados, 9 não verificáveis.

## Correções

| Problema | Causa | Solução | Arquivos | Teste e evidência |
|---|---|---|---|---|
| 4K num monitor 4K a 150% saía numa superfície 2560×1440 esticada pelo Windows (janela 1080p em 1280×720) | processo sem reconhecimento de DPI; o runtime cria a superfície com `GetClientRect` (tamanho lógico) | `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2)` antes da janela | `banjotooie_app.cpp` | `tools/m04/test_monitores.py`: captura da janela = imagem do jogo no monitor principal (antes a imagem ocupava 2/3 da janela) |
| Imagem do jogo mudava de tamanho conforme a janela | o runtime informava ao jogo o tamanho da janela como modo de vídeo do console; com 1920×1080 o jogo gera um quadro 1080p | cvar novo `video_mode_fixed` (rexglue-sdk) ligado pelo port: modo sempre 1280×720 | `rexglue-sdk/src/kernel/xboxkrnl/xboxkrnl_video.cpp`, `banjotooie_app.cpp` | matriz 9/9 com imagem interna constante por resolução |
| C-22: 1080p desenhava a 1280×720 ampliado | escala interna 1× | escala 2× (2560×1440) reduzida na apresentação | `pc_settings.cpp`, `pc_menu.cpp`, `banjotooie_app.cpp` | matriz: 1080p → 2560×1440 em todos os modos |
| C-82: a resolução escolhida no menu era ignorada | `banjotooie.toml` (ao lado do exe, gravado em 13/11 com `resolution = "4k"`) era lido depois das opções da conta | resolução/escala/janela/tela cheia reaplicadas depois dele e não mais gravadas nele | `banjotooie_app.cpp`, `pc_menu.cpp` | log: "valor do arquivo do runtime ignorado"; monitores 7/7 |
| C-29: resolução automática pelo monitor principal mesmo com a janela em outro | `EnumDisplaySettingsW(nullptr)` | linha MONITOR (automático = monitor do cursor), resolução e janela no monitor escolhido; opções antigas (versão < 4) ficam no principal | `pc_display.cpp/h`, `pc_menu.cpp`, `pc_settings.*`, `banjotooie_app.cpp` | 3 monitores (4K 150%, 1080p, ultrawide 2560×1080): janela no monitor pedido, automático = 1080p nos dois secundários; migração testada com cópia das opções da pessoa (continua 4K, tela cheia, principal) |
| C-25: sem bordas cobria só a área útil | `rcWork` | `rcMonitor` em pixels físicos | `pc_menu.cpp`, `pc_automation.cpp` (critério) | monitor inteiro nos 3 monitores |
| Janela encolhia 2/3 ao ir para um monitor de outra escala | reescala de DPI do Windows depois do redimensionamento | move primeiro, mede e dimensiona depois | `pc_menu.cpp` | 1765×993 (maior que cabe) nos secundários |
| C-6: CONQUISTAS não fazia nada | ação `0x822DD628` só chama `XamShowAchievementsUI` (stub no PC) | gancho abre a lista das 12 conquistas do jogo dentro do menu (PT-BR), obtidas guardadas por conta via `XGIUserWriteAchievements` | `pc_achievements.*`, `pc_hooks.cpp/.toml`, `pc_ingame_menu.*`, `pc_menu.*`, rexglue-sdk `xgi_app.*` | `--probe-conquistas` PASS no título e na pausa; 0 chamadas ao painel do Xbox |
| Matriz de vídeo M03 0/9 | sonda: fotos do jogo e da janela em instantes diferentes; teste de andar sem fechar o diálogo de abertura | repete o par de fotos; 2 toques em B antes de andar | `pc_automation.cpp` | 9/9 + 9/9 reaberturas |
| Sonda de foco minimizava a janela em primeiro plano (podia ser de outro programa) e o vigia nem rodava em teste | `GetForegroundWindow`; vigia desligado em execução automática | só a janela do jogo, sem ativar nada; minimizar também pausa | `pc_automation.cpp`, `banjotooie_app.cpp` | `--probe-focus` PASS |
| Bateria de saves s14 falhou depois das linhas novas do menu | a sonda escolhia RESTAURAR pela posição | escolhe pelo texto | `pc_automation.cpp` | `test_saves.py --rapido` 17/17 |

## Testes automatizados (jogo real, pastas isoladas)

| Teste | Comando | Resultado |
|---|---|---|
| Arquivos PT-BR | `_audit/verify_baseline.py` | 36/36 (início e fim de cada bateria) |
| Pasta de saves da pessoa | `tools/m04/foto_pessoa.py --compare` | igual (15 arquivos) em todas as conferências |
| Saves (etapa B) | `tools/test_saves.py` | 18/18 (completo) e 17/17 (rápido, depois das mudanças) |
| Auto-teste do Craft | `--self-test` | 10/10 (várias vezes; 60,0 FPS, 16,66 ± 0,93 ms) |
| Tradução PT-BR | `tools/test_traducao.ps1` | PASS: 29/29 leituras, 15/15 telas, 36/36 |
| Física 30×60 | `tools/m04/fisica_30x60.py` | 71/80 (terra, 2026-11-14); 68/79 com câmera e natação (2026-11-15, §20.1) |
| Vídeo 3×3 | `tools/test_display_modes.py` | 9/9 estados + 9/9 reaberturas, 72 trocas de modo; limite físico: janela 4K num monitor 4K |
| Monitores | `tools/m04/test_monitores.py` | 7/7 (3 monitores) |
| Distância de desenho / viagem | `--probe-dd-worlds` | 18/18 mapas PASS (3 perfis aplicados e capturados) |
| CONQUISTAS | `--probe-conquistas` | PASS título e pausa |
| Pausa ao minimizar | `--probe-focus` | PASS |
| Registro de sessão | sondas + teste negativo | PASS (cabeçalho, eventos, desempenho; pasta impossível: jogo 10/10) |
| Sessão longa | `--probe-longa` 60 min | **PASS**: 1 h sem travamento, 184/184 viagens, 59,4 FPS, memória estável |

**Mundos cobertos:** viagem e distância de desenho em 18 mapas (0x112, 0x128, 0x127, 0x136,
0x142, 0x15D, 0x12D, 0x124, 0xD6, 0x163, 0xBB, 0x18A, 0x1A7, 0x1A8, 0x1A9, 0x100, 0xB8, 0xC7);
física em 4 (0xAF, 0x127, 0xD6, 0x1A7).
**Configurações de vídeo:** 1080p/1440p/4K × janela/sem bordas/tela cheia; monitores 3840×2160
(150%), 1920×1080 e 2560×1080.

## Sessão longa

`--probe-longa` com `--longa-minutos 60` (2026-11-14, 18:2x–19:2x;
`_audit/out/m04/F/sessao-longa2/longa.json` + `user/logs/sessao-*.txt`): o jogo viaja pelos 18
mapas com a rotina do próprio jogo e, em cada um, corre, pula, gira a câmera, golpeia e abre/fecha
a pausa. Uma 1ª tentativa parou em 62 s porque a sessão do agente foi interrompida (não foi falha
do jogo).

| Medida | Resultado |
|---|---|
| Duração / travamentos | 3.611 s; **0 travamentos** (vigia: 15 s sem quadro novo) |
| Viagens / pausas | **184/184** viagens; 183 pausas abertas e fechadas |
| FPS (por minuto) | média **59,4**; mínimo 56,8; lógica 59,4/s |
| FPS por visita (≈ 15 s de jogo) | 59–60 em 16 dos 18 mapas; mínimo 56,7 (0x128) e 54,9 (0x100, 1 visita) |
| Tempo de quadro (122 blocos de 30 s) | p95 médio 18,6 ms; 217 quadros > 33,4 ms em 1 h (≈ 1 por carregamento de mapa; pior 1,7 s durante um carregamento) |
| Memória do processo | 1.337 MB no início, **1.476 MB aos 5 min, 1.479 MB no fim** (+2,7 MB em 55 min: sem vazamento) |
| Handles / CPU / vídeo | 1.039 → 1.041; CPU média 21,5%; memória de vídeo 0,48–0,76 GB |
| Erros no log do runtime | 0 |

Leitura: estável por 1 hora a 4K/60 neste PC. Os quadros longos acontecem nos carregamentos de
mapa (tela de transição), não no jogo livre. GPU (uso %) não foi medida: o registro traz só a
memória de vídeo (DXGI).

## Pendências (tudo o que não foi concluído ou validado)

- **Física/tempo (C-32, C-33, C-35, C-36):** atualizado em 2026-11-15 (MECHANICS.md §20.1):
  câmera achada (ponteiro fixo `0x8266EEF0`) e medida — igual em terra e no Fundo do Mar, mas em
  **Atlântida a câmera assenta diferente a 60 FPS** depois de soltar o analógico (não vem das 17
  constantes; causa no código da câmera não identificada); natação medida e igual; relógio do
  jogo igual. **Ainda não medidos:** voo (só começa numa plataforma de voo; nenhum ponto de
  chegada automático fica numa e as posições das plataformas não foram achadas), minijogos
  (começam por diálogo/ingresso; 16 das 17 constantes do 60 FPS só rodam neles), inimigos,
  chefes, cenas e sincronia de áudio.
- **Controles (C-38, C-52–C-61):** sem controle físico conectado nesta rodada; remapeamento de
  controle, aviso de conflito e restaurar padrões **não implementados**.
- **Áudio (C-65–C-67):** música/efeitos separados não implementados; nenhum nível medido.
- **Câmera (C-62–C-64):** suavização no controle não implementada; modos de câmera não testados.
- **Opções (C-81, C-82):** FXAA, MSAA, anisotropia e VSync ainda ficam no arquivo do runtime
  (máquina), não na conta; arquivo inválido não testado nesta rodada.
- **Cenas e QoL (C-84–C-86, C-92, C-93):** pular cenas vistas não implementado; contador de FPS
  e estatísticas sem medida de custo.
- **Tradução por mundo (C-13–C-19, C-87, C-88):** só as 15 telas da regressão; diálogos de todos
  os mundos, HUD e textos em texturas não percorridos.
- **Distância de desenho (C-43–C-45):** pop-in/geometria só por capturas (comparação automática
  inconclusiva por animação); custo em FPS por perfil e persistência não medidos.
- **Saves de teste (C-70):** faltam saves de meio e fim da história (há jogo novo, save real e
  100% dos colecionáveis).
- **Bloqueios externos:** tearing (precisa do olho da pessoa), 2ª conta do Windows, escala DPI
  diferente por monitor além da atual (não mudada sem autorização).
- **Foco:** pausa ao perder o foco não testada para não tirar o foco da pessoa (só minimizar).

## Entrega

- **Executável PT-BR:** `app\out\native\banjotooie.exe` (lançador do Craft: `app\Build\banjotooie.exe`;
  seletor: `app\Seletor\BanjoTooie_Seletor.exe --auto ptbr`).
- **Identificação do build:** o cabeçalho de cada `logs\sessao-*.txt` traz o SHA-256 do exe e
  os commits do app e do rexglue-sdk.
- **Executar:** abrir o seletor ou o lançador; saves e opções em `Documentos\My Games\Banjo-Tooie XBLA`.
- **Relatórios e evidências:** `app\_audit\out\m04\` (A, B, C, D, E, F, V; fora do Git porque contêm
  imagens do jogo), este arquivo, `PLANO.md`, `MECHANICS.md` §19–§23.
- **Commits (branch `port-pc-4k60`):** `9ca78e0` (checkpoint), `e91d70d` (CONQUISTAS), `bd22c5c`
  (vídeo/DPI/monitores), `e927028` (matriz 9/9), `d66b3fa` (foco, saves, bateria, documentação) e o
  commit final desta rodada. **rexglue-sdk:** `e7c0213` (aviso de conquistas), commit do
  `video_mode_fixed`. **Checkpoint:** `refs/checkpoints/m04-cde-wip-20261114` → `0808b2f` (antes
  desta rodada).
