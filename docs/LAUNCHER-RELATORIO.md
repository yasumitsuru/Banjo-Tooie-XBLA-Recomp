# Launcher portátil — adaptação do MC_LA_Recompiled (2026-11-16)

Missão: adaptar o launcher do [MC_LA_Recompiled](https://github.com/robertoaraujo77/MC_LA_Recompiled)
para este port, como um launcher gráfico em PT-BR, 100% portátil, que usa a cópia do Banjo-Tooie
XBLA **já descompactada** da pessoa, com build Release e ZIP prontos para testes.

**Resultado:** entregue e testado. Bateria de ponta a ponta sobre o ZIP final: **42/42 PASS**
(`tools/launcher/testar_launcher.py`, resultado em `docs/evidencias/launcher/resultado.json`), mais
um percurso pela própria interface (UI Automation): Conferir, Gráficos → SALVAR, JOGAR → o jogo
abriu, foi fechado e o launcher registrou "Jogo fechado (código 0)".

## Onde está e como usar

| O quê | Onde |
|---|---|
| Código do launcher (GPL-3.0-or-later) | `launcher/` (`Core.cs`, `Launcher.cs`, `Launcher.xaml`, `assets/`, `pacote/`) |
| Build do launcher + pasta portátil + ZIP | `tools\build_launcher.cmd` (também roda no fim de `npm run build` / `tools\build_native.cmd`) |
| Executável Release x64 | `out\launcher\Banjo-Tooie PT-BR Recompiled.exe` |
| Pasta portátil pronta | `out\portable\Banjo-Tooie-PT-BR-Recompiled\` (42 arquivos, 44,4 MB) |
| ZIP de distribuição | `out\portable\Banjo-Tooie-PT-BR-Recompiled-1.0.0-win64.zip` (13,9 MB; SHA-256 `dba079cf8e130601bfe7330a3b57a6f9ed949848d0f7b0598208d06abe0ce3cb`) |
| Testes | `python -I tools\launcher\testar_launcher.py --jogo "<pasta do jogo>"` |
| Capturas da interface | `launcher/capturas/*.png` |

Instruções para quem joga (também em `LEIA-ME.txt` dentro do ZIP):
1. Extraia o ZIP numa pasta gravável (não em "Arquivos de Programas").
2. Abra `Banjo-Tooie PT-BR Recompiled.exe`.
3. **Procurar pasta…** → a pasta do Banjo-Tooie XBLA descompactado (`default.xex` + `RAWFiles`).
   **Conferir** diz se serve e qual versão é.
4. Deixe ligado "Copiar os arquivos do jogo para dentro desta pasta portátil" e clique em
   **PREPARAR MEU JOGO**. Os originais nunca são alterados.
5. Ajuste Gráficos / Controles / Áudio e jogo (**SALVAR CONFIGURAÇÕES**) e clique em **JOGAR**.

## 1. Análise de compatibilidade

| Aspecto | MC_LA_Recompiled (referência) | Este projeto | Decisão |
|---|---|---|---|
| Tecnologia do launcher | C# 5, WPF + WinForms, .NET Framework 4, compilado pelo `csc.exe` do Windows; XAML embutido | Windows 11 x64; `csc.exe` do .NET 4 presente | **Compatível**: mesma tecnologia, sem instalar nada |
| Fonte do jogo | ISO do disco, extraída (xdvdfs) + bundle embutido + bootstrap PowerShell | XBLA já descompactado (`default.xex` + `RAWFiles`) | Extração/ISO/bundle **não se aplicam**; trocados pela conferência da pasta |
| Validação | tamanho/hash da ISO | pré-checagem do próprio port (`--preflight_only --boot_report`, 11 arquivos) | **Reaproveitada a do jogo** (fonte única) + SHA-256 de `default.xex`, `X360_strings.dat`, `db360*.cmp` |
| Versões | uma versão | PT-BR (alvo) e global em inglês (mesma imagem do XEX) | PT-BR e global aceitas e identificadas; outro `default.xex` é recusado |
| Instalação | Arquivos de Programas / LocalAppData, atalho na Área de Trabalho | pedido: 100% portátil | **Tudo relativo à pasta do launcher**; sem atalho, sem Registro |
| Opções | arquivo próprio do jogo | `pc_settings.toml` (Configurações de PC, dono de vídeo e PC, C-82) + `banjotooie.toml` (cvars do runtime) | O launcher edita **esses mesmos arquivos** (nenhum sistema novo de configuração) |
| Saves | pasta do jogo | port: `--user_data_root` | `dados\` dentro da pasta portátil |
| Shaders | `shader_prepare` | port já tem `--prepare-shaders` (inspirado no MC_LA) | Reaproveitado |
| Licença | GPL-3.0-or-later | base do port sem licença (uso pessoal) | Launcher derivado → GPL-3.0-or-later, com crédito |

## 2. Componentes reaproveitados

- Do **MC_LA_Recompiled** (via a primeira adaptação do projeto irmão `W:\Decomp\Banjo`): layout de
  duas colunas com navegação lateral, estilos (botões, toggles, ComboBox, cartões, barras de
  rolagem), XAML embutido como recurso, editor de opções "chave = valor" que preserva o resto do
  arquivo (`KeyValueFile`, gravação atômica `.tmp` + `File.Replace` com `.bak`), construtores de
  opções por grupo, perfis, exportação de backup `.zip`, tarefa em segundo plano com barra de
  progresso, detecção do jogo já aberto e o script de build com `csc.exe`.
- **Do próprio port**: pré-checagem (`--preflight_only`/`--boot_report`), pastas por argumento
  (`--game_data_root`, `--user_data_root`, `--cache_path`), `--prepare-shaders`, as chaves de
  `pc_settings.toml` e os cvars do runtime, o auto-teste `--self-test` (usado nos testes).
- Arte: `banjotooie.png` (favo de mel, arte original do projeto irmão) e o ícone do port.

## 3. Alterações

- **Novo** `launcher/` (≈990 linhas de C# e XAML): núcleo (`Core.cs`: pastas portáteis, conferência,
  preparação, opções, saves, monitores, diagnóstico), interface (`Launcher.cs`, `Launcher.xaml`) e
  modos de teste `--teste` / `--captura-ui`.
- **Novo** `tools/build_launcher.ps1` + `.cmd` (CRLF): compila Release x64 e monta pasta portátil +
  ZIP + `MANIFESTO-SHA256.txt`; recusa empacotar arquivo do jogo, save ou opções de desenvolvimento.
- `tools/build_native.cmd`: depois do build C++, chama `build_launcher.ps1` (o build do projeto
  regenera o launcher e o pacote com o port recém-compilado).
- **Port** (opção nova, real, também no menu F1): `internal_scale` em `pc_settings.toml`
  (0 automática, 1 = 720p, 2 = 1440p, 3 = 4K) e a linha GRÁFICOS → **RES. INTERNA** no menu
  (`src/pc/pc_settings.{h,cpp}`, `src/pc/pc_menu.cpp`). Vale ao reiniciar, como a resolução.
- **Novo** `tools/launcher/testar_launcher.py` (bateria de 42 verificações).

Pasta portátil:

```
Banjo-Tooie PT-BR Recompiled.exe   launcher
bin\      banjotooie.exe, rexruntime.dll, runtime VC++ (msvcp140, msvcp140_atomic_wait, vcruntime140, vcruntime140_1)
jogo\     cópia conferida dos arquivos do jogo (criada na preparação, se escolhida)
dados\    saves, backups, pc_settings.toml, logs de sessão, crashes
cache\    shaders
config\   launcher.ini (jogo = "jogo", relativo)
logs\     launcher.log, preflight.json, shaders.json, diagnósticos
LICENCAS\ GPL-3.0 do launcher, BSD-3 do ReXGlue, licenças de terceiros, nota do runtime VC++
LEIA-ME.txt, CREDITOS.txt, MANIFESTO-SHA256.txt
```

## 4. Configurações disponíveis (todas lidas pelo jogo)

| Página | Opção | Arquivo / chave |
|---|---|---|
| Gráficos | Resolução de saída (Automática, 1080p, 1440p, 4K) | `dados\pc_settings.toml` `resolution`, `resolution_auto` |
| | Resolução interna (Automática, 720p, 1440p, 4K) | `pc_settings.toml` `internal_scale` |
| | Modo de exibição (Janela, Sem bordas, Tela cheia) | `pc_settings.toml` `display_mode` |
| | Monitor (Automático ou cada monitor ligado) | `pc_settings.toml` `monitor` |
| | VSync | `bin\banjotooie.toml` `host_vsync` |
| | Limite de FPS (60 / 30 original), ritmo fixo em 60, cenas a 60 (experimental) | `pc_settings.toml` `fps60`, `lock60`, `scenes60` |
| | Antisserrilhado (Desligado, FXAA, FXAA forte), MSAA 2×, anisotrópica (jogo, 0, 2–16×) | `banjotooie.toml` `swap_post_effect`, `native_2x_msaa`, `anisotropic_override` |
| | Distância de desenho (Original, Longe, Total) | `pc_settings.toml` `draw_distance` |
| | Perfis Original / Equilibrado / 4K máximo; Restaurar padrões; preparar shaders | combinações das opções acima; `--prepare-shaders` |
| Controles | Vibração, zona morta, sensibilidade e inversão do analógico direito | `pc_settings.toml` `pad_*` |
| | Câmera pelo mouse, sensibilidade, inversão, suavização | `pc_settings.toml` `mouse_look`, `cam_*` |
| | Teclado: 18 botões do controle + 4 teclas de câmera | `banjotooie.toml` `keybind_*`; `pc_settings.toml` `cam_key_*` |
| Áudio e jogo | Volume geral, silenciar | `pc_settings.toml` `volume_master`; `banjotooie.toml` `audio_mute` |
| | Velocidade dos diálogos, início rápido, pausar ao perder o foco, rastreador de colecionáveis, contador de FPS, estatísticas | `pc_settings.toml` |
| | Backups automáticos e quantidade, registro detalhado | `pc_settings.toml` `save_backup`, `backup_count`, `log_detailed` |
| Saves | Exportar/importar `.zip`, copiar o save de Documentos, abrir pastas | `dados\` |
| Diagnóstico | Registro do launcher, gerar diagnóstico, abrir registros e falhas | `logs\`, `dados\logs`, `dados\crashes` |

4K real a 60 FPS: perfil **4K máximo** = saída 4K + imagem interna 4K (3× de 1280×720) + 60 FPS
(medido no jogo real na camada de PC/M04). Não há opção fictícia: AMD CAS/FSR (`present_effect`)
**não** aparece porque este build do runtime só aceita `bilinear`; volumes de música/efeitos ficam
no menu do próprio jogo; os botões do controle físico não são remapeáveis pelo runtime (só o teclado).

## 5. Testes

Bateria `tools/launcher/testar_launcher.py` sobre o ZIP final, extraído numa pasta com espaços —
**42/42 PASS** (`docs/evidencias/launcher/resultado.json`):

| Grupo | Verificações |
|---|---|
| Pacote | ZIP íntegro (CRC), nenhum arquivo do jogo/save/opção de desenvolvimento, launcher + port + runtime VC++ + LEIA-ME + créditos + licenças, SHA-256 de cada arquivo extraído = manifesto |
| Conferência | pasta vazia (lista do que falta), pasta inexistente, versão incompatível (`default.xex` falso, recusado pelo SHA-256), pasta real reconhecida como **PT-BR (tradução conferida)** |
| Preparação | concluída; `launcher.ini` com `jogo = "jogo"` (relativo); cópia em `jogo\` com SHA-256 igual ao original nos 36 arquivos |
| Opções | 18 opções gravadas e relidas iguais; vídeo só em `pc_settings.toml` (v4); cvars em `bin\banjotooie.toml`; preservadas depois de o jogo regravar o arquivo |
| Jogo pelo launcher | auto-teste do port **10/10**, código 0 em 73 s; dados, cache e jogo dentro da pasta portátil (boot report); `1080p escala 1x` aplicados; FXAA e anisotropia 8× no registro de sessão |
| Portabilidade | pasta movida para outro caminho: continua preparada, acha `jogo\`, o jogo abre (código 0) com dados/cache na pasta nova, opções seguem, nenhum caminho absoluto antigo gravado |
| Saves | exportar `.zip`; importar `.zip` (dados anteriores guardados em `backups-launcher\`); copiar o save de Documentos |
| Shaders | `--prepare-shaders` pelo launcher: relatório da placa e do cache |
| Diagnóstico | arquivo com programa, jogo, monitores e opções |
| Originais | pasta do jogo da pessoa idêntica (36 SHA-256); saves em Documentos\My Games sem mudança (foto antes/depois) |
| Offline/Registro | nenhum código de rede nem de Registro no launcher |
| Interface | as 7 páginas desenham (capturas em `launcher/capturas/`) |

Também pela interface real (UI Automation, pasta movida): **Conferir** → "✔ Pasta reconhecida";
Gráficos → **SALVAR** → "Configurações salvas (Gráficos): 12 opções"; **JOGAR** → launcher
minimizado, jogo aberto com os argumentos portáteis, fechado depois de 20 s → "Jogo fechado
(código 0)" e launcher restaurado. Regressão do port: `npm run build` (código 0) e o check do Craft.

## 6. Correções durante o trabalho

- Opções do runtime: os padrões reais são MSAA 2× **ligado** e anisotrópica **4×** (`3`), não os
  do launcher irmão; `present_effect` CAS/FSR não é aceito por este build (ficou de fora); o
  launcher irmão gravava `vsync` (ritmo do console) em vez de `host_vsync` e gravava resolução no
  `banjotooie.toml`, que o jogo agora ignora (C-82).
- Start = Esc e B = Shift (padrões do runtime) não são oferecidos: o port os troca na abertura.
- Build: `Get-FileHash` indisponível no PowerShell chamado pelo `.cmd` (trocado por .NET);
  `"$f:"` lido como unidade pelo PowerShell; `.ps1` com BOM para acentos no PowerShell 5.
- Interface: rótulo "Registro e diagnóstico" cortado na navegação (agora "Diagnóstico"); botões de
  perfil sem nome acessível (agora com `AutomationProperties.Name`).

## 7. Limitações e cuidados

- **Distribuição:** o ZIP não tem nenhum arquivo do jogo, mas `bin\banjotooie.exe` contém código
  recompilado do jogo e a base do port não declara licença: é para **uso pessoal** (`docs/DECISOES.md`
  nº 4). Não publique o ZIP sem permissão do autor da base; a pasta preparada contém os arquivos do
  jogo e também não deve ser compartilhada.
- Testado só neste PC (Windows 11, RTX 5080, 3 monitores). Não foi testado num Windows limpo sem o
  Visual Studio: o runtime VC++ vai junto em `bin\` (implantação local), e o .NET Framework 4 vem
  com o Windows 10/11.
- O cache de shaders **do driver** (por exemplo DXCache da NVIDIA) fica onde o driver decide, fora
  da pasta; o jogo só **lê** `HKCU\Control Panel\Desktop\WindowMetrics\AppliedDPI`. Nada é gravado
  no Registro.
- Restaurar um backup automático específico continua no menu F1 → SAVES do jogo (que confere o
  arquivo); o launcher exporta/importa a pasta `dados\` inteira.
- Uma instância só não é imposta; o launcher avisa quando o jogo já está aberto.

## 8. Créditos

MC_LA_Recompiled (Roberto Araujo, GPL-3.0-or-later) como modelo do launcher; projeto irmão
`W:\Decomp\Banjo` (primeira adaptação); iron-jay (base do port); ReXGlue SDK (BSD-3-Clause,
derivado do Xenia) e bibliotecas de terceiros (licenças em `LICENCAS\terceiros`). Detalhes em
`CREDITS.md`.
