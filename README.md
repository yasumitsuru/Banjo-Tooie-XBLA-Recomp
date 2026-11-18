This build contains the original game's own files, for the private use of the person who owns the game, on their own computer. It must not be shared or published.

# Banjo-Tooie XBLA PT-BR Recompiled

Port nativo para Windows (x86-64) do **Banjo-Tooie de Xbox Live Arcade**, feito por recompilação
estática do executável do Xbox 360 com o [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).
O jogo roda como um programa do Windows, **sem emulador**, com a tradução em português do Brasil
(ou a versão global em inglês) e melhorias de PC: 4K, 60 FPS e um menu de configurações dentro
do jogo.

O repositório **não contém nenhum arquivo do jogo**: o código do jogo é gerado na sua máquina a
partir da **sua própria cópia** e os dados são lidos dela, sem alterações.

## O que o projeto oferece

- **Vídeo:** resolução de saída 1080p, 1440p ou 4K (automática pelo monitor) e resolução interna
  separada (720p original, 1440p ou 4K real); janela, janela sem bordas ou tela cheia; escolha
  do monitor; VSync; FXAA, MSAA 2× e filtragem anisotrópica; distância de desenho Original,
  Longe ou Total.
- **60 FPS** na lógica do jogo (ou os 30 FPS originais), com ritmo de quadros constante.
- **Configurações de PC dentro do jogo** (tecla F1 ou START → AJUDA E OPÇÕES → CONFIGURAÇÕES DE PC),
  nos menus do próprio jogo.
- **Controles:** teclado e mouse (teclas remapeáveis, câmera pelo mouse com sensibilidade,
  inversão e suavização) e controle de Xbox/XInput (zona morta, sensibilidade, vibração).
- **Qualidade de vida:** velocidade dos diálogos (Original, Rápida, Muito rápida ou Instantânea), início
  rápido, pausa ao perder o foco, contador de FPS, estatísticas de desempenho, rastreador de
  colecionáveis por mundo e lista de conquistas em português.
- **Saves** no formato do jogo com backups automáticos e restauração pelo menu; registro de cada
  sessão e relatório de falhas.
- **Launcher portátil** (português ou inglês): confere a pasta do jogo, prepara uma cópia
  portátil e guarda saves, opções, registros e cache dentro da própria pasta.

## O que é preciso para jogar

