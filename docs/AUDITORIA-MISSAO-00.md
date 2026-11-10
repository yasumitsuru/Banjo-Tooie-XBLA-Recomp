# MISSÃO 00 — Auditoria do estado atual (relatório, revisão 2)

**Data:** 2026-11-09 (rev. 1) · 2026-11-09/10 (rev. 2) · **Ambiente:** Windows 11 Pro x86-64
**Modo:** somente leitura/não destrutivo. Nenhuma funcionalidade implementada e nenhum arquivo
original modificado (comprovado por hash antes e depois, seção 4).
**Commit de base desta revisão:** `d938a31` (repositório `app/`); as mudanças desta revisão estão
no commit seguinte (ver `git -C app log`).

---

## 0. O que mudou da revisão 1 para a 2 (correções)

| # | Revisão 1 dizia | Revisão 2 (com evidência) |
|---|---|---|
| 1 | "PT-BR in-game **ausente** nos arquivos fornecidos" | **Errado.** O jogo original, rodando com os seus arquivos, mostra **"PRESSIONE START PARA JOGAR"** na tela de abertura (smoke test real, seção 4.3). A tradução fica nos dados compactados (`.cmp`/`.tbl`), que a busca de texto da rev. 1 não conseguia ler. |
| 2 | "Xenia não está instalado; smoke test do original BLOQUEADO" | **Errado.** Há um Xenia Canary em `W:\Emulador\Xbox360\Emulators\Xenia Canary`. O smoke test foi feito numa cópia isolada (seção 4.3). |
| 3 | "60 FPS: provável 30" e, depois, em `MECHANICS.md`: "o jogo é 60 FPS por padrão" | O "60" em `0x822F9CB8` é a **taxa de atualização da tela (60 Hz)** usada pelo agendador de troca de quadros, não a meta do jogo. O código original grava **intervalo de apresentação = 2** (`li r9,2` → `sth` em `0x8269B1BC`, endereço `0x82276E34`), ou seja, **30 FPS numa tela de 60 Hz**. É exatamente esse valor que o patch "60 FPS" da comunidade troca por 1 (seção 3.4). Confiança: inferida, pelo código e pela estrutura do patch. Não foi medido em execução. |
| 4 | "Hash do XEX ≠ base: talvez o layout de código seja outro" | A imagem do seu XEX confere com os **113 hashes SHA-1** do próprio cabeçalho (código e dados intactos). Os **49 endereços** que a base corrige à mão conferem **49/49** neste XEX. A diferença de hash vem do **arquivo estar decifrado/descomprimido** (Encryption None / Compression None), enquanto a base testou o arquivo como ele sai do pacote STFS (seção 3). |
| 5 | Commit `d938a31` e `README.md` falavam em "MISSÃO 01 concluída" | Rótulo incorreto: aquilo foi uma **rodada automática de revisão da própria Missão 00** (análise estática, sem implementação). Nenhuma missão sua além da 00 foi iniciada. O histórico Git não foi reescrito, para não fazer uma operação destrutiva; o rótulo foi corrigido nos documentos. |

---

## 1. Resumo executivo

