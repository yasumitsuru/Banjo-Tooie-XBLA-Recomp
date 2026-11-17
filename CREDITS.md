# Créditos e proveniência dos arquivos originais do jogo

O jogo **Banjo-Tooie** é propriedade de Microsoft/Rare. Este projeto não redistribui nem reproduz os arquivos do jogo fora do computador da pessoa dona da cópia.

## Arquivos originais usados como fonte

Missão 00 (auditoria): os arquivos foram apenas **lidos**. Missão 01 (port nativo): continuam
**sem cópia** para o repositório. O port os lê direto da pasta de origem (`--game_data_root`,
montada como somente leitura), e o `rexglue codegen` lê o `default.xex` para gerar o C++
recompilado em `banjotooie-recomp/generated/`, que **não é versionado** (código derivado do jogo,
ignorado pelo Git, regenerável com `tools\build_port.cmd codegen`). Para o teste de arquivo
ausente, `tools\smoke_port.ps1` copia só o `default.xex` para uma pasta de teste em
`_audit/out/m01/smoke/` (também ignorada pelo Git).

Origem: `W:\Decomp\Banjo 3\Banjo Tooie BR\` (36 arquivos fornecidos legalmente pela pessoa dona da cópia)

| Arquivo | Uso no projeto |
|---|---|
| `default.xex` | Executável XBLA original — fonte do código recompilado |
| `RAWFiles\db360.cmp` | Dados do jogo |
| `RAWFiles\db360.textures.cmp` | Texturas do jogo |
| `RAWFiles\banjo2_360.tbl` / `banjo2_360.ctl` | Tabelas e índices do jogo |
| `RAWFiles\X360_strings.dat` | Strings de sistema do XBLA (inclui PT-BR) |
| `RAWFiles\xarialuni.ttf` | Fonte Unicode do jogo |
| `RAWFiles\ArcadeNormal.ptc` / `ArcadeWide.ptc` | Layouts de tela XBLA |
| `RAWFiles\58410955033F0001` | Contêiner PIRS (tema NXE) |
| `ArcadeInfo.xml` + 24 PNGs | Metadados/ícones XBLA (não são necessários para o jogo em si) |

## Versão global em inglês (2026-11-13)

Origem: `W:\Decomp\Banjo 3\Banjo-Tooie (World) (XBLA)\` (36 arquivos fornecidos pela pessoa dona da
cópia). O mesmo port nativo abre esta pasta com `--game_data_root` (ou pelo seletor
`Seletor\BanjoTooie_Seletor.exe`), **sem cópia** para o projeto e somente leitura (36/36 hashes
iguais antes e depois do teste de fumaça). O `default.xex` dela é o arquivo de varejo cifrado
(sha256 `4b512ae9…`, o mesmo que o projeto-base usa); depois de decifrado é a mesma imagem do XEX
PT-BR, por isso o código recompilado serve às duas. Diferem só os textos e texturas traduzidos
(`X360_strings.dat`, `db360.cmp`, `db360.textures.cmp`); os outros arquivos são idênticos.

## Ferramentas e código de terceiros usados no port (Missão 01)

| Item | Onde está | Licença | O que foi usado |
|---|---|---|---|
| Port base [iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp) @ `72c2397` | `banjotooie-recomp/` (copiado) | **sem licença declarada**. A pessoa decidiu **uso só pessoal** (`docs/DECISOES.md` nº 4); para distribuir, seria preciso pedir permissão ao autor | `banjotooie_manifest.toml` (caminhos trocados para a pasta da pessoa), `overrides.toml` (+1 ponto deste projeto: `0x822479EC`), `CMakeLists.txt`/`CMakePresets.json` (+ fontes e `dbghelp`), `src/main.cpp` e `src/banjotooie_app.h` (reescritos com os ganchos de diagnóstico), `banjotooie.rc`/`banjotooie.ico` (ícone do autor da base) |
| Runtime/SDK [ReXGlue](https://github.com/rexglue/rexglue-sdk), fork `iron-jay/rexglue-sdk` @ `035aa25` (derivado do [Xenia](https://xenia.jp)) | submódulo `rexglue-sdk/` (não copiado para o repositório; fixado no commit) | BSD-3-Clause (Tom Clay + Xenia) | runtime (gráfico D3D12, kernel, áudio SDL, entrada) e o gerador `rexglue codegen`, sem alterações de código. Na cópia de trabalho, 16 links simbólicos do Git foram trocados por cópias do arquivo de destino (`tools/materialize_symlinks.py`) |
| Dependências do SDK (SDL3, imgui, fmt, spdlog, tomlplusplus, FFmpeg/fork, glslang, SPIRV, libmspack, xxHash, snappy, Tracy etc.) | `rexglue-sdk/thirdparty/` (submódulos do SDK, nos commits fixados por ele) | licenças próprias de cada projeto | compiladas junto com o runtime |
| Código deste projeto | `banjotooie-recomp/src/banjotooie_app.cpp`, `src/port_diagnostics.{h,cpp}`, `tools/` | do projeto | diagnóstico, preflight, relatório de boot, minidump, contador de quadros, PNG, scripts de build e teste |

Referências consultadas, sem código copiado: [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) e os forks dele.

## Launcher portátil (2026-11-16)

| Item | Onde está | Licença | O que foi usado |
|---|---|---|---|
| [MC_LA_Recompiled](https://github.com/robertoaraujo77/MC_LA_Recompiled) (Roberto Araujo), instalador/configurador `installer/source` | adaptado em `launcher/` (via a primeira adaptação do projeto irmão `W:\Decomp\Banjo\app\recomp\launcher\windows`) | **GPL-3.0-or-later** | layout e estilos do XAML, editor de opções "chave = valor", perfis, exportação de backup, tarefa com progresso, script de build com `csc.exe`. Por isso `launcher/` é **GPL-3.0-or-later** (`launcher/LICENSE-GPL-3.0.txt`, cópia do texto do repositório de referência); a pasta portátil leva a licença em `LICENCAS\` e o crédito em `CREDITOS.txt` |
| Arte `launcher/assets/banjotooie.png` | projeto irmão `W:\Decomp\Banjo` | do projeto | favo de mel original (sem arte do jogo); `banjotooie.ico` é o ícone do port |
| Runtime do Microsoft Visual C++ 2015-2022 x64 | copiado de `VC\Redist\MSVC\14.44.35112` para `bin\` da pasta portátil | redistribuível da Microsoft | implantação local para o jogo abrir sem instalar o pacote VC++ |
| Licenças do ReXGlue e de `rexglue-sdk/thirdparty/*` | copiadas para `LICENCAS\` da pasta portátil | de cada projeto | cumprimento das licenças do runtime distribuído junto |

A pasta portátil e o ZIP **não** contêm arquivos do jogo (o build recusa empacotá-los), mas
`bin\banjotooie.exe` contém código recompilado do jogo e a base do port não declara licença:
uso pessoal, como em `docs/DECISOES.md` nº 4.

## Usados só como referência/verificação na auditoria (nada copiado para o produto)

| Item | Origem | Uso |
|---|---|---|
| `overrides.toml` / `banjotooie_manifest.toml` / `launcher.py` da base | clone de referência `%TEMP%\opencode\bt-xbla-base` (commit `72c2397`) | comparação endereço a endereço com o XEX do usuário (`docs/evidencias/overrides_vs_xex.json`) |
| Patches da comunidade do Xenia "58410955 - Banjo-Tooie" (autores: illusion, retroben, wedarobi) | `W:\Emulador\Xbox360\Emulators\Xenia Canary\patches\58410955 - Banjo-Tooie.patch.toml` (instalação do usuário, só lida) | referência de endereços para 60 FPS / draw distance / LOD (`docs/evidencias/xenia_patches_vs_xex.json`); **não** aplicados |
| Xenia Canary (`canary_experimental@7ef873b0d`) | cópia do exe do usuário em `app/_audit/out/xenia_smoke/` (ignorada pelo Git) | smoke test do jogo original, em modo portátil isolado |
| Código-fonte do fork `iron-jay/rexglue-sdk@035aa25` (BSD-3, Tom Clay + Xenia) | cópia de leitura em `app/_audit/out/sdk_fork_035aa25/` (ignorada pelo Git), baixada pela API do GitHub | rev. 3: leitura da ponte gráfica (`docs/evidencias/ponte_grafica_sdk_035aa25.json`); a lista de opções em `docs/evidencias/sdk_cvars_035aa25.txt` cita linhas desse código |
| README/metadados de XenonRecomp (hedge-dev, sonicnext-dev), XenonRecompUnlimited, MC_LA_Recompiled e ReXGlue oficial | API do GitHub, em `app/_audit/out/fontes_readme/` (ignorada pelo Git) | rev. 3: avaliação de compatibilidade e licenças (`docs/evidencias/fontes_consulta.json`) |
| Tabela de conquistas (XDBF) do `default.xex` da pessoa: 12 ids, pontos e textos em inglês | `W:\Decomp\Banjo 3\Banjo Tooie BR\default.xex`, lida em tempo de execução (`kernel_state()->title_xdbf()`); nada é copiado para o app | M04 C-6: lista CONQUISTAS do PC; na versão PT-BR os nomes e descrições exibidos são uma tradução curta escrita para o port (`src/pc/pc_achievements.cpp`) com os termos da tradução da pessoa |
