# MECHANICS.md — Valores reais do executável original (default.xex)

**Análise estática do programa original, feita nas rodadas de revisão da MISSÃO 00** (não é
uma missão separada; o commit `d938a31` foi rotulado "MISSAO 01" por engano). Revisão 2:
§6 (cadência) e §10 corrigidos; ver `docs/AUDITORIA-MISSAO-00.md` §0. Revisão 3: §10 item 2
(4K) atualizado com o que o código do runtime da base mostra. Missão 02 rodada 2: §11 (textos
traduzidos e regra real de idioma) e §10 item 3 corrigido. **Revisão 4 (2026-11-11)**: o
executável foi reaberto nesta sessão no Craft Anything em modo Deep e cada função/constante da
página foi conferida de novo (pseudocódigo Ghidra + bytes); 4 correções e a tabela completa de
constantes do "60 FPS" — ver **§12**. Todos os valores, fórmulas e regras desta página foram
lidos do `default.xex` original (sha256 `3efca627…`), via Craft Anything (Ghidra 12.1.3,
linguagem `PowerPC:BE:64:A2ALT-32addr`), e conferidos contra o dump headless do Ghidra com a
extensão XEXLoaderWV. Nada foi inventado; onde a análise ficou parcial, está marcado.

> Endereços: todos são endereços **reais do XEX** (base de imagem `0x82000000`).
> O Ghidra do Craft Anything carrega a imagem desempacotada em `0x0`; vale a regra
> **endereço real = endereço do Ghidra + 0x82000000**.

---

## 1. Como o executável foi aberto

O XEX2 não é reconhecido pelos motores padrão, então:

1. `xex_unpack.py` (em `_audit/`) desempacota o XEX2 (mesma lógica do XEXLoaderWV) para a
   imagem plana `_audit/out/xex_image.bin` — **arquivo não commitado** (pasta `out/`),
   regenerável a qualquer momento.
2. A imagem plana é aberta no Craft Anything como `raw-binary` com
   `language_id = PowerPC:BE:64:A2ALT-32addr` (o Ghidra assume a análise).
3. Um dump paralelo do Ghidra headless sobre o `default.xex` original (com XEXLoaderWV)
   produz os mapas de funções/referências em `_audit/out/ghidra_xex_out/`
   (`functions.txt`, `call_edges.txt`, `refs_to_data.txt`) — também regeneráveis com
   `run_headless.cmd` e `DumpXex.java`.

### Mapa do executável

| Item | Valor real |
|---|---|
| Formato | XEX2 (PowerPC big-endian, 64-bit com endereços de 32) |
| Base de imagem | `0x82000000` |
| Ponto de entrada | `0x825B3AF8` (`mflr r12; bl` para salvar registradores; a rotina continua depois disso e contém as duas únicas chamadas a `XamLoaderTerminateTitle`, em `0x825B3B44` e `0x825B3CB8` — §11.5) |
| Seções | `.rdata` `0x82000400` · `.pdata` `0x82080400` · `.text` `0x820A0000–0x825F39C7` · `.data` `0x82600000` · `.XBMOVIE` `0x826CCE00` · `.idata` `0x826D0000` · `.XBLD` `0x826E0000` · `.reloc` `0x826E0200` |
| Funções | **16.996** encontradas (15.295 declaradas na tabela `.pdata`) |
| Importações | **122** resolvidas (xboxkrnl.exe: 269 registros, xam.xex: 106) |
| PDB original | `c:\work\Rare\BanjoKazooie\360\Banjo2\Xbox 360\Release_ContentPackage\Banjo2.pdb` (string em `0x8207FDC0`) |
| Sem criptografia/compressão | módulo flags `0x1` (title module); base file format: encryption 0, compression 0 |

Evidências: ev_cc0c07e80 (caminho do PDB), ev_26d9261f3 (triage), dump em `_audit/out/ghidra_xex_out/program.txt`.

---

## 2. Boot — fluxo real de inicialização

Cadeia verificada (pseudocódigo Ghidra):

```
0x825B3AF8  entry          -> mflr r12; bl 0x825505B0      (Function_825B3AF8, 8 bytes)
0x825505B0  startup        -> bl 0x8259ADA0 (não retorna)
0x8227D9F0  main           -> lógica abaixo
0x8227D340  validação de dados do jogo (mostra o erro fatal se falhar)
0x822209E0  loop do jogo
```

`main` (`0x8227D9F0`, ev_be29fe13d) faz, em ordem:

1. `FUN_0027cfb0(...)` — parse/inicialização de configuração (se falhar, sai).
2. `FUN_002ce398(0x8269F620, ...)` — inicialização do sistema (heap/globais).
3. `FUN_0027d2d0()` — máquina de estados de inicialização.
4. **Mapeamento estado → modo** (regra real do jogo):
   - estado `2` → modo `1` · estado `3` → modo `2` · estado `4` → modo `4`
   - estado `5` → modo `3` · outro → modo `0` · estado `1` → modo `5` (+ chamada extra `FUN_0026b4d0`)
5. `FUN_005401a8()` — preparação do loop principal.
6. Se a inicialização falhou (`iVar2 != 0`): **diálogo de erro fatal** `0x8227D128`.
7. `FUN_0027d340` — validação/carregamento dos dados do jogo (`.cmp`, `.tbl`, `.ctl`).
8. `FUN_002209e0(0)` — entrada no jogo.

### Carregamento de texturas — `0x8227D0C0` (rev. 3; ev_aa0b87d2a, ev_75f22fe8f, ev_903172f33)

