# Smoke test do jogo ORIGINAL (Xenia Canary isolado) — 2026-11-09

**Objetivo:** provar com execução real que o `default.xex` + dados fornecidos iniciam, e ler o
hash de módulo que o Xenia calcula. Não é teste do port (o port ainda não existe localmente).

## Como foi isolado (nada fora de `app/` foi alterado)

- Cópia do `xenia_canary.exe` do usuário (sha256 `8362A218…FB57DD`, build
  `canary_experimental@7ef873b0d`, 03/08/2026) em `app/_audit/out/xenia_smoke/` com
  `portable.txt` → cache, config, logs e saves ficam só nessa pasta (ignorada pelo Git).
- **Sem** a pasta `patches/` (jogo puro, sem patches da comunidade); `license_mask = 0` (padrão).
- Pasta do jogo montada pelo Xenia como `\Device\Harddisk0\Partition1` (somente leitura).
- Execução e captura pela ferramenta `native_window_capture` do Craft Anything (abre, espera,
  fotografa a janela e fecha o programa).

## Execuções

| # | Parâmetros | Espera | Resultado | Evidência |
|---|---|---|---|---|
| 1 | `--apu=nop --fullscreen=false default.xex` | 51 s | **Tela preta.** Log: `AudioSystem::RegisterClient: CreateDriver failed` e o jogo parado lendo `\RAWFiles`. Causa provável: o áudio desligado por mim (`apu=nop`); a base do port também precisou de um "audio-device fallback". | Craft Anything `ev_e66dc0d13ca3…`; log `xenia_run1_apu-nop.log` |
| 2 | `--apu=any --mute=true --fullscreen=false default.xex` | 62 s | **PASS — tela de abertura do jogo**, com Banjo andando e o texto **"PRESSIONE START PARA JOGAR"** (PT-BR). Título da janela: `[58410955 v0.0.1.2] Banjo-Tooie <Direct3D 12 - RTV/DSV - XAudio2>` | Craft Anything `ev_cb3bce86ad96…`; captura `smoke2_60s.png` (sha256 `5F40FAAB…8159CC`, não versionada: arte do jogo) |

## Fatos lidos do log do Xenia (execução 2)

- `Module Hash: 324176DDF940D212` (o arquivo de patches da comunidade espera `CFE7D4B66FAC7096`
  → o Xenia **não** aplicaria esses patches a este arquivo, mesmo com o código idêntico;
  ver `xenia_patches_vs_xex.json`).
- `XEX_HEADER_FILE_FORMAT_INFO: Encryption Type: None / Compression Type: None`.
- `XEX_HEADER_ENTRY_POINT: 825B3AF8`, `IMAGE_BASE_ADDRESS: 82000000`, `Image Size: 00710000`.
- `XEX_HEADER_ORIGINAL_PE_NAME: Banjo2_CPR.exe`; `Time Stamp: 49A69CFF - Thu Feb 26 10:45:35 2009`.
- Bibliotecas estáticas `D3D9`/`D3DX9` 2.0.7776.0; imports `xboxkrnl.exe` (269) e `xam.xex` (106).
- `Hardware scaler: width ratio 1:1, height ratio 1:1, final aspect ratio 16:9`.
- Avisos esperados do emulador: `NtQueryInformationFile(XFileXctdCompressionInformation) unimplemented`.

## Pós-condições verificadas

- `linha_de_base_hashes_pos_smoke.json`: **36/36 arquivos do jogo idênticos** (PASS).
- Instalação do Xenia do usuário (`W:\Emulador\Xbox360\Emulators\Xenia Canary`): datas de
  `recent.toml`, `xenia-canary.config.toml` e `xenia.log` inalteradas (03/08/2026).

## Limites (não declarado PASS)

- Não houve entrada de controle: **menu START, jogabilidade, saves e gamepad não foram testados**.
- FPS do original não foi medido em execução (o Xenia não grava FPS no log). A taxa vem do código
  (ver relatório, item 60 FPS).
- Modo de licença do emulador = 0 (trial): não distingue versão completa × demo.
