# MISSÃO 01 — Port nativo para Windows 11 x86-64 (relatório)

**Data:** 2026-11-10 · **Branch Git:** `missao-01-port-nativo` (a partir de `1075821`, Missão 00 rev. 3)
**Rollback:** `git -C app checkout master` (o `master` continua em `1075821`), ou
`git -C app revert <commit desta missão>`. Os arquivos do jogo nunca são escritos (hashes antes e
depois, seção 5).

## 1. Decisão de arquitetura (com base na inspeção, não presumida)

| Opção | Veredito | Por quê (evidência) |
|---|---|---|
| **Reaproveitar a base iron-jay + runtime ReXGlue (fork `035aa25`)** | **Escolhida** | Mesmo jogo e mesma versão: 49/49 overrides da base conferem no seu XEX (auditoria §3.3); o fork traz o runtime completo (D3D12, kernel, áudio com fallback, entrada) e o gerador `rexglue codegen`; o leitor de XEX aceita arquivo sem cifra e sem compressão (auditoria §3.5) |
| XenonRecomp (hedge-dev/sonicnext-dev) ou XenonRecompUnlimited | Descartada | Só geram C++; não trazem runtime (gráfico, áudio, kernel). Seria reescrever o que o ReXGlue já tem |
| ReXGlue oficial (168 commits à frente do fork) | Adiada | A combinação jogável declarada pela base é o fork `035aa25`; atualizar fica para depois de o port rodar (risco de regressão) |
| Emulador (Xenia) | Fora do objetivo | A missão pede execução nativa, sem emulador tradicional |

## 2. Toolchain e build reproduzível

Tudo já estava instalado nesta máquina; nada foi instalado no sistema.

| Ferramenta | Versão | Observação |
|---|---|---|
| Visual Studio 2022 Build Tools | MSVC 14.44.35207, Windows SDK 10.0.26100 | ambiente por `vcvars64.bat` |
| Clang (incluído no VS 2022 BT) | **19.1.5**, alvo `x86_64-pc-windows-msvc` | o SDK exige ≥ 18 (`CMakeLists.txt` do SDK); o Clang 22 do MSYS2 gera para MinGW (`x86_64-w64-windows-gnu`) e não foi usado |
| CMake | 4.4.0-rc3 | `C:\Program Files\CMake` |
| Ninja | 1.13.2 | chamado por caminho completo, sem pôr o MSYS2 no PATH |
| Flags | `-march=x86-64-v3` | as mesmas da base (o SDK usa SSSE3/AVX2) |