O projeto é um port nativo para PC do **Banjo-Tooie (XBLA)** por recompilação estática PPC→C++,
com base pública em [iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp)
e no SDK [ReXGlue](https://github.com/rexglue/rexglue-sdk). Estado real hoje:

- **Os seus arquivos estão íntegros e funcionam.** A imagem do executável é idêntica à registrada
  no cabeçalho (113/113 blocos). O jogo original **inicia até a tela de abertura em PT-BR** no
  Xenia, usando exatamente a pasta fornecida.
- **O seu executável é compatível com a base do port.** Todos os 49 pontos corrigidos à mão pela
  base existem no seu XEX com o mesmo código. Só o launcher da base recusaria o arquivo, porque
  compara o hash do arquivo, e o seu está decifrado.
- **O computador tem tudo para compilar** (CMake 4.4, Ninja 1.13, Clang 22, MSVC 14.51 + Windows
  SDK, Python, Java, Vulkan) e **para exibir 4K/60** (RTX 5080 + monitor 3840×2160 a 60 Hz).
- **Ainda não existe código do port nem executável recompilado** nesta pasta. Build, gamepad,
  saves e jogabilidade **não** foram verificados.
- Há **mapas conhecidos para 60 FPS, draw distance e LOD**: patches da comunidade do Xenia cujos
  ganchos conferem byte a byte com o seu XEX (seção 3.4). Eles servem de roteiro para o port; não
  foram aplicados.

> **Honestidade de evidência:** o único programa executado foi o jogo **original**, num emulador,
> até a tela de abertura e **sem nenhuma entrada de controle**. Nada do port foi compilado ou
> executado. Jogabilidade **não** está comprovada.

---

## 2. Raiz do projeto, arquivos fornecidos e ferramenta de build

| Item | Localização (verificada) |
|---|---|
| Raiz do workspace | `W:\Decomp\Banjo 3` (não é repositório Git) |
| Arquivos do jogo (fornecidos) | `W:\Decomp\Banjo 3\Banjo Tooie BR` (36 arquivos, ~105 MB) |
| Área de trabalho do projeto | `W:\Decomp\Banjo 3\app` (repositório Git próprio, branch `master`) |
| Ferramenta de build do port | **Não existe localmente.** É a da base: CMake preset `win-amd64-release` + Ninja + Clang + ReXGlue (`rexglue codegen`). Todos os pré-requisitos estão instalados (seção 4.1). |
| Clone de referência da base | `C:\Users\<usuario>\AppData\Local\Temp\opencode\bt-xbla-base` (commit `72c2397` = HEAD remoto em 2026-11-09; submódulo `rexglue-sdk` **não inicializado**, pino `035aa25` = HEAD do fork) |
| Emulador para referência | `W:\Emulador\Xbox360\Emulators\Xenia Canary\xenia_canary.exe` (build `canary_experimental@7ef873b0d`, 03/08/2026), só lido. O teste usou uma cópia isolada. |

### Conteúdo de "Banjo Tooie BR" (pacote XBLA já extraído)

| Arquivo | Tamanho | O que é (verificado) |
|---|---|---|
| `default.xex` | 7.413.760 | XEX2 PowerPC BE, **Encryption None / Compression None** (cabeçalho 0x2000 + imagem 0x710000). Title ID `0x58410955`, Media ID `0x65D27094`, versão `0.0.1.2` (= versão base: sem title update). Nome PE original `Banjo2_CPR.exe`, compilado em 26/02/2009. |
| `RAWFiles\db360.textures.cmp` | 50.625.264 | Texturas compactadas (formato próprio) |
| `RAWFiles\banjo2_360.tbl` / `.ctl` | 14.555.180 / 187.992 | Tabelas/índice de dados |
| `RAWFiles\db360.cmp` | 12.525.322 | Banco de dados compactado (contém o texto do jogo, PT-BR observado em execução) |
| `RAWFiles\xarialuni.ttf` | 6.608.540 | Fonte Unicode do jogo |
| `RAWFiles\58410955033F0001` | 5.206.016 | Contêiner PIRS só com o **tema NXE** do dashboard (35 imagens), não é dado de jogo |
| `RAWFiles\X360_strings.dat` | 427.089 | Strings de sistema do XBLA (inclui PT-BR) |
| `RAWFiles\ArcadeNormal.ptc` / `ArcadeWide.ptc` | 101.229 / 99.557 | Layouts de tela do XBLA |
| `ArcadeInfo.xml` + 24 PNGs | ~0,4 MB | Metadados/ícones/conquistas da loja |

Hashes de todos: `app/_audit/out/inventory.json` (rev. 1), reconferidos em
`docs/evidencias/linha_de_base_hashes.json` (36/36 iguais).

---

## 3. Comparação: código local × base principal

### 3.1 Componentes

| Componente | Local (`app/`) | Base (iron-jay @ `72c2397`) |
|---|---|---|
| Executável original (XEX) | ✔ fornecido (decifrado) | não distribuído; o launcher testa o hash `4b512ae9…` do arquivo como sai do pacote STFS |
| Código recompilado (~3,4 M linhas, `rexglue codegen`) | ✘ | ✘ não versionado (artefato de build) |
| Manifesto do recompilador `banjotooie_manifest.toml` | ✘ | ✔ (ReXGlue 0.8.1.4, `game_root = ../xbla/files`) |
| Overrides `overrides.toml` (setjmp/longjmp + 47 entradas de função) | ✘ | ✔ |
| Host harness (`src/main.cpp`, `banjotooie_app.h`, CMake + presets) | ✘ | ✔ |
| Ponte gráfica / runtime (Vulkan/D3D12, SDL, imgui, entrada, áudio) | ✘ | ✔ no submódulo `iron-jay/rexglue-sdk` @ `035aa25` |
| Launcher (tkinter + extrator STFS + checagem de hash) | ✘ | ✔ (recusa e **apaga** os dados extraídos se o hash diferir) |
| Testes automatizados | ✔ só ferramentas de auditoria (seção 4) | só scripts shell de smoke do Steam Deck; nada no Windows |
| Binário Windows | ✘ | não versionado |

### 3.2 Integridade do seu `default.xex` — PASS

`_audit/verify_xex_integrity.py` → `docs/evidencias/xex_integridade.json`

- Os 113 descritores de página do *security info* (blocos de 64 KiB) conferem:
  **SHA-1(bloco_i) = digest_i para 113/113**. A raiz `0x114` = SHA-1(tabela de descritores) também
  confere.
- Conclusão: código e dados embutidos no XEX são **byte a byte** os registrados no cabeçalho.
  Nenhuma tradução ou patch foi aplicado ao XEX. *Limite:* a assinatura RSA do cabeçalho não foi
  verificada (exigiria a chave pública da Microsoft).

### 3.3 Overrides da base × seu XEX — 49/49 conferem

`_audit/check_overrides.py` → `docs/evidencias/overrides_vs_xex.json` / `.log`

Cada endereço do `overrides.toml` foi testado contra as previsões que os comentários da própria
base fazem:

| Grupo | Teste | Resultado |
|---|---|---|
| `setjmp` 0x8259FD90 / `longjmp` 0x8259FA70 | setjmp lê o gancho em 0x826CCC58 e é chamado por `bl` (7×); longjmp é início de função (pdata) | ✔ |
| 4 alvos de *tail-call* (0x8223F200, 0x8223AC28, 0x82523A78, 0x822E8C00) | existe um `b` apontando para cada um | ✔ 4/4 |
| 0x82415170 (comparador indireto) | instrução anterior é fronteira (`b`) | ✔ |
| 41 entradas achadas por varredura de ponteiros | o endereço aparece como ponteiro em `.rdata`/`.data` | ✔ 41/41 |
| 0x82280AF8 (crash do wigwam) | `b 0x82280AC4` em 0x82280AF4, `blr` em 0x82280B98, nenhum ponteiro em dados | ✔ |

**Leitura:** a chance de 49 previsões independentes acertarem num layout diferente é desprezível.
O código está nos mesmos endereços do build que a base testou. A diferença de hash do arquivo
vem do formato: o seu está decifrado e descomprimido; o `stfs_extract.py` da base diz que o
ReXGlue decifra o XEX sozinho, ou seja, a base testou o arquivo ainda cifrado. *Limite:* não temos
o arquivo da base para comparar a imagem inteira; o pacote STFS original não está mais em
`W:\Emulador\Xbox360\Roms` (pasta vazia).

### 3.4 Patches da comunidade (Xenia) × seu XEX — ganchos conferem

`_audit/check_xenia_patches.py` → `docs/evidencias/xenia_patches_vs_xex.json` / `.log`
(arquivo lido: `W:\Emulador\...\patches\58410955 - Banjo-Tooie.patch.toml`, autores illusion,
retroben e wedarobi)

| Patch | Pontos | O que o original tem (seu XEX) | Ganchos conferem? |
|---|---|---|---|
| **60 FPS** | 2 ganchos + 17 constantes de tempo + caverna 0x825F4000 | `0x82276E34: li r9,2` … `0x82276E3C: sth r9,-4(r11)` (grava 2 em `0x8269B1BC`); `0x822F9F10..18: stwu r6,4(r3); stw r3,0x30(r31); lwz r11,0x351C(r31)` | ✔ 2/2: as instruções originais estão realocadas na caverna; caverna vazia no original |
| **Draw Distance Hack** | 3 pontos + caverna 0x825F4100 | `0x82240DF8: slwi r3,r3,2` (substituído por tabela de distâncias por mapa); `0x8223F714`/`0x822A9F84`: desvios condicionais | ✔ caverna vazia; o gancho substitui (não realoca) a instrução |
| **Nests And Stairs LOD Hack** | 2 ganchos + caverna | — | ✔ 2/2 realocadas |
| **HD Shadows** | 8 constantes de 16 bits em `.data` | valores originais registrados no JSON | (sem gancho) |

- Media ID citado no patch (`65D27094`) = Media ID do seu XEX.
- O Xenia calculou para o seu arquivo `Module Hash 324176DDF940D212` (smoke test); o patch espera
  `CFE7D4B66FAC7096`. Por isso **o Xenia não aplicaria esses patches ao seu arquivo decifrado**,
  embora o código seja o mesmo. Isso não afeta o port, que aplicará mudanças no código recompilado.

---

## 4. Verificações não destrutivas executadas

### 4.1 Comandos e resultados

| Comando | Resultado | Evidência |
|---|---|---|
| `python -I _audit/inspect.py` (rev. 1): inventário + sha256 | ✔ 36 arquivos | `_audit/out/inventory.json` |
| `python -I _audit/verify_baseline.py <jogo> inventory.json …` (antes do smoke) | **PASS** 36/36 iguais, 0 novos/faltando | `docs/evidencias/linha_de_base_hashes.json` |
| `python -I _audit/verify_xex_integrity.py default.xex …` | **PASS** cadeia SHA-1 113/113 + raiz | `docs/evidencias/xex_integridade.json` |
| `python -I _audit/check_overrides.py default.xex overrides.toml functions.txt …` | **PASS** 49/49 | `docs/evidencias/overrides_vs_xex.{json,log}` |
| `python -I _audit/check_xenia_patches.py default.xex <patch.toml> …` | ganchos 60 FPS 2/2, LOD 2/2; cavernas vazias; hash do patch ≠ (esperado, ver 3.4) | `docs/evidencias/xenia_patches_vs_xex.{json,log}` |
| `pwsh -File _audit/check_tools.ps1` | git 2.56, cmake 4.4.0-rc3, ninja 1.13.2, clang 22.1.8 (msys2 clang64), Python 3.14.6, Java 21; VS 2026 BuildTools/Community (MSVC 14.51.36231) + VS 2022 BT (14.44); Windows SDK 10.0.22621/26100; `vulkan-1.dll` presente; **RTX 5080 em 3840×2160 @ 60 Hz**; Xenia encontrado | `docs/evidencias/ferramentas.log` |
| `git ls-remote <7 repositórios de referência> HEAD` | base = `72c2397` (= clone local), fork rexglue = `035aa25` (= pino); demais HEADs registrados | `docs/evidencias/upstream_heads.log` |
| Leitura de bytes via Craft Anything (`native_bytes_show`) | `0x82276E28` = `3d60826a 39000000 396bb1c0 39200002 814b0000 b12bfffc`; `0x822F9F08` = `… 94c30004 907f0030 817f351c …`; `0x825F39C8+1592` = zeros | `ev_e8df486c…`, `ev_34ebcee6…`, `ev_1afa0738…` |
| Smoke test do original (2 execuções, seção 4.3) | execução 2: **PASS até a tela de abertura** | `docs/evidencias/smoke_xenia_original.md`, `ev_cb3bce86…` |
| `python -I _audit/verify_baseline.py …` (depois do smoke) | **PASS** 36/36 iguais | `docs/evidencias/linha_de_base_hashes_pos_smoke.json` |
| Rev. 1: Ghidra 12.1.3 + XEXLoaderWV (import + dump headless) | ✔ 16.996 funções, 63.750 chamadas | `_audit/out/ghidra_xex_out/` (regenerável: `_audit/run_headless.cmd`) |

### 4.2 Testes que NÃO puderam ser executados

| Teste | Status | Causa |
|---|---|---|
| Build do port | **BLOQUEADO (nesta missão)** | Não há código do port localmente; trazê-lo e compilá-lo é implementação (M1–M3) |
| Smoke test do port | **BLOQUEADO** | Binário não existe |
| Menu START, jogabilidade, saves, gamepad (original ou port) | **PENDENTE** | Exige injetar entradas de controle. Nenhum harness de entrada automatizada existe ainda; o smoke parou na tela de abertura sem entrada |
| FPS real do original | **PENDENTE** | O Xenia não registra FPS no log; o valor de 30 FPS vem do código (inferido) |
| Assinatura RSA do cabeçalho do XEX | **NÃO EXECUTADO** | Exigiria a chave pública da Microsoft; a cadeia SHA-1 da imagem foi verificada |
| Comparar a imagem inteira com o XEX da base | **BLOQUEADO** | O pacote STFS original não está mais em disco e a base não distribui o arquivo |
| Checagem automática do Craft Anything (`rebuild_check`) | **FAIL esperado, não aplicável** | Ela procura um projeto Unity em `app/` ("No Unity project found"). O stack deste projeto é C++/CMake + ReXGlue, e esta missão proíbe implementar, então nenhum projeto Unity foi criado e nada foi declarado PASS por causa dela |

### 4.3 Smoke test do jogo original (resumo; detalhes em `docs/evidencias/smoke_xenia_original.md`)

- Cópia do Xenia em `app/_audit/out/xenia_smoke/` em modo portátil, **sem patches**; pasta do jogo
  montada como somente leitura.
- Execução 1 (`--apu=nop`): tela preta aos 51 s; log mostra `CreateDriver failed` (áudio desligado
  por mim). Lição para o port: **sem dispositivo de áudio o jogo não avança**. A base precisou de
  um "audio-device fallback".
- Execução 2 (`--apu=any --mute=true`): aos 62 s, **tela de abertura renderizada com "PRESSIONE
  START PARA JOGAR"**, D3D12, proporção 16:9.
- Os arquivos do jogo continuam idênticos depois (36/36) e a instalação do Xenia do usuário não
  foi alterada.
- A captura não foi versionada no Git (é arte do jogo). O sha256 está registrado.

---

## 5. Matriz de requisitos

Legenda: **IMPLEMENTADO** = existe e foi comprovado por execução/verificação real ·
**PARCIAL** = parte comprovada · **PENDENTE** = não existe ou não foi verificado ·
**BLOQUEADO** = impedido por uma causa concreta.

| Requisito | No jogo original (seus arquivos) | No port nativo Windows (o produto) | Evidência / observação |
|---|---|---|---|
| Arquivos fornecidos íntegros e inicializáveis | **IMPLEMENTADO** | — | 36/36 hashes; 113/113 blocos; boot até a abertura no Xenia |
| Executável Windows x64 recompilado | — | **PENDENTE** | Nada compilado localmente; a base tem o caminho de build |
| Compatibilidade do seu XEX com a base | — | **PARCIAL** (forte) | 49/49 overrides conferem; imagem inteira não comparável |
| Tela de abertura + menu START | **PARCIAL**: "PRESSIONE START PARA JOGAR" renderizada; o menu não foi aberto (sem entrada) | **PENDENTE** | smoke 4.3. Se "menu START" for um menu de opções do port (como nos recomps de referência), ele não existe em lugar nenhum |
| PT-BR | **PARCIAL**: texto in-game PT-BR observado na abertura; strings de sistema XBLA em PT-BR; o diálogo de erro fatal embutido no XEX não tem PT-BR (EN/DE/FR/ES/IT/JA) | **PENDENTE** (deve ser herdado dos dados) | smoke 4.3; `MECHANICS.md` §3; cobertura total (menus, diálogos, HUD) não verificada |
| 4K | **PENDENTE** (XBLA renderiza em resolução de console; a config do Xenia do usuário usa escala 7×, sem teste meu) | **PENDENTE** | hardware pronto: RTX 5080 + 3840×2160 @ 60 Hz (`ferramentas.log`); escala de render do ReXGlue a validar |
| 60 FPS | **Original = 30 FPS (inferido)**: intervalo de apresentação 2 em `0x82276E34` | **PENDENTE**: rota conhecida (2 ganchos + 17 constantes de tempo do patch "60 FPS", ganchos conferem) | 3.4; `ev_e8df486c…` |
| Draw distance | valores originais | **PENDENTE**: rota conhecida (Draw Distance Hack: 3 pontos; LOD Hack: 2 ganchos) | 3.4 |
| Gamepad | não testado | **PENDENTE** (a base afirma suporte a controle + teclado/mouse) | README da base; não verificado aqui |
| Saves | não testado (o jogo grava em `savedrive:\savegame.dat`) | **PENDENTE** (a base afirma save/load) | strings do XEX (rev. 1) |
| QoL | — | **PENDENTE**: candidatos com rota conhecida: HD Shadows, LOD, draw distance | 3.4 |
| Launcher com validação | — | **BLOQUEADO para este arquivo**: o launcher da base exige o pacote STFS e o hash `4b512ae9…`, e **apaga** os dados se diferir | `launcher.py` linhas 60–67 e 524–535 |
| Testes automatizados | ferramentas de auditoria reprodutíveis (5 scripts) | **PENDENTE**: a base não tem suíte no Windows | seção 4.1 |

### Bloqueios técnicos e riscos

1. **Sem código/binário do port local.** Resolve-se no M1–M3.
2. **Launcher da base incompatível com a sua pasta**: ele espera o pacote STFS e um hash do arquivo
   cifrado. Rota sem contornar nada: rodar `banjotooie.exe --game_data_root <sua pasta>`
   diretamente (a base documenta esse parâmetro) ou adaptar o launcher para validar pela cadeia
   de hashes da imagem, como em 3.2.
3. **Codegen com o XEX decifrado:** a base gera código a partir do arquivo cifrado. É preciso
   confirmar no M2 que o `rexglue codegen` aceita o arquivo sem cifra e gera o mesmo conjunto de
   funções (esperado, pois a imagem é a mesma; não verificado).
4. **Áudio:** sem dispositivo de áudio o jogo fica em tela preta (execução 1). O port precisa do
   fallback de áudio do fork.
5. **Layout de dados:** o launcher da base duplica 9 arquivos em `RAWFiles\` e na raiz; a sua pasta
   só tem `RAWFiles\`. No Xenia isso bastou para chegar à abertura. Nas fases seguintes, não
   verificado.
6. **Testes de jogabilidade exigem entrada automatizada** (harness de controle/teclado), que ainda
   não existe.
7. Caminhos fixos da máquina do autor em `rexenv.ps1` da base: adaptar no M3.

---

## 6. Mapa de módulos e ordem de implementação (próximas missões, não iniciadas)

```
 Banjo Tooie BR\ (seus arquivos, só leitura)
   ├─ default.xex ──► [M2] rexglue codegen ──► generated/ (C++ recompilado)
   │                     ▲ overrides.toml (49 pontos, conferidos)
   └─ RAWFiles\ ─────────────────────────────┐ (--game_data_root)
                                             ▼
 [M1] base + submódulo ReXGlue ──► [M3] CMake/Ninja/Clang ──► banjotooie.exe + rexruntime.dll
        runtime: gráfico (D3D12/Vulkan), entrada (XInput/SDL), áudio (XAudio2 + fallback), VFS, saves
                                             │
       [M5] harness de testes (captura, entrada automatizada, --self-test) ◄─┘
       [M6] PT-BR   [M7] 4K/60 (escala de render + intervalo de apresentação)   [M8] QoL (draw distance, LOD, sombras)
       [M9] launcher/pacote final (sem redistribuir dados)
```

| # | Módulo | O quê | Por que nesta ordem |
|---|---|---|---|
| M0 | Linha de base | Esta auditoria (feito) | Sem base não há comparação |
| M1 | Trazer a base | Clonar o port + submódulo `iron-jay/rexglue-sdk@035aa25` para dentro de `app/`, sem dados do jogo | Sem código não há build |
| M2 | Codegen | `rexglue codegen` sobre o **seu** XEX; comparar o conjunto de funções e os 49 overrides com o esperado | Prova que o XEX decifrado funciona antes de gastar tempo compilando (risco 3) |
| M3 | Build Windows | Adaptar o ambiente e rodar `cmake --preset win-amd64-release` → `banjotooie.exe` | Produto executável mínimo |
| M4 | Dados | `--game_data_root` apontando para a sua pasta, sem copiar; checar o layout `RAWFiles\` | O jogo não roda sem os dados (risco 5) |
| M5 | Smoke + harness | Abertura → menu START → introdução → jogo; entrada automatizada; logs/capturas; teste de áudio | Primeira prova real do port. Os testes das fases seguintes dependem do harness (risco 6) |
| M6 | PT-BR | Verificar a cobertura da tradução (menus, diálogos, HUD) e decidir sobre o diálogo de erro do XEX (sem PT-BR) | A tradução já está nos dados; só falta confirmar e completar |
| M7 | 4K/60 | Escala de render do runtime para 3840×2160; 60 FPS pelo intervalo de apresentação (`0x82276E34`, `0x822F9F10`) + constantes de tempo; medir o FPS | Depende do port estável (M5) e do harness para medir |
| M8 | QoL | Draw distance, LOD, sombras e um menu de opções, se for o caso | Depois do jogo estável e medido |
| M9 | Entrega | Launcher adaptado (validação pela cadeia de hashes, não pelo hash do arquivo) e pacote final | Pacote para você, sem redistribuir dados |

---

## 7. Pendências abertas

| # | Pergunta | Severidade | O que a resolveria |
|---|---|---|---|
| 1 | O `rexglue codegen` aceita o XEX decifrado e gera exatamente as funções esperadas? | Alta | M2 |
| 2 | A tradução PT-BR cobre todo o texto do jogo (menus, diálogos, HUD, conquistas)? | Média | Jogar ou extrair o texto de `db360.cmp` no port |
| 3 | O pacote é a versão completa ou a demo? | Baixa | Rodar com licença completa (port) ou perguntar à pessoa |
| 4 | Como o runtime ReXGlue escala para 4K, e o intervalo 2 em `0x82276E34` é mesmo o limitador de FPS? | Média | Docs/código do SDK no M1; medir no M7 |
| 5 | O que você chama de "menu START": o menu do próprio jogo ou um menu de opções do port? | Baixa | Perguntar a você antes do M8 |

---

## 8. Evidências (arquivos)

- `docs/evidencias/linha_de_base_hashes.json` e `…_pos_smoke.json`: 36/36 hashes antes e depois
- `docs/evidencias/xex_integridade.json`: formato, informações de execução, cadeia SHA-1 113/113
- `docs/evidencias/overrides_vs_xex.json` / `.log`: 49/49
- `docs/evidencias/xenia_patches_vs_xex.json` / `.log`: patches × XEX
- `docs/evidencias/smoke_xenia_original.md`: smoke test
- `docs/evidencias/ferramentas.log`, `docs/evidencias/upstream_heads.log`
- Ferramentas (reproduzíveis): `_audit/verify_baseline.py`, `_audit/verify_xex_integrity.py`,
  `_audit/check_overrides.py`, `_audit/check_xenia_patches.py`, `_audit/check_tools.ps1`
- Craft Anything: `ev_cb3bce86…` / `ev_e66dc0d1…` (capturas), `ev_e8df486c…`, `ev_34ebcee6…`,
  `ev_1afa0738…` (bytes); leituras do Ghidra listadas em `MECHANICS.md`
- Não versionados (regeneráveis ou arte do jogo): `_audit/out/` (imagem do XEX, mapas do Ghidra,
  Xenia isolado, capturas e logs do smoke)

## 9. Rollback e situação da memória

- **Rollback:** os arquivos do jogo nunca são escritos (hashes antes e depois). Esta revisão só
  adicionou ou alterou arquivos em `app/`. Para desfazer: `git -C app revert <commit desta
  revisão>`, ou `git -C app checkout d938a31 -- .` seguido de commit. A pasta
  `app/_audit/out/xenia_smoke/` pode ser apagada sem efeito colateral.
- **Memória Understory:** **BLOCKED_MCP**. As ferramentas `memory_status`/`memory_query` não
  estão disponíveis nesta sessão. Não existe `.understory-project-id` em `W:\Decomp\Banjo 3`, e ele
  não foi criado: não dá para verificar uma identidade já existente sem o MCP, e esta sessão só
  pode escrever dentro de `app/`. Também não houve gravação na fila
  `.understory/queue/pending/` (fora de `app/`), então a fila fica em **BLOCKED_QUEUE**. As
  descobertas estão registradas no histórico da investigação do Craft Anything
  (`investigation.cany`) e neste relatório.

## 10. Próximos passos recomendados (não iniciados)

1. **Missão seguinte (quando você pedir):** M1 + M2. Trazer a base e gerar o código a partir do
   seu XEX, confirmando a pendência nº 1.
2. M3–M5: compilar, rodar com os seus dados e criar o harness de testes com entrada automatizada.
3. M6–M7: confirmar a cobertura do PT-BR; 4K pela escala de render; 60 FPS pelos pontos da seção 3.4.
4. M8–M9: QoL e pacote final.
