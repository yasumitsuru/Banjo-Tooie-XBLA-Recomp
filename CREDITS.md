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