**Um comando:** `tools\build_port.cmd` (ou por fase: `sdk`, `codegen`, `game`). Logs em
`_audit\out\m01\` (não versionados). Fases:

1. `materialize_symlinks.py` + configure/build do SDK → `rexglue-sdk\out\win-amd64\rexglue.exe`
2. `rexglue --force codegen banjotooie_manifest.toml` → `banjotooie-recomp\generated\` (não versionado)
3. `cmake --preset win-amd64-release -DREXSDK_DIR=../rexglue-sdk` + build → `banjotooie.exe` + `rexruntime.dll`

O runtime e as 22 dependências vêm como **submódulo Git fixado** (`rexglue-sdk` @ `035aa25`,
dependências nos commits fixados pelo SDK), então o mesmo comando reproduz o mesmo build.

## 3. O que mudou (arquivos)

| Arquivo | Mudança | Por quê |
|---|---|---|
| `.gitmodules`, `rexglue-sdk/` | submódulo `iron-jay/rexglue-sdk` @ `035aa25` (`ignore = dirty`) | runtime + gerador, sem cópia de código para o repositório |
| `banjotooie-recomp/` | copiado da base @ `72c2397` | projeto do jogo (manifesto, overrides, CMake, harness) |
| `banjotooie-recomp/banjotooie_manifest.toml` | caminhos → `../../Banjo Tooie BR` (só leitura); `sdk_version` 0.8.1.4 → 0.8.1.5 (migração automática do próprio `rexglue codegen`) | usar os seus arquivos sem copiar |
| `banjotooie-recomp/overrides.toml` | **+1** ponto: `0x822479EC` | o codegen sobre o seu XEX relatou `UnresolvedCall 0x822479EC from 0x825F3984`; mesmo padrão dos 4 alvos de *tail-call* da base (seção 4) |
| `banjotooie-recomp/src/main.cpp` | gancho de instrumentação em `sub_822FA358` | conta quadros apresentados (chama a função original) |
| `banjotooie-recomp/src/banjotooie_app.{h,cpp}` | ganchos do host | preflight, relatório de boot, janela atrás das outras nos testes, captura, teste de fumaça, códigos de saída |
| `banjotooie-recomp/src/port_diagnostics.{h,cpp}` | novo | preflight dos arquivos, relatório JSON, minidump em falha, PNG |
| `banjotooie-recomp/CMakeLists.txt` | + as 2 fontes novas, + `dbghelp` | build |
| `tools/build_port.cmd`, `tools/materialize_symlinks.py`, `tools/smoke_port.ps1` | novos | build reproduzível e teste automatizado |

## 4. Integração do código recompilado

- **Codegen sobre o XEX decifrado: funciona.** `rexglue codegen` leu o seu `default.xex` direto da
  pasta do jogo em ~11 s, gerou **22.111 funções** em 45 arquivos C++ recompilados (3.333.968
  linhas), mais `banjotooie_init.cpp`/`banjotooie_register.cpp`. A base cita "~22.000 funções" e
  "~3,4 M linhas". Isso fecha a pendência nº 1 da auditoria.
- **1 erro de análise na primeira geração:** `UnresolvedCall 0x822479EC from 0x825F3984 - target
  not in any function`. Os bytes mostram `0x825F3980: cmplwi cr6,r3,1` e `0x825F3984: b 0x822479EC`,
  que continua com `blt cr6` (trecho dividido pelo compilador; o Ghidra vê `FUN_822479ec`, 240
  bytes). No código recompilado os registradores de condição ficam no contexto compartilhado, então
  declarar `0x822479EC` como início de função não muda o comportamento. Depois disso: **0 erros**,
  e `sub_825F3940` passa a chamar `sub_822479EC(ctx, base)`.
- Restam 3 avisos "Unable to decode instruction" em `0x825F398C`–`0x825F399C`. São bytes de dados no
  fim de `.text` (depois do `b`), que o preenchimento de lacunas do gerador transformou em
  `sub_825F3988`. Ninguém chama esse trecho (é dado, não código).
- **ABI e chamadas de sistema:** o runtime do SDK implementa as importações do kernel/XAM
  (`xboxkrnl`/`xam`), a memória do console (base `0x82000000`) e a troca de endianness nas
  leituras/gravações. O projeto não altera essas camadas.

### Problemas de build encontrados e como foram resolvidos (sem tocar no código do SDK)

| Sintoma | Causa (evidência) | Solução |
|---|---|---|
| `lzxd.c:1:1: error: expected identifier` | 14 links simbólicos do Git no `libmspack` (modo 120000) gravados como texto, porque o Windows está sem symlinks (`core.symlinks=false`) | `tools/materialize_symlinks.py` troca cada link pela cópia do destino (16 links; idempotente; não muda commits) |
| `Error copying ... TracyClient.dll ... Permission denied` | a DLL do perfilador Tracy foi ligada (passo 424 do log) e sumiu da pasta antes da cópia; antivírus ativo: Kaspersky (Defender desligado). **Inferido**: não há acesso ao log do Kaspersky | `-DREXGLUE_ENABLE_TRACY=OFF` (opção do próprio SDK). O Tracy só serve para profiling remoto. O executável final não importa `TracyClient.dll` (`llvm-readobj --coff-imports`) |
| `rexglue.exe` não encontrado em `out\sdk` | o SDK grava os binários em `rexglue-sdk\out\win-amd64` | caminho corrigido no script |

## 5. Testes e evidências

### 5.1 Build

`tools\build_port.cmd` → código 0 nas três fases. A fase do jogo levou 167 s na compilação completa
(máquina com RTX 5080). Saídas (`docs/evidencias/m01/build_resumo.txt`):

| Arquivo | Tamanho | sha256 |
|---|---|---|
| `banjotooie.exe` | 31.528.960 | `8f1ec4cf…` |
| `rexruntime.dll` | 12.424.704 | `42f7ddbc…` |

O executável é PE x86-64 nativo e só importa DLLs do Windows, o runtime do VC++ e `rexruntime.dll`.
Nenhum emulador participa.

### 5.2 Teste de fumaça automatizado — `tools\smoke_port.ps1` → **PASS 8/8**

Executado duas vezes, as duas PASS. Resultado da segunda (`docs/evidencias/m01/smoke_result.json`):

| # | Passo | Resultado | Evidência |
|---|---|---|---|
| 0 | sha256 dos 36 arquivos do jogo antes | **PASS** 36/36 | `hashes_antes.json` |
| 1 | preflight na sua pasta (`--preflight_only`) | **PASS**, código 0, 0 ausentes | `preflight_ok.json` |
| 2 | preflight numa pasta de teste sem `RAWFiles\db360.cmp` | **PASS**, código 2, arquivo citado | `preflight_faltando.json` |
| 3 | sem `--game_data_root` | **PASS**, código 3 | `preflight_sem_pasta.json` |
| 4 | **jogo real** até a captura (75 s) | **PASS**: código 0, **2.239 quadros**, **29,61 FPS** no último segundo (1ª execução: 2.235 quadros, 29,45 FPS) | `boot_jogo.json`, `banjotooie.log` |
| 4b | captura PNG da imagem do jogo | **PASS**: 1280×720, brilho médio 30,8, 64,8 % de pixels acesos; mostra a abertura com **"PRESSIONE START PARA JOGAR"** | PNG não versionado (arte do jogo); sha256 em `build_resumo.txt` |
| 5 | falha provocada (`--port_test_crash`) | **PASS**: saída `0xE0425442`, relatório com a falha, minidump de 221.545 bytes | `boot_falha_teste.json` |
| 6 | sha256 dos 36 arquivos do jogo depois | **PASS** 36/36, nenhum arquivo novo | `hashes_depois.json` |

Do log do runtime (`banjotooie.log`: 58 linhas, **0 erros**, 3 avisos):

- GPU: `NVIDIA GeForce RTX 5080`, Direct3D 12; SDL 3.5.0. O **controle Xbox Series X** conectado foi
  detectado pelo SDL (não testado: nenhuma entrada foi enviada).
- Pasta do jogo montada em `\Device\Harddisk0\Partition1`, somente leitura. Saves e cache foram
  para a pasta de teste (`--user_data_root`/`--cache_path`).
- **Primeiro quadro do jogo 0,10 s após iniciar o módulo**. O mesmo jogo no Xenia levou ~62 s até a
  abertura.
- Avisos:
  1. `NtCreateFile ... db360_TexFileList.txt -> 0xc000000f`: esperado, o arquivo é opcional
     (auditoria; o jogo segue para `db360.textures.cmp`).
  2. `ShaderDumpxe:\CompareBackEnds -> [no device]`: caminho de depuração do jogo, inofensivo.
  3. `gamecontrollerdb.txt does not exist`: mapeamentos extras do SDL; os mapeamentos embutidos
     reconheceram o controle.

### 5.3 O que NÃO foi testado (PENDENTE, com a causa)

| Item | Status | Causa |
|---|---|---|
| Passar da tela de abertura (menu, seleção de save, introdução, jogo) | **PENDENTE** | Nenhuma entrada automatizada foi enviada. O teste só observa a abertura. Injetar controle é tarefa de harness (próximas missões) |
| Controle/teclado funcionando no jogo | **PENDENTE** | O controle é detectado, mas não foi acionado |
| Saves | **PENDENTE** | Nada foi gravado: a pasta `user_data` do teste ficou vazia, porque o jogo não saiu da abertura |
| Áudio audível | **PENDENTE** | Testes com `--audio_mute`; o sistema de áudio inicializou (log) |
| Estabilidade em sessões longas | **PENDENTE** | Execuções de ~76 s |
| Rodar com duplo clique, sem argumentos | parcial | Sem `--game_data_root`, o port mostra a mensagem em PT-BR e sai com código 3. Ainda falta um lançador ou arquivo de configuração que guarde a pasta |

## 6. Matriz atualizada (port nativo)

| Requisito | Antes (M00) | Agora (M01) | Evidência |
|---|---|---|---|
| Executável nativo Windows x64 | PENDENTE | **IMPLEMENTADO** (build reproduzível, executa) | 5.1, 5.2 |
| Inicia com os dados fornecidos | PENDENTE | **IMPLEMENTADO** até a tela de abertura | 5.2 passo 4 |
| Diagnóstico de inicialização e arquivos ausentes | — | **IMPLEMENTADO** (preflight, relatório, minidump; códigos 2/3/4/5) | 5.2 passos 1, 2, 3, 5 |
| Preservação dos arquivos originais | forte no código | **IMPLEMENTADO** (36/36 antes e depois) | 5.2 passos 0 e 6 |
| PT-BR | PARCIAL | **PARCIAL**: abertura em PT-BR no port nativo; cobertura total não verificada | captura |
| Menu START / jogabilidade | PENDENTE | **PENDENTE** (sem entrada automatizada) | 5.3 |
| 4K | PENDENTE | **PENDENTE**: o port roda em 1280×720 (padrão); opções 4K existem no runtime, sem teste | 5.2 |
| 60 FPS | PENDENTE | **PENDENTE**. Agora **medido**: o original roda a **~29,5 FPS** no port (confirma os 30 FPS inferidos do código) | `boot_jogo.json` |
| Draw distance, QoL | PENDENTE | **PENDENTE** | — |
| Gamepad | PENDENTE | **PARCIAL**: controle detectado pelo SDL; entrada não testada | log |
| Saves | PENDENTE | **PENDENTE** | 5.3 |

## 7. Como usar

```
tools\build_port.cmd                       (compila tudo; ~5 min depois da 1a vez)
banjotooie-recomp\out\build\win-amd64-release\banjotooie.exe --game_data_root "W:\Decomp\Banjo 3\Banjo Tooie BR"
pwsh -File tools\smoke_port.ps1            (teste automatizado; ~3 min)
```

Saves e configurações vão para `Documentos\banjotooie` (padrão do runtime) ou para `--user_data_root`.
Atalhos do runtime: F3 = FPS, F4 = ajustes, crase (`` ` ``) = console.

## 8. Riscos e pendências

1. **Entrada automatizada** é o próximo bloqueio para testar menu, jogo, controle e saves.
2. **Antivírus (Kaspersky)**: removeu (inferido) o `TracyClient.dll`. O build atual não usa Tracy,
   mas `banjotooie.exe`/`rexruntime.dll` também podem ser analisados; até agora não sumiram.
3. **Licença da base**: sem licença declarada (uso pessoal ok; distribuir exige permissão ou
   reescrever os arquivos copiados).
4. **Fork defasado** (168 commits atrás do ReXGlue oficial): mantido de propósito.
5. **Ponto `0x822479EC`**: correção deste projeto, não da base; pelo código é seguro, mas o caminho
   só será exercitado quando o jogo passar por ali.
6. **Memória Understory**: BLOCKED_MCP (ferramentas indisponíveis nesta sessão) e BLOCKED_QUEUE (a
   fila fica fora de `app/`).
