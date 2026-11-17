# Créditos e proveniência dos arquivos originais do jogo

O jogo **Banjo-Tooie** é propriedade de Microsoft/Rare. Este projeto não redistribui nem reproduz os
arquivos do jogo fora do computador da pessoa dona da cópia.

## Arquivos originais do jogo

Os arquivos do jogo são **só lidos** e **nunca copiados** para o repositório. O port os lê direto
da pasta indicada (`--game_data_root`, aberta somente para leitura), e o `rexglue codegen` lê o
`default.xex` para gerar o C++ recompilado em `banjotooie-recomp/generated/`, que **não é
versionado** (código derivado do jogo, regenerável com `tools\build_port.cmd codegen`).

Arquivos da cópia do jogo (36 arquivos, fornecidos pela pessoa dona da cópia):

| Arquivo | Uso no projeto |
|---|---|
| `default.xex` | Executável XBLA original — fonte do código recompilado |
| `RAWFiles\db360.cmp` | Dados do jogo |
| `RAWFiles\db360.textures.cmp` | Texturas do jogo |
| `RAWFiles\banjo2_360.tbl` / `banjo2_360.ctl` | Tabelas e índices do jogo |
| `RAWFiles\X360_strings.dat` | Textos de sistema do XBLA (inclui PT-BR) |
| `RAWFiles\xarialuni.ttf` | Fonte Unicode do jogo |
| `RAWFiles\ArcadeNormal.ptc` / `ArcadeWide.ptc` | Logo do Xbox Live Arcade da abertura |
| `RAWFiles\58410955033F0001` | Contêiner PIRS (tema NXE) |
| `ArcadeInfo.xml` + 24 PNGs | Metadados/ícones XBLA (não são necessários para o jogo em si) |

**Versões aceitas.** A versão traduzida em PT-BR (`default.xex` decifrado) e a versão global em
inglês (`default.xex` de varejo cifrado, sha256 `4b512ae9…`): depois de decifrado é a mesma
imagem, por isso o mesmo código recompilado serve às duas. Diferem só os textos e texturas
traduzidos (`X360_strings.dat`, `db360.cmp`, `db360.textures.cmp`).

## Código e ferramentas de terceiros

| Item | Onde está | Licença | O que foi usado |
|---|---|---|---|
| Port base [iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp) @ `72c2397` | `banjotooie-recomp/` | **sem licença declarada**: uso só pessoal; para distribuir, é preciso pedir permissão ao autor | `banjotooie_manifest.toml`, `overrides.toml` (+1 ponto deste projeto: `0x822479EC`), `CMakeLists.txt`/`CMakePresets.json`, `src/main.cpp` e `src/banjotooie_app.h` (reescritos), `banjotooie.rc`/`banjotooie.ico` (ícone do autor da base) |
| Runtime/SDK [ReXGlue](https://github.com/rexglue/rexglue-sdk), fork `iron-jay/rexglue-sdk` @ `035aa25` (derivado do [Xenia](https://xenia.jp)) | submódulo `rexglue-sdk/` (branch `ptbr-port-banjo3`, com as correções usadas por este port) | BSD-3-Clause (Tom Clay + Xenia) | runtime (gráfico D3D12, kernel, áudio SDL, entrada) e o gerador `rexglue codegen`. Os links simbólicos do Git do SDK são trocados por cópias do arquivo de destino (`tools/materialize_symlinks.py`) |
| Dependências do SDK (SDL3, imgui, fmt, spdlog, tomlplusplus, FFmpeg, glslang, SPIRV, libmspack, xxHash, snappy etc.) | `rexglue-sdk/thirdparty/` | licenças próprias de cada projeto | compiladas junto com o runtime |
| [MC_LA_Recompiled](https://github.com/robertoaraujo77/MC_LA_Recompiled) (Roberto Araujo), instalador/configurador | adaptado em `launcher/` | **GPL-3.0-or-later** | layout e estilos do XAML, editor de opções, perfis, exportação de backup, script de build com `csc.exe`. Por isso `launcher/` é **GPL-3.0-or-later** (`launcher/LICENSE-GPL-3.0.txt`); a pasta portátil leva a licença em `LICENCAS\` e o crédito em `CREDITOS.txt` |
| Arte `launcher/assets/banjotooie.png` | `launcher/assets/` | do projeto | favo de mel original (sem arte do jogo) |
| Runtime do Microsoft Visual C++ 2015-2022 x64 | `bin\` da pasta portátil | redistribuível da Microsoft | para o jogo abrir sem instalar o pacote do Visual C++ |
| Código deste projeto | `banjotooie-recomp/src/`, `launcher/`, `tools/` | do projeto | camada de PC, diagnóstico, launcher, scripts de build e testes |

A pasta portátil e o ZIP **não** contêm arquivos do jogo (o build recusa empacotá-los), mas
`bin\banjotooie.exe` contém código recompilado do jogo e a base do port não declara licença: uso
pessoal.

## Referências (nada copiado)

| Item | Uso |
|---|---|
| Patches da comunidade do Xenia "58410955 - Banjo-Tooie" (illusion, retroben, wedarobi) | referência de endereços para 60 FPS, distância de desenho e LOD; os efeitos foram reimplementados como ganchos |
| [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | referência de recompilação estática |
| Xenia Canary | comparação com o jogo original rodando no emulador |
| Tabela de conquistas (XDBF) do `default.xex` | lida em tempo de execução para a lista CONQUISTAS; na versão PT-BR os nomes exibidos são uma tradução curta escrita para o port (`src/pc/pc_achievements.cpp`) |
