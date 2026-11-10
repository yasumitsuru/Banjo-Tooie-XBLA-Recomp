# MISSÃO 00 — Auditoria do estado atual (relatório)

**Data:** 2026-11-09/10 · **Ambiente:** Windows 11 x86-64 · **Modo:** somente leitura/não destrutivo — nenhuma funcionalidade implementada, nenhum arquivo original modificado.

---

## 1. Resumo executivo

O projeto é o port nativo para PC do **Banjo-Tooie (XBLA)** via recompilação estática (PPC → C++), com base pública em [iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp) + SDK [ReXGlue](https://github.com/rexglue/rexglue-sdk). O computador já tem **quase tudo**: os arquivos do jogo fornecidos (36 arquivos, ~105 MB), e a cadeia de ferramentas completa (CMake, Ninja, Clang 22, MSVC/SDK do VS2026, Git, Python, Ghidra com suporte a XEX). O que **não existe localmente** é o código do port e o executável recompilado — isso é o que a próxima missão traz.

Três pontos de atenção descobertos:

1. **O `default.xex` fornecido não é o mesmo build que o port testou** (hash diferente). O launcher da base recusaria este pacote. Há indícios de que o layout de código é parecido (endereço do `setjmp` bate), mas isso precisa ser confirmado.
2. **PT-BR existe só nas strings de sistema do XBLA** (mensagens de armazenamento/demo/LIVE). O texto dentro do jogo (menus, conquistas, diálogos) está em inglês/francês/alemão/espanhol/italiano — **a tradução in-game ainda não existe nos arquivos fornecidos**.
3. **Não há emulador de Xbox 360 (Xenia)** instalado, então o jogo original não pôde ser executado aqui; a validação será feita contra o próprio executável recompilado.

> ⚠️ Honestidade de evidência: **nada foi executado nesta missão** (nenhum build existia). A afirmação "jogável de ponta a ponta" sobre a base vem do README do autor do port — não foi verificada aqui e está marcada como PARCIAL/PENDENTE.

---

## 2. Raiz do projeto, arquivos fornecidos e ferramenta de build

| Item | Localização |
|---|---|
| Raiz do workspace | `W:\Decomp\Banjo 3` |
| Arquivos do jogo (fornecidos) | `W:\Decomp\Banjo 3\Banjo Tooie BR` (36 arquivos) |
| Área de trabalho do projeto | `W:\Decomp\Banjo 3\app` (criada nesta missão; report + auditoria) |
| Ferramenta de build | **Não existe localmente.** Será trazida da base: CMake + Ninja + Clang + ReXGlue (todos os requisitos já instalados — seção 4) |

### Conteúdo de "Banjo Tooie BR" (extrato de um pacote XBLA/STFS)

| Arquivo | Tamanho | O que é (verificado) |
|---|---|---|
| `default.xex` | 7.413.760 | Executável XEX2 (PowerPC BE). Entry 0x825B3AF8, imagebase 0x82000000, **sem criptografia**. Title ID 0x58410955. Contém o código e parte dos textos (conquistas) do jogo. |
| `RAWFiles\db360.textures.cmp` | 50.625.264 | Texturas compactadas (formato próprio, não zlib) |
| `RAWFiles\banjo2_360.tbl` | 14.555.180 | Tabelas de dados do jogo (formato N64/360 da Rare) |
| `RAWFiles\db360.cmp` | 12.525.322 | Banco de dados do jogo compactado (formato próprio) |
| `RAWFiles\xarialuni.ttf` | 6.608.540 | Fonte Unicode usada pelo jogo (cobre os acentos do PT-BR) |
| `RAWFiles\58410955033F0001` | 5.206.016 | Contêiner PIRS com **apenas o pacote de tema NXE** (35 papéis de parede/camadas de dashboard) — não é dado de jogo |
| `RAWFiles\X360_strings.dat` | 427.089 | Strings do XBLA (UTF-16LE) — **inclui PT-BR nas mensagens de sistema** |
| `RAWFiles\banjo2_360.ctl` | 187.992 | Controle/índice (companheiro do .tbl) |
| `RAWFiles\ArcadeNormal.ptc` / `ArcadeWide.ptc` | 101.229 / 99.557 | "PTC+MSHM" (layouts de tela do XBLA) |
| `ArcadeInfo.xml` | 7.868 | Metadados XBLA (UTF-16BE): 12 conquistas, 200G, 1–4 jogadores, ScoreName "Jiggies", **descrição oficial pt-br** |
| 24 PNGs | 0,4 MB | Ícones de loja (32/64/420px), 12 conquistas (64×64), classificações etárias |

Total: 36 arquivos, ~105 MB. Sha256 de cada arquivo: `app/_audit/out/inventory.json`.

**Referências de layout encontradas dentro do XEX** (strings): o jogo lê `db360.cmp`, `db360.textures.cmp`, `banjo2_360.tbl/.ctl`, `RAWFiles\X360_strings.dat`, `RAWFiles\ArcadeNormal.ptc`, `RAWFiles\ArcadeWide.ptc`, `RAWFiles\58410955033F0001`, `RAWFiles\db360_TexFileList.txt` (este último **não está** no pacote fornecido — verificar se é opcional), e salva em `savedrive:\savegame.dat`.

---

## 3. Comparação: código local × base principal

**Local:** nenhum código-fonte. Só os arquivos do jogo (acima).

**Base (iron-jay/banjo_tooie_xbla_recomp, clonada em temp para comparação):**

| Componente | Local | Base |
|---|---|---|
| Código executável (XEX) | ✔ presente (é o original) | referenciado, não distribuído |
| Código recompilado (~3,4M linhas C++, gerado por `rexglue codegen`) | ✘ ausente | ✘ não versionado (artefato de build regenerável) |
| Manifesto do recompilador (`banjotooie_manifest.toml`) | ✘ | ✔ (ReXGlue 0.8.1.4) |
| Overrides de funções (`overrides.toml`: setjmp/longjmp + ~50 funções de correção de crash) | ✘ | ✔ |
| Ponte gráfica (runtime ReXGlue: Vulkan/SDL + imgui) | ✘ | ✔ no SDK (submodule `iron-jay/rexglue-sdk` @ 035aa25 — HEAD verificado) |
| Launcher (tkinter + extrator STFS + validação de hash do XEX) | ✘ | ✔ |
| Testes automatizados | ✘ | Parciais: só scripts shell de smoke no Steam Deck; sem suíte no Windows |
| Binário Windows | ✘ | Não versionado (build) |

**Lacunas identificadas:** código recompilado ausente; hash do XEX local ≠ hash testado pela base (`4b512ae9…` vs `3efca627…` — o launcher recusa); PT-BR in-game ausente; `db360_TexFileList.txt` referenciado mas não fornecido; nenhuma suíte de testes no Windows.

---

## 4. Verificações não destrutivas executadas

### 4.1 Comandos e resultados

| Comando | Resultado |
|---|---|
| Inventário + sha256 dos 36 arquivos (Python, `app/_audit/inspect.py`) | ✔ 36 arquivos, hashes em `inventory.json` |
| Listagem do PIRS interno (offsets validados = os do `stfs_extract.py` da base) | ✔ 35 entradas, todas do tema NXE |
| Decodificação de `ArcadeInfo.xml` (UTF-16BE) | ✔ inclui locale oficial `pt-br` |
| Probes de idioma (EN/PT-BR/FR/ES/DE/IT, UTF-16LE e ASCII) em `X360_strings.dat` e `default.xex` | ✔ PT-BR presente só nas strings de sistema |
| Extração de 4.446 strings UTF-16LE e 16.235 strings ASCII do XEX | ✔ arquivos em `app/_audit/out/` |
| Import do XEX no Ghidra 12.1.3 (extensão "XEX Loader by Warranty Voider") | ✔ importou e analisou: PowerPC:BE:64:A2ALT-32addr, 194 imports; `setjmp` em 0x8259FD90 (mesmo endereço do overrides da base) |
| `git --version` / `cmake --version` / `ninja --version` / `clang --version` / VS via vswhere / Windows SDK | ✔ 2.56 / 4.4.0-rc3 / 1.13.2 / 22.1.8 / VS2026 BT (MSVC 14.51) + SDK ✔ |
| Verificação do submodule da base (`git ls-remote iron-jay/rexglue-sdk`) | ✔ HEAD = 035aa25 (bate com o pin) |
| `xenia.exe` (emulador) nos locais comuns | ✘ não instalado |
| Decompressão de `db360.cmp` (variantes zlib) | ✘ formato próprio da Rare (o jogo mesmo descomprime em runtime; não é bloqueio) |

### 4.2 Testes que NÃO puderam ser executados (com causa)

| Teste | Status | Causa |
|---|---|---|
| Build do port | **BLOQUEADO** | Não há código local ainda; build pertence à próxima missão |
| Smoke test do jogo original | **BLOQUEADO** | XEX de Xbox 360 não roda em Windows; Xenia não está instalado |
| Smoke test do port recompilado | **BLOQUEADO** | Binário não existe localmente |
| Verificação visual do estilo (PNGs) | **BLOQUEADO (parcial)** | O modelo do agente não exibe imagens; a análise ficou em dimensões/metadados (`png_dims.json`). O estilo será estudado com o jogo rodando. |

**Nenhum PASS foi declarado por simulação** — todo item de jogo (gameplay, menu, saves) está marcado como não verificado.

---

## 5. Matriz de requisitos

| Requisito | Status | Evidência / justificativa |
|---|---|---|
| Executável Windows x64 do jogo (recompilado) | **PENDENTE** | Existe na base (README); nada compilado localmente |
| Tela título + menu START | **PARCIAL** | Base reporta boot até o menu; não verificado aqui |
| Jogabilidade (andar/voar/atacar) | **PENDENTE** | Não verificada; base reporta jogável |
| **PT-BR — sistema XBLA** | **IMPLEMENTADO** | Strings em `X360_strings.dat` ("Sem espaço livre", "Jogar sem salvar", "Sair para o menu principal"…) |
| **PT-BR — dentro do jogo** (menus, diálogos, HUD, conquistas) | **PENDENTE** | Nenhum texto PT-BR in-game encontrado no XEX; tradução a fazer/confirmar com a pessoa |
| **4K** | **PENDENTE** | Runtime ReXGlue permite janela redimensionável; escala de render a validar pós-build |
| **60 FPS** | **PENDENTE** | Taxa original do XBLA a confirmar (provável 30); desbloqueio pode exigir hook |
| **Draw distance** | **PENDENTE** | Herdada do original; melhoria futura (QoL) |
| **Gamepad** | **PARCIAL** | Base suporta controle + teclado/mouse (README); não verificado aqui |
| **Saves** | **PARCIAL** | Base suporta save/load (README); não verificado aqui |
| **QoL (qualquer)** | **PENDENTE** | Nada implementado em lugar nenhum ainda |
| Launcher com validação de integridade | **PARCIAL** | Existe na base, mas **recusa o pacote local** (hash ≠) |
| Testes automatizados | **PENDENTE** | Base só tem scripts shell (Steam Deck); nada no Windows |

### Bloqueios técnicos

1. **Hash do XEX local ≠ build testado pelo port.** O launcher valida e recusa. Resolução: gerar o mapa de funções do XEX do usuário (codegen) e comparar com a base; se os endereços baterem, basta ajustar a validação; se não, os overrides precisam ser regenerados. *(Desconhecido alto — ver seção 7.)*
2. **Tradução in-game ausente.** Escopo a decidir com a pessoa (menus/diálogos/HUD/conquistas) — afeta carga de trabalho e a forma de aplicar (patches nas strings do XEX).
3. **Sem Xenia** para rodar o original. A referência de comportamento será o port recompilado em si (e o código decompilado no Ghidra, que já funciona).
4. **4K/60 não verificados** — depende de M7 (abaixo) e de medições reais.
5. Caminhos hardcoded no `rexenv.ps1` da base (máquina do autor) — adaptar para esta máquina no M3.

---

## 6. Mapa de módulos e ordem de implementação (próximas missões)

| # | Módulo | O quê | Por que nesta ordem |
|---|---|---|---|
| M0 | Linha de base | Inventário, auditoria, checkpoint git (feito **nesta missão**) | Sem base não há comparação |
| M1 | Trazer a base | Clonar o port + submodule ReXGlue (fork 035aa25) para dentro de `app/` | Sem código não há build |
| M2 | Codegen | Gerar o código recompilado a partir do XEX **do usuário** e comparar o mapa de funções com a base | Resolve o bloqueio 1 (hash) antes de gastar tempo compilando |
| M3 | Build Windows | Adaptar ambiente (clang/ninja/MSVC), `cmake --preset win-amd64-release`, produzir `banjotooie.exe` + `rexruntime.dll` | Produto executável mínimo |
| M4 | Dados | Apontar `game_data_root` para a pasta original do usuário (sem copiar os arquivos) | Jogo não roda sem os dados |
| M5 | Smoke real | Roda título → menu START → introdução → jogabilidade; testa teclado/mouse, gamepad e saves; guarda logs/capturas | Primeira prova real de jogo |
| M6 | PT-BR | Com a pessoa: escopo da tradução in-game; aplicar e testar (backup do original) | Requisito central do projeto |
| M7 | 4K/60 | Validar resolução/escala de render, VSync, medir FPS; hooks se necessário | Requisito central de desempenho |
| M8 | QoL | Draw distance, conforto, melhorias escolhidas pela pessoa | Depois do jogo estável (M5) |
| M9 | Entrega | Launcher de arquivo único (PyInstaller) + opções de autoteste/captura para as checagens automáticas | Pacote final para a pessoa |

**Nota de stack:** o caminho do projeto é **C++/CMake + ReXGlue (recompilação estática)** — preserva o jogo original 1:1, como pede o contexto fixo da missão. Um remake em outra engine (ex.: Unity) seria outro projeto, do zero. O registro `craft.json` marca "Unity" por padrão; fica anotado para a missão 01 confirmar o stack com a pessoa, se preciso.

---

## 7. Pendências abertas (perguntas sem resposta definitiva)

| # | Pergunta | Severidade | O que a resolveria |
|---|---|---|---|
| 1 | O que exatamente difere entre o XEX do usuário e o build testado pelo port — dados ou código? | **Alta** | Mapa de funções via codegen; ou a pessoa informar a origem do pacote |
| 2 | O texto in-game está traduzido em algum lugar ou a tradução precisa ser feita? | Média | Auditoria total de strings; ou a pessoa confirmar o que tem |
| 3 | O pacote é a versão completa ou demo? | Baixa | Rodar o jogo; indicativos (12 conquistas, ArcadeInfo completo) apontam para completa |
| 4 | Qual a resolução/FPS originais do XBLA e como o runtime permite 4K/60? | Média | Docs do SDK + medições pós-build |

---

## 8. Evidências (arquivos desta auditoria)

- `app/_audit/out/inventory.json` — 36 arquivos com sha256
- `app/_audit/out/pirs_list.txt` / `pirs_list.json` — conteúdo do contêiner interno
- `app/_audit/out/language_probes.json` — contagem de palavras por idioma
- `app/_audit/out/strings16_full.txt` — 4.446 strings UTF-16LE do `X360_strings.dat`
- `app/_audit/out/xex_strings_ascii.txt` — 16.235 strings ASCII do XEX
- `app/_audit/out/xex_header.json` / `png_dims.json`
- Log do Ghidra: `C:\Users\<usuario>\AppData\Roaming\ghidra\ghidra_12.1.3_PUBLIC\application.log`
- Clone de referência da base: `C:\Users\<usuario>\AppData\Local\Temp\opencode\bt-xbla-base`
- Registro Craft Anything: findings fnd_1..fnd_6; unknowns unk_27e605ec…, unk_c318987d…, unk_ddf84578…, unk_f626b246…

## 8.1 Resultado da checagem automática (rebuild_check)

A checagem do Craft Anything desta rodada reportou "build: failed — No Unity project found in app/ yet". Isso é **esperado e correto para esta missão**: a MISSÃO 00 proíbe implementar e construir; não há (e não deve haver) build nesta rodada. O check está configurado para o fluxo padrão de "construir um jogo", que começa na próxima missão — quando o executável recompilado existir, será ele o alvo da checagem. Registrado aqui por transparência, sem declarar nada como PASS.

## 9. Próximos passos recomendados

1. **Missão 01:** M1+M2 — trazer a base e gerar/ comparar o mapa de funções (resolve o bloqueio nº 1).
2. Perguntar à pessoa: origem do pacote XBLA e o que ela entende por "já traduzido" (bloqueio nº 2).
3. M3–M5: compilar e fazer o primeiro smoke test real, com logs e capturas.
4. M6–M7: PT-BR in-game e 4K/60.
