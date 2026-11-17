This build contains the original game's own files, for the private use of the person who owns the game, on their own computer. It must not be shared or published.

# Banjo-Tooie XBLA PT-BR Recompiled — estado do projeto

Projeto de port nativo para Windows 11 (x86-64) do Banjo-Tooie (XBLA), por recompilação estática
(ReXGlue), visando 4K/60 FPS e PT-BR, preservando o jogo original.

**Status atual: MISSÃO 01 (port nativo) concluída.** O `banjotooie.exe` nativo é compilado de forma
reproduzível a partir do **seu** `default.xex` (22.111 funções recompiladas), inicia com os seus
dados e mostra a tela de abertura em PT-BR ("PRESSIONE START PARA JOGAR") a ~29,5 FPS, sem
emulador. Os arquivos do jogo não são copiados para o repositório nem alterados (36/36 hashes
iguais antes e depois). Segundo a pessoa, o jogo **funciona normalmente**: o menu do próprio jogo
(o "menu START"), a seleção de save, a introdução, o jogo, o controle/teclado e gravar/carregar.
Ela também confirma a versão completa, a tradução aplicada, o original em 720p/30 e o uso só
pessoal (`docs/DECISOES.md`). **Missão 02 (tradução):** a tradução PT-BR foi conferida sem
alterá-la: estrutura, acentos, fontes e leitura pelo jogo, mais 8 telas reais (abertura, menus,
seleção de jogo, introdução) com regressão automática (`docs/MISSAO-02-TRADUCAO.md`); nenhuma
correção foi necessária. **Camada de PC (branch `port-pc-4k60`):** 4K e 60 FPS entregues e
medidos no jogo real (3840×2160, 60,0 quadros/s), menu de configurações de PC na pausa, controle,
saves com backup e auto-teste `--self-test`. **Estado atual, provas e próximos passos:
[`docs/PLANO.md`](docs/PLANO.md)** (auditoria e plano da Missão 04 de 2026-11-13; texto da missão em
[`docs/MISSAO-04-CORRECAO-INTEGRAL.md`](docs/MISSAO-04-CORRECAO-INTEGRAL.md)).

## Launcher portátil (2026-11-16)