1. `0x822D3980` monta o caminho `"%s%s"` (`0x82003E00`) + `RAWFiles\db360_TexFileList.txt`
   (`0x82005898`) e tenta abri-lo. **Se o arquivo não existir, devolve 0 sem erro.** Se existir,
   lê uma lista (`"%s %d"`, `0x8200588C`) de texturas soltas em `RAWFiles\` e monta a tabela delas
   (registros de 0x48 bytes em `0x826A4F34`).
2. Quando a lista não existe (caso da pasta fornecida), `0x8227D0C0` carrega o pacote
   `db360.textures.cmp` (`0x82003520`) por `0x822D3DA0` e grava na global de estado das
   texturas **`0x826AEE20`** o valor **2** (pacote carregado) ou **3** (falha):
   `cntlzw(resultado & 0xFF) >> 5 + 2` (pseudocódigo ev_034efdbf5; instruções
   `addis r10,r0,0x826A; …; stw r11,0xEE20(r10)` em `0x8227D0FC`–`0x8227D10C` e o
   `stw r11,0xEE20(r21)` de `0x8227D688`, que a grava como **1** na inicialização —
   lista/moldura de desenvolvimento).
   > **Correção (rev. 4):** as revisões anteriores diziam `0x8269EE20`. A aritmética das
   > instruções (`0x826A0000 + 0xEE20`) dá `0x826AEE20`; o rótulo `_DAT_8269ee20` do Ghidra
   > deste endereço é enganoso. Os dois pontos de escrita conferidos por bytes.
3. Conclusão: `db360_TexFileList.txt` é um caminho alternativo de texturas soltas (provável recurso
   de desenvolvimento); o jogo publicado usa o pacote, que está presente.

---

## 3. Diálogo de erro fatal (dados ausentes/corrompidos)

Função: `0x8227D128` (ev_94593f2b3). Regras reais:

- Chama `FUN_005b1fa8()` (ev_6e1ca1402) para obter o **id de idioma**.
- Seleciona 3 strings por idioma (título, texto, botão) de uma tabela em memória
  (strings UTF-16BE gravadas em linha, 0x70 bytes cada):

| Idioma (id) | Título | Endereços (título/texto/botão) |
|---|---|---|
| 2 — Japonês (.data) | `読み込みエラー` | `0x8263E7A0` / `0x8263E7B0` / `0x8263E7FC` |
| 3 — Alemão | `Ladefehler` | `0x82003628` / `0x82003580` / `0x82003548` |
| 4 — Francês | `Erreur de chargement` | `0x82003708` / `0x82003688` / `0x82003640` |
| 5 — Espanhol | `Error al cargar` | `0x820037E8` / `0x82003780` / `0x82003738` |
| 6 — Italiano | `Errore di caricamento` | `0x820038C0` / `0x82003840` / `0x82003808` |
| padrão (EN) | `Loading Error` | `0x82003990` / `0x82003920` / `0x820038EC` |

Texto EN: `"Banjo-Tooie" has failed to load, and cannot continue.` — botão `Return to Game Library`.
(Textos completos dos 6 idiomas: rodar `_audit/extract_tables3.py`.)

- Mostra com **`XamShowMessageBoxUI`** (import `0x825F3334`).
- **Retry**: enquanto o resultado for `5`, espera `0xFA` = **250 ms**
  (`FUN_005b1f48` → `KeDelayExecutionThread`, ev_06aab1ac6) e tenta de novo.
- Se desiste: registra a tag `XLNI_DASH_ARCADE` (`0x82003534`) com `FUN_005b1eb8` e
  **derruba o jogo de propósito** (instrução inválida → halt).

**Regra do idioma** (`FUN_005b1fa8`, `0x825B1FA8`):

1. Lê `ExGetXConfigSetting(categoria=3, chave=9, valor 4 bytes)` — o id de idioma salvo
   (categoria 3 = categoria do usuário; chave 9 = `XCONFIG_USER_LANGUAGE_ID`).
2. Se o valor for inválido (`0` ou `> 0xC`): consulta `XGetGameRegion` (`0x825F31A4`):
   - região `0x101` (Japão) → id `2`
   - outra região `0x01xx` → id `7`
   - senão → id `1` (inglês).

**Implicação para o port PT-BR**: os 6 idiomas deste diálogo estão no próprio XEX
(EN/DE/FR/ES/IT/JA — **sem PT-BR**). O texto do jogo em si vive nos arquivos de dados
(`db360.cmp`, `banjo2_360.tbl/.ctl`) — o XEX contém só estas mensagens de sistema.

---

## 4. Sistema de ajustes (settings)

Funções reais e regras verificadas:

| Função | Endereço | Papel |
|---|---|---|
| `FUN_00565300` | `0x82565300` | Inicializa o singleton de ajustes de vídeo em `0x826B65AC` (ev_087d6be3a) |
| `FUN_00565720` | `0x82565720` | Callback que grava o valor do ajuste (ev_4b6338a84) |
| `FUN_00566240` | `0x82566240` | Aplicador com tabela de 8 sub-operações (ev_e7a18543e) |
| `FUN_00565c00` | `0x82565C00` | Despacho de leitura/escrita (ev_70c485244) |
| `FUN_00565d68` | `0x82565D68` | Getter (ev_ae09aefdd) |
| `FUN_005769c8` | `0x825769C8` | Singleton do gerenciador de configuração em `0x826B789C` (ev_0a6546167) |

Campos gravados no singleton `0x826B65AC` (na inicialização, `.data` começa zerado):

```
+0x04 = 0            +0x08 = 0xA8 (valor padrão)     +0x0C = 0 (flags)
+0x10 = 0x820740AC   (ponteiro para o nome "GrayAmount")
+0x14 = 4            (tipo)
+0x18 = resultado de FUN_005769c8 (pai/configurador)
+0x24 = 0x82565720   (callback)
+0x30 = flag de inicializado (bit 0)
```

Chaves reais (UTF-16BE, `.rdata`): **`GrayAmount`** `0x820740AC` · **`BlurAmount`**
`0x820740C4` · **`Brightness`** `0x820740DC` · literais `false` `0x820740F4`,
`true` `0x82074100`, formatos `%d` `0x8207410C`, `%u` `0x82074114`, `%f` `0x8207411C`.

Regras do callback (`FUN_00565720`):
- resolve o objeto com `FUN_00562068`; se o objeto for nulo **ou o tipo != 4** →
  erro `0x80070057` (`E_INVALIDARG`).
- grava o valor em `objeto + 0xA8` e marca `objeto + 0xC |= 2` (sujo/alterado).

Códigos de erro usados pelo subsistema: `0x80070057` (argumento inválido),
`0x80300001` e `0x80300008` (erros XUI/genéricos).

---

## 5. Áudio — valores e fórmulas reais

- Dispositivo: **XAudio2** (imports `XAudioRegisterRenderDriverClient` `0x825F3554`,
  `XAudioUnregisterRenderDriverClient` `0x825F3544`, `XAudioGetSpeakerConfig` `0x825F3564`).
- Nomes de dispositivo (UTF-16LE): `Xbox 360 audio device` `0x8204F06D`, `Audio` `0x8204F099`.
- Vtable do subsistema de áudio em `0x8204F0A4` (23 ponteiros para `0x82514xxx–0x82515xxx`).

### Fórmulas (funções reais)

`FUN_0050c6d8` (`0x8250C6D8`, ev_6ad77faa8) — atualização de filtro/envelope:

- `dVar5 = (pfVar1[1] / *pfVar1) * _DAT_82001B94` — razão × **2π** (`0x82001B94 = 6.283185`).
- Ramo com **equação quadrática** (limiar `_DAT_8204efc8`):

```
x = ( -(a·c − d) − √( (d−c)·c·2.0 − (−(c² − d)·c)·c ) ) / (d − c)
```

com constantes reais `.rdata`: `0x82000750 = 0.1`, `0x8200075C = 2.0`, `0x82000B48 = 0.05`,
`0x82000738 = 1.0`, `0x82000734 = 0.0`.

`FUN_0050d050` (`0x8250D050`, ev_8b064c5b1) — parâmetros de voz:

- `valor = entrada × _DAT_82000F1C` onde `0x82000F1C = 48.0` → conversão
  **ms → amostras a 48 kHz** (48 amostras por milissegundo ⇒ **taxa do motor = 48 kHz**).
- **Clamp em 12 bits**: se `valor > 0xFFF` (4095), usa `0xFFF`.
- O mesmo valor é copiado para 4 campos do objeto de voz.

### Espera/sleep (kernel)

`FUN_005b81f0` (`0x825B81F0`): converte **ms → unidades de 100 ns** (`× 10000`, sinal
negativo) e chama `KeDelayExecutionThread` (`0x825F38E4`); repete enquanto retornar
`0x101` (`STATUS_ALERTED`) e o chamador for "alertable"; devolve `0xC0`
(`STATUS_CANCELLED`) se cancelado, senão `0`.

---

## 6. Vídeo / render — o que interessa para 4K/60

### Ajuste do modo de vídeo — `FUN_003021a8` (`0x823021A8`, ev_5652ba0fc)

- Se `param_3 != 0`, monta as flags para **`VdSetDisplayMode`** (`0x825F2F94`):
  - base `0x40000000`; `(info[0x11] & 3) << 0x1C` (ou seja, **bits 28–29** — rev. 4: as
    revs anteriores diziam "bits 30–31"; é `<<0x1C` no pseudocódigo ev_30f5711b8);
  - formato de superfície `(info[0x10] & 0x3F) == 7` ou `== 0x36` → bit `0x08000000`;
  - **detecção de SD**: largura `0x2D0` (720) com altura `0x1E0` (480, NTSC) **ou**
    `0x240` (576, PAL) → flag `2`.
- Depois lê **`VdGetCurrentDisplayInformation`** (`0x825F2F84`) e grava
  largura/altura/taxa atuais em `global + 0xA8 / 0xAC / 0xB0`.

### Apresentação de quadro — `FUN_002fa358` (`0x822FA358`, ev_c73be928b)

- Chama **`VdSwap`** (`0x825F2E74`) — a troca de buffer real.
- Antes de apresentar, chama `FUN_003021a8` (ajuste do modo).
- Incrementa contador de quadros em `contexto + 0x40B0`.
- Converte formatos de superfície: `0x32` → `3`, `7` → `0x1B` (ids internos do jogo).

### Agendador de troca de quadros (vsync) — `FUN_002f9cb8` (`0x822F9CB8`, ev_2d9ddba0e)

> **Correção (rev. 2):** a rev. 1 leu o `0x3C` daqui como "o jogo é 60 FPS por padrão". A
> releitura mostra que é o agendador de apresentação do runtime D3D. O `0x3C` = 60 é a **taxa de
> atualização da tela (Hz)**, usada quando o campo `contexto + 0xB4` é 0. Não é a meta de FPS do jogo.

- `ticks_por_vblank = KeQueryPerformanceFrequency() / refresh_hz` (padrão 60 Hz).
- Mede com o **registrador Timebase (TBLr)** do PowerPC quanto do intervalo atual já passou:

```
percentual = min(100, ((TBLr − último_vblank) × 100) / ticks_por_vblank + 1)
```

- Próximo vblank de apresentação = último + **intervalo** (bits 8–11 do parâmetro de
  apresentação). Se já passou e o percentual excede o limiar (byte baixo do parâmetro), apresenta
  imediatamente. Senão, enfileira num anel de 16 entradas (`contexto + 0x40BC/0x40C0`).
- Chama o callback de swap em `contexto + 0x409C` quando existe; contexto **por núcleo**
  (tabelas `0x820005CC` / `0x820005D0`).

### Intervalo de apresentação do jogo (FPS real) — `0x82276E34` (ev_e8df486c…)

- Código original: `lis r11,0x826A; li r8,0; addi r11,r11,-0x4E40; **li r9,2**; lwz r10,0(r11);
  sth r9,-4(r11)` → grava **2** no halfword global `0x8269B1BC`.
- Intervalo 2 numa tela de 60 Hz = **30 FPS** (inferido). O patch "60 FPS" da comunidade do Xenia
  troca exatamente esta instrução por um desvio que escolhe 1 (60 FPS) ou 2 conforme o byte
  `0x8266EF30`. Também troca `lwz r11,0x351C(r31)` em `0x822F9F18` e 17 constantes de tempo
  (ver `docs/evidencias/xenia_patches_vs_xex.json`).
- **Medido na Missão 01** (port nativo, tela de abertura, 2 execuções): **29,45 e 29,61 quadros
  por segundo** (chamadas de `0x822FA358`, a função que chama `VdSwap`, contadas no último segundo
  antes da captura); 2.235 e 2.239 quadros em ~76 s (`docs/evidencias/m01/boot_jogo.json`). Confirma
  os 30 FPS do original. Só foi medido na abertura; ainda não durante o jogo.

### Inatividade (protetor de tela) — `FUN_005b36a0` (`0x825B36A0`, ev_92d9a18b5)

- `param_1 == 0` → `XamResetInactivity` (`0x825F3494`).
- `XamEnableInactivityProcessing(0, param_1)` (`0x825F3484`); em erro, mapeia para o
  código próprio `0x65B` (a menos que o erro seja da faixa `0x00070000`).

---

## 7. Interface XUI e shaders embutidos

- Classes XUI registradas pelo jogo (UTF-16LE): `XuiCanvas` `0x82073B6D`,
  `XuiElement` `0x82073B81`, `XuiText` `0x82073B99`, `XuiScene` `0x82073C19`,
  `XuiSound` `0x82073D19` (ev_f14454cb6). Funções que as usam: `0x8255EEB8`,
  `0x8255F740`, `0x82560348` (manipulação de elementos de texto — medição/layout).
- O jogo **embute fontes HLSL** e um **compilador de microcódigo de shader**:
  - pixel shader `ps_2_0` em `0x820039AC` (textura × cor, `return float4(t.r*PrimCol.r, ...)`);
  - vertex shader `vs_2_0` em `0x82003A94`;
  - mensagens do compilador em `.rdata` (ex.: "Config statements are not allowed in
    inline microcode" `0x820187B0`, "Shaders must contain at least one exece or cexece
    instruction" `0x820188D0`) — as strings "Options"/"Option" (`0x82018890`/`0x82018888`)
    pertencem ao compilador de shader, **não** a um menu de opções.
- Rede Xbox LIVE: URLs de conquistas (`0x8266838E` `/xstats/xachievementclear.ashx` etc.),
  `NetDll_WSAStartup` (`0x825F3264`), tag `XLNI_DASH_ARCADE` (`0x82003534`).
- Nome do arquivo de texturas compiladas: `db360.textures.cmp` (`0x8200351E`).

---

## 8. Constantes reais em `.rdata` (seleção)

> **Rev. 4:** o `.rdata` guarda pares de **f32 big-endian (4 bytes)** lado a lado; as revs
> anteriores as liam como doubles. Os valores abaixo são o f32 em cada endereço.

| Endereço | Valor (f32 BE) |
|---|---|
| `0x82000730` | `0.99999994` (bytes `3F 7F FF EF`; rev. 4: não é "1 − 1/65536") |
| `0x82000734` | `0.0` |
| `0x82000738` | `1.0` |
| `0x82000750` | `0.1` |
| `0x8200075C` | `2.0` |
| `0x82000778` | `0.01` |
| `0x8200081C` | `50.0` |
| `0x82000B48` | `0.05` |
| `0x82000F1C` | `48.0` (amostras por ms ⇒ 48 kHz) |
| `0x82001B94` | `6.283185…` (2π) |

Tabela completa das 17 constantes que o "60 FPS" troca (com endereço de origem, endereço de
destino e endereço do gancho): **§12.2**.

---

## 9. Imports notáveis (endereços resolvidos)

| Endereço | Função | Uso no jogo |
|---|---|---|
| `0x825F2F64` | `ExGetXConfigSetting` | idioma salvo (cat 3, chave 9) |
| `0x825F31A4` | `XGetGameRegion` | fallback de idioma |
| `0x825F3294` | `XGetLanguage` | idioma |
| `0x825F3334` | `XamShowMessageBoxUI` | diálogo de erro fatal |
| `0x825F34B4` | `XamShowMessageBoxUIEx` | diálogos |
| `0x825F38E4` | `KeDelayExecutionThread` | sleep em ms (×10000) |
| `0x825F2E74` | `VdSwap` | apresentar quadro |
| `0x825F2F94` | `VdSetDisplayMode` | modo de vídeo |
| `0x825F2F84` | `VdGetCurrentDisplayInformation` | info do display |
| `0x825F2E44` | `KeQueryPerformanceFrequency` | cadência de quadros |
| `0x825F3494` / `0x825F3484` | `XamResetInactivity` / `XamEnableInactivityProcessing` | protetor de tela |
| `0x825F3554` / `0x825F3564` | `XAudioRegisterRenderDriverClient` / `XAudioGetSpeakerConfig` | áudio |
| `0x825F35D4` / `0x825F35C4` | `XeCryptSha` / `XeCryptRotSumSha` | integridade dos dados |
| `0x825F34D4` | `XamLoaderTerminateTitle` | sair do jogo |
| `0x825F2FE4` / `0x825F37C4` | `KeBugCheck` / `KeBugCheckEx` | crash intencional |
| `0x825F3654` / `0x825F3644` | `RtlCaptureContext` / `RtlUnwind` | **setjmp** (`0x8259FD90`, usado pelo port) |

Lista completa: `_audit/out/ghidra_xex_out/functions.txt` (122 imports, 16.996 funções).

---

## 10. O que isto muda para o port (resumo para quem não programa)

1. **O jogo original roda a 30 FPS** (intervalo de apresentação 2 em `0x82276E34`; o `0x3C` de
   `0x822F9CB8` é a taxa da tela, 60 Hz). Para 60 FPS, o port precisa mudar o intervalo e as
   constantes de tempo; os pontos exatos estão no patch "60 FPS" da comunidade, cujos ganchos
   conferem com este XEX.
2. **4K**: a resolução vem de `VdSetDisplayMode`/`VdGetCurrentDisplayInformation`
   (`0x823021A8`). Rev. 3: no runtime da base (fork ReXGlue `035aa25`), o modo de vídeo que essas
   chamadas devolvem ao jogo vem das opções `video_mode_width`/`video_mode_height` (padrão
   1280×720, limite 640–4095) ou do preset `resolution=4k` (3840×2160), e a escala de
   renderização `resolution_scale` vai de 1 a 8×. São opções do runtime, não valores do jogo; qual
   delas o jogo aceita sem quebrar a interface só será sabido no teste do M7
   (`docs/AUDITORIA-MISSAO-00.md` §3.5).
3. **PT-BR**: o XEX tem 6 idiomas no diálogo de erro (EN/DE/FR/ES/IT/JA) e nenhum PT-BR. Já o
   texto do jogo vive nos dados — menus da camada XBLA em `X360_strings.dat`, diálogos em
   `db360.cmp` (os `banjo2_360.tbl/.ctl` são áudio) — e **os dados fornecidos já estão em PT-BR**,
   no espaço do inglês. A regra real de escolha do idioma e o formato que o jogo lê estão no §11
   (Missão 02 rodada 2); as telas conferidas no jogo estão em `docs/MISSAO-02-TRADUCAO.md`.
4. **Áudio**: motor a **48 kHz**, parâmetros com clamp de 12 bits (4095) — reproduzir
   exatamente evita estouros/sons errados no port.
5. **Ajustes de vídeo** existem de verdade no XEX: `GrayAmount`, `BlurAmount`,
   `Brightness` (tipo 4, padrão `0xA8`), com gravação em `objeto+0xA8` e flag de
   alterado em `+0xC |= 2`.
6. O **`setjmp`** que a base do port sobrescreve (`0x8259FD90`) existe neste XEX
   (usa `RtlCaptureContext`/`RtlUnwind`). Na rev. 2, **todos os 49 endereços** do
   `overrides.toml` da base foram conferidos contra as previsões da própria base: 49/49
   (`docs/evidencias/overrides_vs_xex.json`). A imagem do XEX também confere com os 113 hashes
   SHA-1 do seu cabeçalho (`docs/evidencias/xex_integridade.json`).

## 11. Textos traduzidos e idioma — regras reais (Missão 02, rodada 2, 2026-11-10)

Lido nesta sessão com o Ghidra do Craft Anything (`xex_image.bin` aberto como `raw-binary`,
`PowerPC:BE:64:A2ALT-32addr`; endereço real = endereço do Ghidra + `0x82000000`). As chamadas
`bl`/`b` e os pares `lis`/`addi` que o dump antigo não registrou foram achados por varredura das
instruções da imagem (somente leitura) e conferidos no pseudo-C.

### 11.1 Carregador de `X360_strings.dat` — `0x822E9C90` (ev_4a9501f8e)

Chamado por `main` com o **número do bloco** (`param_1`). Regras, na ordem:

1. Se já houver bloco carregado (global `0x8264B59C` ≠ 6, que significa "nenhum"), libera o
   anterior (`0x822E9BD8`); grava o novo número em `0x8264B59C`.
2. Caminho = pasta do jogo (`0x8227E758`) + `RAWFiles\X360_strings.dat` (`0x82005EB4`; o prefixo
   `GAME:\` fica em `0x82005EAC`).
3. Cabeçalho: `n` = u16 e `L` = u16 (**little-endian**: `0x822E9AE0` lê 2 bytes e devolve
   `b1<<8 | b0`, ev_d867a0f0c), guardados em `0x826A60F0` (n) e `0x826A60F4` (L); depois `L`
   tamanhos u32 (**little-endian**, `0x822E9B40`, ev_26f24e2fb) em `0x826A60D0 + 4·i`.
4. Pula as tabelas de comprimento dos blocos anteriores (`n·bloco·4` bytes, a partir da posição
   atual) e lê as `n` comprimentos u32 do bloco pedido para `0x826A60CC`.
5. Vai ao início dos dados do bloco: **`4 + 4·L + 4·n·L + Σ tamanho[k] (k < bloco)`**, aloca
   `tamanho[bloco]` bytes (`0x826A60EC`) e lê **só esse bloco**; monta a tabela de ponteiros das
   strings em `0x826A60E8` (ponteiro *i* = início + soma dos comprimentos anteriores).
6. Devolve 0 se leu tudo, 1 se falhou (então `main` mostra o erro fatal `0x8227D128`).

É exatamente o formato que `tools/m02/traducao.py` (`parse_x360`) já usava, e explica o rastro de
E/S da Missão 02 (o jogo lê o cabeçalho e só o bloco 0).

### 11.2 Escolha do idioma — `0x825B1FA8` → `0x8227D2D0` → `0x8227D9F0`

| Passo | Função | Regra real |
|---|---|---|
| idioma do console | `0x825B1FA8` (§3) | `ExGetXConfigSetting(3, 9)`; 0 ou > 0xC → região (Japão → 2, outra `0x01xx` → 7, senão 1) |
| código interno | `0x8227D2D0` (ev_be5e5991a) | 2 (japonês) → **1**; 3 (alemão) → **3**; 4 (francês) → **2**; 5 (espanhol) → **5**; 6 (italiano) → **4**; **qualquer outro → 0** (`LZCOUNT(id−6)>>3 & 4` dá 4 só para id 6) |
| bloco do `X360_strings` | `main` `0x8227D9F0` (ev_7c92dad0f) | interno 1 → bloco **5** (e chama `0x8226B4D0`, que grava 1 em `0x82697E68`); 2 → **1**; 3 → **2**; 4 → **4**; 5 → **3**; 0 → **0** |

Resultado (XLanguage → bloco): 1 inglês → 0 · 2 japonês → 5 · 3 alemão → 2 · 4 francês → 1 ·
5 espanhol → 3 · 6 italiano → 4 · **7 a 12 → 0**. Ordem dos blocos no arquivo: 0 inglês (aqui
**PT-BR**), 1 francês, 2 alemão, 3 espanhol, 4 italiano, 5 japonês — a mesma que o verificador
tinha tirado só dos dados. Confere com os dois experimentos da Missão 02 (`--user_language 7` → bloco
0; `--user_language 3` → alemão).

No port, o idioma vem da opção `user_language` do runtime (padrão **1**,
`rexglue-sdk/src/kernel/xam/xam_user.cpp:31`), que `ExGetXConfigSetting(3, 9)` devolve ao jogo
(`xboxkrnl_xconfig.cpp:64`). O runtime lê opções de `banjotooie.toml` ao lado do executável
(`rex_app.cpp:112`) e o painel de ajustes do runtime grava nesse arquivo.

### 11.3 Idioma dos diálogos — variável do jogo `0xC0D` (3 bits)

- `0x8226B4B0` = `ler(0xC0D, 3)` (salta para `0x822698A8`); `0x8226B4C0` = `gravar(0xC0D, valor, 3)`
  (salta para `0x82269E00`). Conferido em assembly: `li r4,3; li r3,0xC0D; b 0x822698A8` e
  `mr r4,r3; li r5,3; li r3,0xC0D; b 0x82269E00` (ev_4f3732029, ev_075026589).
- `0x82268E08` (corpo em `0x82268E10`, ev_1f1606fbe): com `param_1 == 0` grava em `0xC0D` o código
  interno de `0x8227D2D0`; senão chama `0x8226B528`.
- `0x8226B528` (ev_c2a6942b8): depois de tentar ler o jogo salvo, **sempre** regrava `0xC0D` com o
  código interno atual (`0x82269E00(0xC0D, idioma, 3)`). Ou seja, o idioma não fica preso no save:
  segue o idioma do console a cada carregamento.
- Cada caixa de texto guarda o idioma ao abrir: `0x82196390` (ev_5815fc0a1) copia `ler(0xC0D)` para
  os bits 0–8 de `objeto+0x6C`.
- Ajuste só para o alemão: `0x821C1BC8` (ev_ae96e2bca) chama `0x82242B88(0x14, 5, 2)` quando o
  código interno é 3.
- O código interno 0..5 (inglês/PT-BR, japonês, francês, alemão, italiano, espanhol) é a mesma ordem
  dos 6 espaços de idioma dos registros de diálogo do `db360` medida nos dados na Missão 02. A
  indexação do registro pelo espaço não foi lida instrução por instrução (**derivado**, não
  observado); a exibição do espaço 0 (PT-BR) no jogo foi vista nas capturas.

### 11.4 Banco de diálogos — `0x82220790` (ev_61961b02c)

Carrega `db360.cmp` (nome em `0x82002F80`) por `0x8227ED50`; guarda o buffer em `0x8269EE80` e a
tabela de recursos (buffer + 8) em `0x826CC034`; grava `0x400000` em `0x8266C698`. A exigência do
cabeçalho XCTD `0x0FF512ED` versão `0x01000000` (`0x825B9218`) vem da Missão 02 (Ghidra headless);
nesta sessão o Ghidra do Craft não montou função nesse endereço.

### 11.5 Saída do jogo e o que não foi achado no código

- As únicas chamadas a `XamLoaderTerminateTitle` (`0x825F34D4`) estão na rotina de entrada
  `0x825B3AF8`, em `0x825B3B44` e `0x825B3CB8` (varredura de `bl`). O caminho "SAIR DO JOGO → SIM"
  até elas **não foi seguido**; o teste nunca confirma a saída.
- O menu de pausa (START) e a mensagem "perfil conectado à Xbox LIVE" usam strings do
  `X360_strings.dat` por número, não por texto, então `native_strings_find` não os acha no XEX. O
  comportamento usado no teste (START só abre a pausa sem diálogo na tela e alterna abrir/fechar) é
  **medido** (`_audit/out/m02r2/exp1`–`exp6`), não lido no código.
- `0x8238FC48` também usa `0xC0D`, mas é o compilador de shaders embutido (lá `0xC0D` é um código
  de erro, ev_670ff372f): **não** tem relação com o idioma.

### 11.6 Onde isso é usado no projeto

- `tools/m02/traducao.py`: `parse_x360` segue §11.1; a verificação nova **L1** aplica a tabela do
  §11.2 ao `user_language` que o port entrega (o `banjotooie.toml` ao lado do exe, senão o padrão do
  runtime) e reprova se o jogo não for carregar o bloco 0 e o espaço 0 (PT-BR); também confere os
  rótulos de idioma do verificador contra o código.
- `tools/m02/teste_negativo.py`: casos `runtime_idioma_alemao` (`user_language = 3`) e
  `runtime_idioma_invalido` (`0`), que a L1 tem de acusar.
- `mechanics.json`: os mesmos valores, com a função de origem (o arquivo continua na **versão 1**,
  que é a que o Craft Anything lê nesta linha de base, rodada 2026-11-11; acrescenta o bloco `pc`
  com as constantes K-60 e os valores que o build consome — §12.5).

## Anexo: evidências da sessão Craft Anything

Leituras via `native_function_pseudocode` (IDs de evidência): `main`
ev_be29fe13d · validação de dados ev_8674f8240 · cadência de quadros ev_2d9ddba0e ·
inatividade ev_92d9a18b5 · apresentação ev_c73be928b · modo de vídeo ev_5652ba0fc ·
settings ev_70c485244/ev_ae09aefdd/ev_e7a18543e/ev_087d6be3a/ev_4b6338a84 ·
áudio ev_8b064c5b1/ev_6ad77faa8 · delay ev_06aab1ac6 · idioma ev_6e1ca1402 ·
erro fatal ev_94593f2b3 · entry ev_f14ee5973. Strings: ev_cc0c07e80 (PDB),
ev_7b837408e (mensagens), ev_07a046e4c (Brightness), ev_4186018c6 (Audio),
ev_664db0975/ev_f14454cb6 (XUI). Dumps locais: `_audit/out/ghidra_xex_out/*.txt`.

§11 (Missão 02 rodada 2): carregador `X360_strings` ev_4a9501f8e · leitura u16/u32
ev_d867a0f0c/ev_26f24e2fb · `main` ev_7c92dad0f · idioma interno ev_be5e5991a · variável `0xC0D`
ev_4f3732029/ev_075026589/ev_1f1606fbe/ev_c2a6942b8 · caixa de texto ev_5815fc0a1 · ajuste alemão
ev_ae96e2bca · `db360` ev_61961b02c · strings ev_27f1dcf10 · `GAME:\` ev_e228f6454 · compilador de
shader (falso positivo de `0xC0D`) ev_670ff372f.

---

## 12. Reabertura Deep de 2026-11-11 — o que foi relido e o que mudou (rev. 4)

Nesta rodada o `default.xex` da pessoa foi **reaberto nesta sessão** do Craft Anything e cada
função, constante e gancho citados nesta página foi conferido de novo, desta vez com pseudocódigo
do Ghidra e com os bytes de cada instrução de gancho. O passo a passo do que mudou no build está
no §12.4.

### 12.1 Como a sessão foi aberta

| Item | Valor |
|---|---|
| Arquivo aberto | `app/_audit/out/xex_image.bin` (imagem plana do default.xex, sha256 `2e70c5e9…`) |
| `core_target_open` | `format=raw-binary`, `architecture=x86_64` (obrigatório na API), `language_id=PowerPC:BE:64:A2ALT-32addr`, `entry_point=0x5B3AF8` |
| Alvo | `t_2e70c5e978fa` |
| Espaço do Ghidra | imagem em `0x0` → **endereço real = endereço Ghidra + 0x82000000** (pede-se as funções pelo endereço rebaseado, ex. `0x27D9F0` = main `0x8227D9F0`) |
| Espaço do motor embutido | uma seção única a partir do entry → **endereço embutido = entry (0x5B3AF8) + offset no arquivo** (é o espaço usado por `native_strings_find`/`native_data_show`/`native_bytes_show`) |

Detalhe observado: a análise do Ghidra nesta sessão não forma função "on demand" em alguns
endereços (`0x8226B4B0`, `0x8226B4C0`, `0x82268E08/0x82268E10`, `0x8226B528`, `0x825B81F0`,
`0x8223F6F0`, `0x82220790`), embora os bytes ali sejam PowerPC válidos (conferidos byte a
byte: `0x8226B4B0` = `li r4,3; li r3,0xC0D; b 0x822698A4`; `0x8226B4C0` = `mr r4,r3; li r5,3;
li r3,0xC0D; b 0x82269DF8`). As conclusões dessas funções continuam as das rodadas anteriores
(que tinham a varredura de instruções como fonte).

### 12.2 Correções desta rodada

1. **Global das texturas = `0x826AEE20`**, não `0x8269EE20` (ver §2, revisado; a instrução
   `stw r11,0xEE20(r10)` com `r10=0x826A0000` dá `0x826AEE20`; dois pontos de escrita, um
   gravando o valor inicial **1**). Os valores **2** (pacote `db360.textures.cmp` carregado) e
   **3** (falha) continuam certos: `cntlzw(ret & 0xFF) >> 5 + 2`.
2. **`0x8225D8A8` é `ps_add f13,f13,f29`** (`0xEDADE82A`), não `fadds` — o cronômetro da caixa
   de texto (`caixa+0x22C`) é um **par** de floats (paired-single); o gancho de velocidade dos
   diálogos multiplica o delta f29 e segue válido.
3. **Flags do `VdSetDisplayMode`**: `(info[0x11] & 3) << 0x1C` → bits **28–29** (as revs
   anteriores diziam 30–31), além da base `0x40000000` (§6, revisado).
4. **`.rdata` são f32 (4 bytes), não doubles** — corrigido §8, inclusive `0x82000730` =
   `0.99999994` (bytes `3F 7F FF EF`).

### 12.3 As 17 constantes que o "60 FPS" troca (tabela completa, verificada byte a byte)

Cada linha: endereço do **gancho** no XEX (= endereço no `pc_hooks.toml`), registrador do
gancho, constante **original** (f32 BE do `.rdata`, com endereço) e a constante de **60 FPS**
usada no port. Todas as 17 foram lidas do `.rdata` (f32 BE) e o `lfs` de cada uma foi conferido
na instrução exata do gancho. Evidências desta rodada: ev_6f49033b9/ev_eed7fccda/ev_aaf3aa2ab/
ev_17d3ad3e1 (funções) e as leituras de bytes de cada endereço.

| Gancho | reg | Original (f32) | → 60 FPS (f32) | Função dona |
|---|---|---|---|---|
| `0x822C8930` | f0 | **0.05** (`0x82000B48`) | 0.2 (`0x8200073C`) | região próxima a FUN_002c8560 (0x822C8560) |
| `0x821A22E4` | f5 | **3.0** (`0x82000800`) | 9.0 (`0x82001EDC`) | FUN_001a2288 (0x821A2288; câmera/mira; clamp 90.0) |
| `0x821A22E8` | f3 | **-0.01** (`0x82000924`) | -0.03 (`0x82004238`) | FUN_001a2288 |
| `0x821A22EC` | f2 | **-1.0** (`0x82000748`) | -3.0 (`0x82000B20`) | FUN_001a2288 |
| `0x821A2324` | f5 | **3.0** | 9.0 | FUN_001a2288 |
| `0x821A2328` | f3 | **0.01** (`0x82000778`) | 0.03 (`0x82000B30`) | FUN_001a2288 |
| `0x821A232C` | f2 | **1.0** (`0x82000738`) | 3.0 | FUN_001a2288 |
| `0x821A2358` | f0 | **90.0** (`0x820007C0`) | 100.0 (`0x82000774`) | FUN_001a2288 (clamp `min(x·0.98, 90)`) |
| `0x8213E2EC` | f5 | **3.0** | 11.0 (`0x82002060`) | FUN_0013e290 (0x8213E290; gêmea da anterior; clamp 200.0) |
| `0x8213E2F0` | f3 | **-0.01** | -0.03 | FUN_0013e290 |
| `0x8213E2F4` | f2 | **-1.0** | -3.90625 (`0x820028F0`) | FUN_0013e290 |
| `0x8213E32C` | f5 | **3.0** | 11.0 | FUN_0013e290 |
| `0x8213E330` | f3 | **0.01** | 0.03 | FUN_0013e290 |
| `0x8213E334` | f2 | **1.0** | 3.75 (`0x820013F4`) | FUN_0013e290 |
| `0x8213E340` | f0 | **-0.01** | -0.03 | FUN_0013e290 |
| `0x8213E360` | f0 | **200.0** (`0x820008F4`) | 220.0 (`0x82000B0C`) | FUN_0013e290 (clamp `min(x·0.98, 200)`) |
| `0x821129B8` | f0 | **0.028** (`0x82000974`) | 0.0142857 = 1/70 (`0x82001EF8`) | FUN_00112980 (fator por quadro = 0.028 × (1/dt)) |

### 12.4 Funções relidas nesta rodada (pseudocódigo Ghidra, com evidência)

| Função | Endereço | O que o pseudocódigo desta rodada confirma | Evidência |
|---|---|---|---|
| singleton de ajustes | `0x82565300` | campos +0x04..0x28 do `0x826B65AC` exatamente como no §4 (padrão `0xA8`, tipo 4, callback 0x82565720, guarda em 0x826B65DC) | ev_1c8ebd6fa0 |
| callback de ajuste | `0x82565720` | `objeto+0xA8 = valor`; `objeto+0xC \|= 2`; nulo/tipo≠4 → `0x80070057` | ev_94e32e7dd |
| aplicador | `0x82566240` | tabela de 8 sub-operações; id inválido → `0x80300001` | ev_a01dfc9d6 |
| despacho | `0x82565C00` | retransmite ao callback/sub-tabela; argumentos vazios → `0x80070057` | ev_510316554 |
| getter | `0x82565D68` | tipo divergente → `0x80300008`; tabela de leitura `uVar1-1 < 8` | ev_64c75d527 |
| singleton do configurador | `0x825769C8` | guarda `0x826B78B0`; singleton `0x826B789C` | ev_8e1bef0fc |
| resolvedor de objeto | `0x82562068` | hash em `0x826B61D8`, retorno = `item+0x20`, erro `0x8030000A` | ev_d369dbd85 |
| diálogo de erro fatal | `0x8227D128` | tabela 6 idiomas (§3 exata), `while (result==5)` com espera `0xFA`, tag `0x82003534`, halt | ev_fcfe01e1d6 |
| idioma (console) | `0x825B1FA8` | `ExGetXConfigSetting(3,9)`; 0 ou >0xC → região (0x101→2, 0x01xx→7, senão 1) | ev_60c4b6824 |
| código interno | `0x8227D2D0` | 2→1, 3→3, 4→2, 5→5, `LZCOUNT(id−6)>>3&4` (6→4, resto→0) | ev_8833112d4d |
| main | `0x8227D9F0` | cadeia §2 completa; interno 1→bloco 5 (+`0x8226B4D0`), 2→1, 3→2, 4→4, 5→3, resto→0 | ev_62ecdf16c |
| GetString | `0x822E9C28` | `id < count(0x826A60F0) → tabela(0x826A60E8)+4·id`, senão 0 | ev_79cdb272e |
| ação CLASSIFICAÇÕES | `0x822DEC40` | `tela+0x220` 1→0; sem perfil (`0x8269EF20`) → tela `0x822DE7E8`; entrada = `tela+0x154/page·0x2C/cursor·0x88`, alvo em `+0x28` | ev_4df5cfde3 |
| despacho do botão A | `0x822E2860` | ação da entrada chamada por `(+0x7C)(tela)`; regras de cursor das páginas | ev_f3b70f75e |
| leitores do X360_strings | `0x822E9AE0/0x822E9B40` | u16 = `b1<<8\|b0`, u32 = concatenado byte a byte (little-endian) | ev_af205c64f/ev_9f8aeba21 |
| lista de texturas soltas | `0x822D3980` | `"%s%s"`(0x82003E00)+pasta+`RAWFiles\db360_TexFileList.txt`(0x82005898); formato `"%s %d"`(0x8200588C); registros de 0x48 em `0x826A4F34`; 1=ok/0=falha | ev_213c4bab7 |
| texturas (decisão) | `0x8227D0C0` | `0x826AEE20` = `cntlzw(ret&0xFF)>>5+2`; §2 revisado | ev_034efdbf5 |
| modo de vídeo | `0x823021A8` | flags exatas (§6 revisado); display atual → `global+0xA8/0xAC/0xB0` | ev_30f5711b8 |
| apresentação | `0x822FA358` | contador de quadros `+0x40B0`; `VdSwap`; formatos `0x32→3`, `7→0x1B` | ev_3f8cd843f |
| agendador vsync | `0x822F9CB8` | fórmula §6 exata, refresh padrão `0x3C`, anel de 16 em `+0x40BC/0x40C0`, callback `+0x409C`, tabelas por núcleo `0x820005CC/0x820005D0` | ev_ddd22d4b7 |
| alcance de desenho | `0x82240DE8` | `(FUN_0023fb98() & 0x3FFFFFFF) << 2`; gancho em `0x82240DF8` = `slwi r3,r3,2` (`0x5463103A`) | ev_9a276abed |
| filtros de áudio | `0x8250C6D8` | razão × 2π + equação quadrática exatas (§5) | ev_2d71aeb2b |
| parâmetros de voz | `0x8250D050` | `×48.0`, clamp `0xFFF`, 4 campos; demais fórmulas intactas | ev_86e6f708f |
| inatividade | `0x825B36A0` | `XamReset/XamEnableInactivityProcessing`; erro próprio `0x65B` fora da faixa `0x00070000` | ev_488868736 |
| estado de cena | `0x82224208` | `return DAT_8266ef30 != 0` (byte lido pela camada de PC) | ev_f01d91d9d |
| passo de quadro | `0x82276E34` | `addis r11,0,0x826A; addi r11,-0x4E40; li r9,2; lwz r10,0(r11); sth r9,-4(r11)` → halfword `0x8269B1BC` = **2** (bytes conferidos) | ev_604a2dd92 + bytes |
| intervalo de apresentação | `0x822F9F18` | `lwz r11,0x351C(r31)` (bytes conferidos) | bytes |
| menu de pausa | `0x822DF11C/0x822DF120` | chamada compartilhada + `lwz r11,0x154(r30)` (gancho do port) | bytes |

### 12.5 O build agora usa estes valores

- `app/mechanics.json` (na versão 1, a lida pelo Craft Anything) ganhou o bloco `pc` com os
  valores do quadro (passo de quadro 2, endereços de gancho, estado de cena `0x8266EF30`, mapa
  `0x82673720`) e as **17 constantes K-60** (original → 60 FPS, com endereços do `.rdata`).
- `tools/gen_mechanics_header.py` lê o `mechanics.json`, **confere os endereços com o
  `pc_hooks.toml`** (para o build se parar a tabela de ganchos divergir do decompilado, em vez
  de ligar algo pela metade) e gera `banjotooie-recomp/src/pc/mechanics_values.h`
  (não versionado, regerado a cada build).
- `src/pc/pc_hooks.cpp` passou a ler o passo de quadro original, o estado de cena, o id do mapa
  e as 17 constantes de 60 FPS **desse cabeçalho** em vez de literais espalhados; comportamento
  idêntico (mesmos valores), origem agora rastreável.
- `tools/build_native.cmd` (entry do `npm run build`) e a fase `game` de `tools/build_port.cmd`
  rodam o gerador antes de compilar; falha do gerador = falha do build.
- Correções da rodada também registradas em `mechanics.json` (global `0x826AEE20`, `ps_add` da
  caixa de diálogo, fórmula do alcance e do vblank).
