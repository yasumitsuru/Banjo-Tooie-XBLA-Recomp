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
pessoal (`docs/DECISOES.md`). O teste automatizado do projeto vai até a abertura. **Ainda não
feitos:** 4K (será pelo sistema gráfico do ReXGlue, por decisão da pessoa) e 60 FPS.

## Como compilar e rodar

Para o checker do Craft Anything, `npm run build` executa o build C++/CMake real
sem dependências npm; veja [a integração do build nativo](docs/BUILD-NATIVE-CRAFT.md).

```
git submodule update --init --recursive        (runtime ReXGlue fixado em 035aa25)
tools\build_port.cmd                           (VS 2022 Build Tools + Clang 19; ~5 min)
banjotooie-recomp\out\build\win-amd64-release\banjotooie.exe --game_data_root "<pasta com default.xex e RAWFiles>"
pwsh -File tools\smoke_port.ps1                (teste automatizado: preflight, jogo, captura, falha, hashes)
```

Opções do port: `--preflight_only`, `--boot_report <json>`, `--screenshot <png>`,
`--screenshot_frames <n>`, `--screenshot_seconds <s>`, `--smoke_seconds <s>`,
`--background_window`, `--port_silent` (ver `banjotooie-recomp/src/banjotooie_app.cpp`).

## Documentos

- **Missão 01 (port nativo):** `docs/MISSAO-01-PORT-NATIVO.md`: arquitetura, toolchain, mudanças,
  testes com evidências (`docs/evidencias/m01/`), matriz atualizada e pendências
- **Respostas e decisões da pessoa:** `docs/DECISOES.md` (uso pessoal, versão completa, 720p/30,
  4K pelo sistema gráfico do ReXGlue)
- **Missão 00 (auditoria):** `docs/AUDITORIA-MISSAO-00.md`, com evidências em `docs/evidencias/`
- **Valores reais do executável original:** `MECHANICS.md` (endereços do XEX, base `0x82000000`)
- **Créditos e licenças:** `CREDITS.md`

## Estrutura

- `banjotooie-recomp/`: projeto do jogo (da base iron-jay @ `72c2397`, com os ganchos de
  diagnóstico deste projeto); `generated/` e `out/` não são versionados
- `rexglue-sdk/`: submódulo do runtime/gerador (fork `iron-jay/rexglue-sdk` @ `035aa25`)
- `tools/`: build, teste de fumaça e correção de links simbólicos
- `_audit/`: ferramentas da auditoria (só leitura); `_audit/out/` guarda saídas e capturas não versionadas
- Arquivos originais do jogo: fora desta pasta, em `..\Banjo Tooie BR` (somente leitura)

**Próximo passo (só quando for pedido):** a missão seguinte.