`out\portable\Banjo-Tooie-PT-BR-Recompiled-1.0.0-win64.zip` (gerado por `npm run build` ou
`tools\build_launcher.cmd`): extraia, abra `Banjo-Tooie PT-BR Recompiled.exe`, escolha a pasta do
seu Banjo-Tooie XBLA descompactado (`default.xex` + `RAWFiles`), **PREPARAR MEU JOGO** e **JOGAR**.
O launcher (português ou inglês, no seletor IDIOMA · LANGUAGE; adaptado do MC_LA_Recompiled, GPL-3.0) confere a pasta com a pré-checagem do
jogo e o SHA-256, copia o jogo para `jogo\` e guarda saves, opções, registros e cache dentro da
própria pasta (nada no Registro nem em Documentos). Gráficos, controles (inclusive teclado), áudio,
qualidade de vida, saves e diagnóstico editam os mesmos arquivos que o menu F1 do jogo. O ZIP não
tem arquivos do jogo, mas o programa do port contém código recompilado: uso pessoal. Relatório,
testes (56/56, português e inglês) e limitações: [`docs/LAUNCHER-RELATORIO.md`](docs/LAUNCHER-RELATORIO.md).

## Escolher a versão (PT-BR ou global em inglês)

`Seletor\BanjoTooie_Seletor.exe` abre uma janela com dois botões: **Português (PT-BR)** (pasta
`Banjo Tooie BR`) e **Global (inglês)** (pasta `Banjo-Tooie (World) (XBLA)`). O jogo é o mesmo
port; só muda a pasta de dados. As duas versões usam os mesmos saves e configurações, para
comparar o mesmo ponto do jogo nas duas línguas. A camada de PC (menu de configurações dentro do
jogo e entradas novas dos menus) segue o idioma do jogo.
Para testes: `BanjoTooie_Seletor.exe --auto ptbr|global [opções do jogo]`.

## Onde fica o save

Em **`Documentos\My Games\Banjo-Tooie XBLA`** da conta do Windows que está jogando: cada conta
tem o seu próprio save, as suas opções de PC (`pc_settings.toml`) e os seus backups (`backups\`).
Na primeira vez em que o jogo abre, ele copia o save das versões anteriores (pasta
`out\craft-runtime\user` do app) para lá, sem apagar o original, que ganha o aviso
`MIGRADO-PARA-MY-GAMES.txt` e não é copiado para mais nenhuma conta. As verificações do Craft usam
outra pasta (`out\craft-check\user`) e nunca mexem no save de ninguém.

## Novidades da Missão 04 (2026-11-14)

- **Vídeo**: a imagem 4K agora sai em 4K de verdade mesmo com a escala do Windows (antes, num
  monitor 4K a 150%, saía 2560×1440 esticada); 1080p desenha a 2560×1440 e reduz (mais nítido);
  "sem bordas" cobre o monitor inteiro; a linha **MONITOR** (GRÁFICOS) escolhe em qual monitor o
  jogo abre (AUTOMÁTICO = o monitor onde está o mouse ao abrir). Quem já tinha opções salvas
  continua no monitor principal.
- **CONQUISTAS** (menu principal e pausa) mostra a lista das 12 conquistas do jogo em português,
  com as obtidas neste PC marcadas com `*`; A mostra como conseguir cada uma.
- **Registro de sessão**: cada vez que o jogo abre, um arquivo `logs\sessao-*.txt` na pasta do
  save (versão, placa de vídeo, mapas, desempenho, erros). ÁUDIO E SAVES → ABRIR PASTA DOS
  REGISTROS abre a pasta. Útil para relatar problemas.
- Testes da missão: `tools\m04\` (bateria única: `python -I tools\m04\bateria.py [--longos]`).

## Como compilar e rodar

Para o checker do Craft Anything, `npm run build` executa o build C++/CMake real
sem dependências npm; veja [a integração do build nativo](docs/BUILD-NATIVE-CRAFT.md).

```
git submodule update --init --recursive        (runtime ReXGlue fixado em 035aa25)
tools\build_port.cmd                           (VS 2022 Build Tools + Clang 19; ~5 min)
banjotooie-recomp\out\build\win-amd64-release\banjotooie.exe --game_data_root "<pasta com default.xex e RAWFiles>"
pwsh -File tools\smoke_port.ps1                (teste automatizado: preflight, jogo, captura, falha, hashes)
pwsh -File tools\test_traducao.ps1             (Missão 02: integridade da tradução + telas PT-BR no jogo; ~5 min)
```

Opções do port: `--preflight_only`, `--boot_report <json>`, `--screenshot <png>`,
`--screenshot_frames <n>`, `--screenshot_seconds <s>`, `--smoke_seconds <s>`,
`--background_window`, `--port_silent` (ver `banjotooie-recomp/src/banjotooie_app.cpp`).

## Documentos

- **Plano do projeto (vivo):** `docs/PLANO.md`: o que está pronto com prova, matriz de requisitos
  e a ordem dos próximos passos
- **Missão 02 (tradução PT-BR):** `docs/MISSAO-02-TRADUCAO.md`: arquivos traduzidos, como o jogo os
  lê, verificações, telas no jogo, teste negativo, matriz e pendências (`docs/evidencias/m02/`)
- **Missão 01 (port nativo):** `docs/MISSAO-01-PORT-NATIVO.md`: arquitetura, toolchain, mudanças,
  testes com evidências (`docs/evidencias/m01/`), matriz atualizada e pendências
- **Respostas e decisões da pessoa:** `docs/DECISOES.md` (uso pessoal, versão completa, 720p/30,
  4K pelo sistema gráfico do ReXGlue)
- **Missão 00 (auditoria):** `docs/AUDITORIA-MISSAO-00.md`, com evidências em `docs/evidencias/`
- **Valores reais do executável original:** `MECHANICS.md` (endereços do XEX, base `0x82000000`)
- **Launcher portátil:** `docs/LAUNCHER-RELATORIO.md` (código em `launcher/`, testes em
  `tools/launcher/`)
- **Créditos e licenças:** `CREDITS.md`

## Estrutura

- `banjotooie-recomp/`: projeto do jogo (da base iron-jay @ `72c2397`, com os ganchos de
  diagnóstico deste projeto); `generated/` e `out/` não são versionados
- `rexglue-sdk/`: submódulo do runtime/gerador (fork `iron-jay/rexglue-sdk` @ `035aa25`)
- `tools/`: build, teste de fumaça e correção de links simbólicos
- `_audit/`: ferramentas da auditoria (só leitura); `_audit/out/` guarda saídas e capturas não versionadas
- Arquivos originais do jogo: fora desta pasta, em `..\Banjo Tooie BR` (somente leitura)

**Próximo passo (só quando for pedido):** a missão seguinte.