- Windows 10 ou 11 de 64 bits e placa de vídeo com **DirectX 12**.
- A sua cópia do **Banjo-Tooie XBLA já descompactada**: a pasta com `default.xex` e `RAWFiles\`
  (`db360.cmp`, `db360.textures.cmp`, `X360_strings.dat`, `banjo2_360.tbl`, `banjo2_360.ctl`,
  `xarialuni.ttf` e outros). A versão traduzida em PT-BR é a principal; a versão global em inglês
  também roda.
- O pacote do launcher já traz o runtime do Visual C++; o launcher usa o .NET Framework 4, que
  vem com o Windows.

## Como usar (launcher)

1. Extraia o ZIP `Banjo-Tooie-PT-BR-Recompiled-1.0.0-win64.zip` numa pasta gravável.
2. Abra `Banjo-Tooie PT-BR Recompiled.exe` e, em **Jogar**, escolha a pasta do jogo descompactado
   (**Procurar pasta…**; **Conferir** mostra se a pasta serve e qual versão é).
3. Clique em **PREPARAR MEU JOGO** (os arquivos originais nunca são alterados) e depois em **JOGAR**.

As páginas Gráficos, Controles, Áudio e jogo editam as mesmas opções do menu F1. Saves e backups
ficam em `dados\` da pasta do launcher; a página Diagnóstico gera um relatório para investigar
problemas. Instruções completas em `LEIA-ME.txt` / `README-EN.txt` dentro do ZIP.

Sem o launcher, o jogo também abre direto:

```
out\native\banjotooie.exe --game_data_root "<pasta com default.xex e RAWFiles>"
```

Nesse caso os saves e as opções de PC ficam em `Documentos\My Games\Banjo-Tooie XBLA` da conta do
Windows.

### Teclas padrão

| Ação | Tecla |
|---|---|
| Mover | W A S D |
| A (pular) / B (atacar, voltar) | Espaço / Backspace |
| X / Y | R / E |
| Start (pausa) | Enter |
| Câmera | I J K L (ou o mouse, com "Girar a câmera com o mouse" ligado) |
| Configurações de PC | F1 |

## O que é preciso para compilar

- Windows 10/11 x64 e Git (com submódulos).
- Visual Studio 2022 Build Tools com MSVC, Windows SDK e as ferramentas Clang (Clang 19).
- CMake e Ninja (os scripts usam `C:\Program Files\CMake` e `C:\msys64\clang64\bin\ninja.exe`).
- Python 3.
- A pasta do jogo (`default.xex` + `RAWFiles`) **ao lado da pasta `app`**, com o nome
  `Banjo Tooie BR` (ou ajuste `game_root` em `banjotooie-recomp/banjotooie_manifest.toml`).

```
git clone --recursive https://github.com/yasumitsuru/Banjo-Tooie-XBLA-Recomp.git app
cd app
tools\build_port.cmd sdk        (compila o gerador rexglue)
tools\build_port.cmd codegen    (gera o C++ do jogo a partir do seu default.xex)
tools\build_native.cmd          (compila o jogo e monta o launcher portátil; o mesmo que npm run build)
```

Saídas:

| Arquivo | O que é |
|---|---|
| `out\native\banjotooie.exe` + `rexruntime.dll` | o jogo |
| `out\launcher\Banjo-Tooie PT-BR Recompiled.exe` | o launcher (C# 5/WPF, compilado com o `csc.exe` do Windows) |
| `out\portable\Banjo-Tooie-PT-BR-Recompiled\` e o `.zip` | pasta portátil pronta (launcher, jogo, runtime VC++, licenças), sem arquivos do jogo |

O código gerado (`banjotooie-recomp/generated/`) e as saídas (`out/`) não são versionados.

### Opções de linha de comando do jogo

| Opção | Efeito |
|---|---|
| `--game_data_root <pasta>` | pasta do jogo (`default.xex` + `RAWFiles`) |
| `--user_data_root <pasta>` | pasta dos saves, opções de PC, registros e falhas |
| `--cache_path <pasta>` | cache de shaders |
| `--preflight_only` | só confere os arquivos do jogo e sai (código 0 = tudo certo) |
| `--boot_report <arquivo.json>` | relatório da inicialização |
| `--prepare-shaders <arquivo.json>` | recria os shaders guardados no cache e sai |
| `--screenshot <arquivo.png>`, `--smoke_seconds <s>` | captura de tela / roda por alguns segundos e sai |
| `--port_silent` | sem caixas de mensagem |

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `banjotooie-recomp/` | projeto do jogo: manifesto do gerador, ganchos (`pc_hooks.toml`) e a camada de PC (`src/pc/`) |
| `rexglue-sdk/` | submódulo do runtime e do gerador (ReXGlue com as correções usadas por este port) |
| `launcher/` | launcher portátil (GPL-3.0-or-later) |
| `tools/` | scripts de build, empacotamento e testes automatizados |
| `_audit/` | ferramentas de análise do executável original |
| `MECHANICS.md`, `mechanics.json` | endereços e valores do executável original usados pelos ganchos |

## Créditos e licenças

Banjo-Tooie © Microsoft / Rare. Recompilação base:
[iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp) (sem
licença declarada — por isso o projeto é de **uso pessoal**). Runtime: ReXGlue SDK (BSD-3-Clause,
derivado do Xenia). Launcher adaptado do
[MC_LA_Recompiled](https://github.com/robertoaraujo77/MC_LA_Recompiled) (GPL-3.0-or-later).
Detalhes e a lista dos arquivos do jogo usados: [`CREDITS.md`](CREDITS.md).
