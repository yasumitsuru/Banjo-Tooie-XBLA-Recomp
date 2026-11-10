# Créditos e proveniência dos arquivos originais do jogo

O jogo **Banjo-Tooie** é propriedade de Microsoft/Rare. Este projeto não redistribui nem reproduz os arquivos do jogo fora do computador da pessoa dona da cópia.

## Arquivos originais usados como fonte

Nesta missão (auditoria) os arquivos foram apenas **lidos** — nada foi copiado para `app/` ainda. A próxima missão os usará diretamente da pasta de origem (sem cópia) ou os importará para uso privado.

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

## Ferramentas e código de terceiros (futuro)

- Port base: [iron-jay/banjo_tooie_xbla_recomp](https://github.com/iron-jay/banjo_tooie_xbla_recomp)
- Runtime/SDK: [ReXGlue](https://github.com/rexglue/rexglue-sdk) (fork `iron-jay/rexglue-sdk`), derivado do [Xenia](https://xenia.jp)
- Referências de recompilação: [hedge-dev/XenonRecomp](https://github.com/hedge-dev/XenonRecomp), [MC_LA_Recompiled](https://github.com/robertoaraujo77/MC_LA_Recompiled) (padrão de port PT-BR)

## Usados só como referência/verificação na auditoria (nada copiado para o produto)

| Item | Origem | Uso |
|---|---|---|
| `overrides.toml` / `banjotooie_manifest.toml` / `launcher.py` da base | clone de referência `%TEMP%\opencode\bt-xbla-base` (commit `72c2397`) | comparação endereço a endereço com o XEX do usuário (`docs/evidencias/overrides_vs_xex.json`) |
| Patches da comunidade do Xenia "58410955 - Banjo-Tooie" (autores: illusion, retroben, wedarobi) | `W:\Emulador\Xbox360\Emulators\Xenia Canary\patches\58410955 - Banjo-Tooie.patch.toml` (instalação do usuário, só lida) | referência de endereços para 60 FPS / draw distance / LOD (`docs/evidencias/xenia_patches_vs_xex.json`); **não** aplicados |
| Xenia Canary (`canary_experimental@7ef873b0d`) | cópia do exe do usuário em `app/_audit/out/xenia_smoke/` (ignorada pelo Git) | smoke test do jogo original, em modo portátil isolado |
| Código-fonte do fork `iron-jay/rexglue-sdk@035aa25` (BSD-3, Tom Clay + Xenia) | cópia de leitura em `app/_audit/out/sdk_fork_035aa25/` (ignorada pelo Git), baixada pela API do GitHub | rev. 3: leitura da ponte gráfica (`docs/evidencias/ponte_grafica_sdk_035aa25.json`); a lista de opções em `docs/evidencias/sdk_cvars_035aa25.txt` cita linhas desse código |
| README/metadados de XenonRecomp (hedge-dev, sonicnext-dev), XenonRecompUnlimited, MC_LA_Recompiled e ReXGlue oficial | API do GitHub, em `app/_audit/out/fontes_readme/` (ignorada pelo Git) | rev. 3: avaliação de compatibilidade e licenças (`docs/evidencias/fontes_consulta.json`) |
