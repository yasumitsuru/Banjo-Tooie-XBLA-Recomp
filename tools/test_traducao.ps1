# MISSAO 02 - teste automatico de integridade da traducao PT-BR (sem acoes manuais).
#
# O que faz (cada passo grava evidencia em -OutDir; nada e escrito na pasta do jogo):
#   1. inspecao estatica dos arquivos traduzidos + regressao contra a linha de base
#      (tools\m02\traducao.py; inclui o sha256 dos 36 arquivos do jogo ANTES)
#   2. jogo real, tela de abertura, com rastro de E/S do runtime, dump do db360 e da memoria onde
#      o jogo guarda os textos do menu  -> inspecao completa (o que o jogo leu e tem na memoria)
#   3. jogo real com controle virtual (--port_test_input): abertura -> START -> menu principal ->
#      AJUDA E OPCOES -> CONFIGURACOES  (capturas)
#   4. jogo real: menu -> JOGAR -> selecao de jogo -> introducao com dialogo do jogo (capturas)
#   4b. jogo real, parte jogavel: pula a introducao (B), abre a pausa (START) e percorre VER TOTAIS,
#      AJUDA E OPCOES, a mensagem do sistema de CLASSIFICACOES e a confirmacao de SAIR DO JOGO (sem
#      confirmar); fecha a pausa e captura a fala do Bottles com o icone de botao (rodada 2)
#   5. regressao visual: o texto de cada captura comparado com as referencias (comparar_capturas.py)
#   6. sha256 dos 36 arquivos do jogo DEPOIS (tem de ser igual ao antes)
# A janela abre atras das outras sem roubar o foco (modo automatico do port).
#
# Uso: pwsh -File tools\test_traducao.ps1 [-GameDir <pasta>] [-OutDir <pasta>] [-Exe <banjotooie.exe>]
#        [-Referencias <pasta de PNGs>] [-GravarReferencias] [-GravarReferenciasNovas] [-GravarLinhaDeBase]
#   -GravarReferenciasNovas grava so as referencias que ainda nao existem (as conferidas ficam como estao).
# Saida: resultado.json em -OutDir; codigo 0 se nada falhou (PENDENTE nao reprova), 1 se falhou.
param(
  [string]$GameDir = (Join-Path $PSScriptRoot "..\..\Banjo Tooie BR"),
  [string]$OutDir = (Join-Path $PSScriptRoot ("..\_audit\out\m02\teste-" + (Get-Date -Format "yyyyMMdd-HHmmss"))),
  [string]$Exe = (Join-Path $PSScriptRoot "..\out\native\banjotooie.exe"),
  [string]$Referencias = (Join-Path $PSScriptRoot "..\_audit\out\m02\referencias"),
  [string]$LinhaDeBase = (Join-Path $PSScriptRoot "..\docs\evidencias\m02\linha_de_base_traducao.json"),
  [switch]$GravarReferencias,
  [switch]$GravarReferenciasNovas,
  [switch]$GravarLinhaDeBase,
  [double]$IouMinimo = 0.75,
  # rodada 2: tambem o IoU de cada linha de texto da referencia (item de menu ou fim de frase sumido)
  [double]$IouLinhaMinimo = 0.75
)
$ErrorActionPreference = "Stop"
$App = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$GameDir = (Resolve-Path $GameDir).Path
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Py = Join-Path $App "tools\m02\traducao.py"
$Cmp = Join-Path $App "tools\m02\comparar_capturas.py"
$results = [ordered]@{ versao = 1; inicio = (Get-Date).ToString("s"); exe = $Exe; jogo = $GameDir;
  exe_sha256 = (Get-FileHash $Exe -Algorithm SHA256).Hash.ToLower(); passos = @() }

function Add-Step($name, $status, $detail) {
  $script:results.passos += [ordered]@{ passo = $name; status = $status; detalhe = $detail }
  Write-Host ("[{0}] {1}: {2}" -f $status, $name, $detail)
}
function Quote([string]$s) { '"' + $s + '"' }
function Run-Port([string]$dir, [string[]]$extra, [int]$timeoutS) {
  New-Item -ItemType Directory -Force $dir | Out-Null
  $argv = @("--game_data_root", (Quote $GameDir), "--user_data_root", (Quote "$dir\user"),
    "--cache_path", (Quote "$dir\cache"), "--audio_mute", "--boot_report", (Quote "$dir\boot.json")) + $extra
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $p = Start-Process -FilePath $Exe -ArgumentList $argv -PassThru
  if (-not $p.WaitForExit($timeoutS * 1000)) { Stop-Process -Id $p.Id -Force; return @{ code = -999; s = $sw.Elapsed.TotalSeconds } }
  return @{ code = $p.ExitCode; s = $sw.Elapsed.TotalSeconds }
}
function Boot($dir) { if (Test-Path "$dir\boot.json") { Get-Content "$dir\boot.json" -Raw | ConvertFrom-Json } else { $null } }
function Inspect([string]$dir, [string[]]$extra) {
  # --exe: a verificacao L1 le o banjotooie.toml ao lado do exe (idioma que o port entrega ao jogo)
  $argv = @("-I", $Py, "--jogo", $GameDir, "--saida", $dir, "--exe", $Exe) + $extra
  $out = & python @argv 2>&1
  $out | Set-Content -Path "$dir.log" -Encoding utf8
  return (Get-Content (Join-Path $dir "relatorio_traducao.json") -Raw | ConvertFrom-Json)
}
function Checks-Summary($rep) {
  $c = $rep.verificacoes
  "{0} PASS, {1} FAIL, {2} PENDENTE{3}" -f (@($c | Where-Object status -eq "PASS").Count),
    (@($c | Where-Object status -eq "FAIL").Count), (@($c | Where-Object status -eq "PENDENTE").Count),
    $(if (@($c | Where-Object status -eq "FAIL").Count) { " (" + ((@($c | Where-Object status -eq "FAIL") | ForEach-Object { $_.id }) -join ",") + ")" } else { "" })
}

# 1. inspecao estatica + regressao (antes de rodar o jogo)
$baseArgs = @()
if ((Test-Path $LinhaDeBase) -and -not $GravarLinhaDeBase) { $baseArgs = @("--linha-de-base", $LinhaDeBase) }
$rep1 = Inspect (Join-Path $OutDir "1_estatico") $baseArgs
$st1 = if (@($rep1.verificacoes | Where-Object status -eq "FAIL").Count) { "FAIL" } else { "PASS" }
Add-Step "1_inspecao_estatica_antes" $st1 (Checks-Summary $rep1)

# 2. abertura com rastro de E/S e dumps da memoria do jogo
$d2 = Join-Path $OutDir "2_rastro"
# --log_max_file_size_mb: com a camada de PC (60 FPS/4K) o rastro passa dos 5 MB padrao em ~25 s e
# o runtime gira o arquivo (log.1.txt fica com o inicio, onde o jogo le os arquivos traduzidos);
# um log so, grande, mantem as leituras em log.txt para o inspetor (MISSAO 00 rodada 8, 2026-11-11).
$r = Run-Port $d2 @("--log_noisy", "--log_level", "trace", "--log_file", (Quote "$d2\log.txt"),
  "--log_max_file_size_mb", "1024",
  "--port_dump_db", (Quote "$d2\db_mem.bin"), "--port_dump_ranges", (Quote "0x40000000:0x200000=$d2\heap_40000000.bin"),
  "--screenshot", (Quote "$d2\titulo.png"), "--screenshot_seconds", "28", "--smoke_timeout_seconds", "90") 150
$b = Boot $d2
$ok2 = $r.code -eq 0 -and $b -and $b.screenshot.ok -and (Test-Path "$d2\db_mem.bin") -and (Test-Path "$d2\heap_40000000.bin")
Add-Step "2_jogo_abertura_com_rastro" $(if ($ok2) { "PASS" } else { "FAIL" }) ("codigo {0}; {1:N0} s; captura {2}" -f $r.code, $r.s, $(if ($b) { $b.screenshot.detail } else { "sem relatorio" }))
if ($ok2) {
  $inspArgs = @("--db", "$d2\db_mem.bin", "--heap", "$d2\heap_40000000.bin", "--rastro", "$d2\log.txt")
  if ($GravarLinhaDeBase) {
    New-Item -ItemType Directory -Force (Split-Path $LinhaDeBase) | Out-Null
    $inspArgs += @("--gravar-linha-de-base", $LinhaDeBase)
  } elseif (Test-Path $LinhaDeBase) { $inspArgs += @("--linha-de-base", $LinhaDeBase) }
  $rep2 = Inspect (Join-Path $OutDir "2_inspecao_completa") $inspArgs
  $st2 = if (@($rep2.verificacoes | Where-Object status -eq "FAIL").Count) { "FAIL" } else { "PASS" }
  Add-Step "2_inspecao_com_memoria_e_rastro" $st2 (Checks-Summary $rep2)
} else { Add-Step "2_inspecao_com_memoria_e_rastro" "PENDENTE" "sem dumps (passo 2 falhou)" }

# 3. menus XBLA com controle virtual
$d3 = Join-Path $OutDir "3_menus"
# Menu principal da camada de PC (2026-11-11): CLASSIFICACOES saiu, entao AJUDA E OPCOES e a 3a
# entrada (2 para baixo); antes eram 3 toques.
$s3 = "start@26,down@31,down@32.5,a@36,down@41,down@42.5,a@44"
$r = Run-Port $d3 @("--port_test_input", $s3, "--port_test_shots", (Quote "30=$d3\menu_principal.png;40=$d3\ajuda_e_opcoes.png"),
  "--screenshot", (Quote "$d3\configuracoes.png"), "--screenshot_seconds", "48", "--smoke_timeout_seconds", "100") 160
$b = Boot $d3
$ti = if ($b) { ($b.stages | Where-Object name -eq "test_input" | Select-Object -Last 1).detail } else { "" }
$ok3 = $r.code -eq 0 -and $ti -and $ti -notmatch "NAO_ENTREGUE" -and (Test-Path "$d3\menu_principal.png") -and (Test-Path "$d3\ajuda_e_opcoes.png") -and (Test-Path "$d3\configuracoes.png")
Add-Step "3_menus_com_controle_virtual" $(if ($ok3) { "PASS" } else { "FAIL" }) ("codigo {0}; {1}" -f $r.code, $ti)

# 4. selecao de jogo e introducao (dialogo do db360)
# Tempos medidos em 2 calibracoes (2026-11-10, _audit\out\m02\calib4 e calib4b, A em 36 s): o texto 1
# fica na tela de 53 a 57 s e o texto 2 de 65 a 69 s. Tres capturas por texto, 1 s de distancia; a
# comparacao usa a melhor delas (o texto tem de aparecer em pelo menos uma).
$d4 = Join-Path $OutDir "4_dialogo"
$s4 = "start@26,a@30,a@36"
$sh4 = "34=$d4\selecao_de_jogo.png;54.5=$d4\dialogo_intro_1a.png;55.5=$d4\dialogo_intro_1b.png;56.5=$d4\dialogo_intro_1c.png;" +
  "66.5=$d4\dialogo_intro_2a.png;67.5=$d4\dialogo_intro_2b.png"
$r = Run-Port $d4 @("--port_test_input", $s4, "--port_test_shots", (Quote $sh4),
  "--screenshot", (Quote "$d4\dialogo_intro_2c.png"), "--screenshot_seconds", "68.5", "--smoke_timeout_seconds", "110") 170
$b = Boot $d4
$ti = if ($b) { ($b.stages | Where-Object name -eq "test_input" | Select-Object -Last 1).detail } else { "" }
$ok4 = $r.code -eq 0 -and $ti -and $ti -notmatch "NAO_ENTREGUE" -and (Test-Path "$d4\selecao_de_jogo.png") -and (Test-Path "$d4\dialogo_intro_2c.png")
Add-Step "4_selecao_e_dialogo_com_controle_virtual" $(if ($ok4) { "PASS" } else { "FAIL" }) ("codigo {0}; {1}" -f $r.code, $ti)

# 4b. parte jogavel: pausa, submenus, mensagem do sistema e fala do Bottles (rodada 2, 2026-11-10).
# Calibracao em _audit\out\m02r2 (exp2-exp6): B em 58 s pula a introducao e o jogo comeca em Spiral
# Mountain por volta de 62 s; o START so e aceito sem dialogo na tela e alterna abrir/fechar, por isso
# ele vai em 64 s (antes da primeira fala do Bottles, ~66 s). Com a pausa aberta o jogo para e os
# tempos dos submenus ficam fixos. Depois do START em 116 s a pausa fecha e o Bottles fala de novo;
# o texto dele rola enquanto e escrito, entao ha 21 capturas (a cada 0,25 s) e a comparacao usa a melhor.
$d4b = Join-Path $OutDir "4b_jogavel"
$s4b = "start@26,a@30,a@36,b@58,start@64,down@69,a@71,b@78,down@81,a@83,b@89,down@92,a@94,b@100," +
  "down@103,down@105,a@107,b@112,start@116"
$inv = [cultureinfo]::InvariantCulture
$sh4b = @("66.5=$d4b\pausa_a.png", "67.25=$d4b\pausa_b.png", "68=$d4b\pausa_c.png",
  "74.5=$d4b\ver_totais_a.png", "75.5=$d4b\ver_totais_b.png", "76.5=$d4b\ver_totais_c.png",
  "86=$d4b\ajuda_no_jogo_a.png", "87=$d4b\ajuda_no_jogo_b.png", "88=$d4b\ajuda_no_jogo_c.png",
  "97=$d4b\xbox_live_a.png", "98=$d4b\xbox_live_b.png", "99=$d4b\xbox_live_c.png",
  "109.75=$d4b\sair_do_jogo_a.png", "110.5=$d4b\sair_do_jogo_b.png", "111.25=$d4b\sair_do_jogo_c.png")
$bottles = @()
for ($i = 0; $i -le 20; $i++) {
  $t = 125.5 + 0.25 * $i
  $f = [string]::Format($inv, "{0}\dialogo_bottles_{1:00}.png", $d4b, $i)
  $sh4b += [string]::Format($inv, "{0}={1}", $t, $f); $bottles += $f
}
$r = Run-Port $d4b @("--port_test_input", $s4b, "--port_test_shots", (Quote ($sh4b -join ";")),
  "--screenshot", (Quote "$d4b\final.png"), "--screenshot_seconds", "131.5", "--smoke_timeout_seconds", "170") 230
$b = Boot $d4b
$ti = if ($b) { ($b.stages | Where-Object name -eq "test_input" | Select-Object -Last 1).detail } else { "" }
$shotsOk = @($sh4b | Where-Object { -not (Test-Path ($_.Substring($_.IndexOf("=") + 1))) }).Count -eq 0
# Encerramento: o port pede para sair depois da captura final. Se o processo nao terminar sozinho
# ate o limite, o teste o encerra (codigo -999). As capturas ja estao gravadas nesse caso, entao o
# passo e julgado por elas, e o travamento vira um AVISO separado (defeito do runtime, fora do texto).
$saiu = $r.code -eq 0
$pediuSaida = $b -and $b.exit -and $b.exit.code -eq 0 -and @($b.stages | Where-Object name -eq "shutdown").Count -gt 0
$ok4b = ($saiu -or ($r.code -eq -999 -and $pediuSaida)) -and $ti -and $ti -notmatch "NAO_ENTREGUE" -and $shotsOk
Add-Step "4b_jogavel_pausa_sistema_e_dialogo" $(if ($ok4b) { "PASS" } else { "FAIL" }) ("codigo {0}; capturas {1}; {2}" -f $r.code, $(if ($shotsOk) { "todas gravadas" } else { "FALTANDO" }), $ti)
if ($r.code -eq -999) {
  Add-Step "4b_encerramento_do_processo" "AVISO" ("o jogo pediu para sair (shutdown={0}) mas o processo nao terminou em {1:N0} s e foi encerrado pelo teste" -f $pediuSaida, $r.s)
}

# 5. regressao visual do texto
$shots = @(
  @{ n = "titulo"; f = "$d2\titulo.png"; reg = "420,460,860,492"; cor = "branco"; txt = "PRESSIONE START PARA JOGAR" },
  @{ n = "titulo_copyright"; f = "$d2\titulo.png"; reg = "320,555,900,620"; cor = "branco"; txt = "(C) 2009 MICROSOFT - JOAOHRS2. / TODOS OS DIREITOS RESERVADOS." },
  # Menu principal pedido pela pessoa (2026-11-11): referencia nova, gravada do proprio port com
  # -GravarReferenciasNovas (a antiga menu_principal.png, do menu original, fica como registro).
  @{ n = "menu_principal_recomp"; f = "$d3\menu_principal.png"; reg = "165,180,700,560"; cor = "claro"; txt = "JOGAR / CONQUISTAS / AJUDA E OPCOES / CONFIGURACAO DA RECOMPILACAO / SAIR PARA O DESKTOP" },
  @{ n = "ajuda_e_opcoes"; f = "$d3\ajuda_e_opcoes.png"; reg = "165,180,700,470"; cor = "claro"; txt = "COMO JOGAR / CONTROLES / CONFIGURACOES / CREDITOS" },
  @{ n = "configuracoes"; f = "$d3\configuracoes.png"; reg = "165,180,700,470"; cor = "claro"; txt = "VOLUME DA MUSICA / VOLUME DOS EFEITOS SONOROS / RESTAURAR CONFIGURACOES PADRAO / INVERTER EIXO Y : NAO" },
  @{ n = "selecao_de_jogo"; f = "$d4\selecao_de_jogo.png"; reg = "190,45,1010,125"; cor = "branco"; txt = "PRESSIONE (A) PARA INICIAR O JOGO." },
  @{ n = "dialogo_intro_1"; f = @("$d4\dialogo_intro_1b.png", "$d4\dialogo_intro_1a.png", "$d4\dialogo_intro_1c.png"); reg = "230,130,1030,500"; cor = "laranja"; txt = "DOIS ANOS TEM SE PASSADO DESDE QUE GRUNTILDA A BRUXA FOI DERROTADA..." },
  @{ n = "dialogo_intro_2"; f = @("$d4\dialogo_intro_2b.png", "$d4\dialogo_intro_2a.png", "$d4\dialogo_intro_2c.png"); reg = "230,130,1030,500"; cor = "laranja"; txt = "APOS CAIR DO TOPO DE SUA TORRE ELA FOI ENTERRADA..." },
  # rodada 2: parte jogavel (regioes sem o Bottles, que brilha em branco em x 690-810)
  @{ n = "pausa"; f = @("$d4b\pausa_b.png", "$d4b\pausa_a.png", "$d4b\pausa_c.png"); reg = "290,165,600,545"; cor = "cinza"; txt = "CONTINUAR JOGO / VER TOTAIS / AJUDA E OPCOES / CLASSIFICACOES / CONQUISTAS / SAIR DO JOGO" },
  @{ n = "pausa_ver_totais"; f = @("$d4b\ver_totais_b.png", "$d4b\ver_totais_a.png", "$d4b\ver_totais_c.png"); reg = "290,165,660,300"; cor = "cinza"; txt = "OBJETOS E ITEMS / VER TOTAIS" },
  @{ n = "pausa_ajuda_e_opcoes"; f = @("$d4b\ajuda_no_jogo_b.png", "$d4b\ajuda_no_jogo_a.png", "$d4b\ajuda_no_jogo_c.png"); reg = "290,165,560,345"; cor = "cinza"; txt = "COMO JOGAR / CONTROLES / CONFIGURACOES" },
  @{ n = "mensagem_xbox_live"; f = @("$d4b\xbox_live_b.png", "$d4b\xbox_live_a.png", "$d4b\xbox_live_c.png"); reg = "190,230,680,305"; cor = "cinza"; txt = "ESTE RECURSO EXIGE UM P... / JOGADOR CONECTADO A (ate x=680)" },
  # o fim das duas linhas da mensagem fica a direita do Bottles (x 820-890, estavel entre execucoes);
  # mesma referencia da tela acima (refn). So "ERFIL" e "XBOX L", atras do Bottles, ficam de fora.
  @{ n = "mensagem_xbox_live_fim"; refn = "mensagem_xbox_live"; f = @("$d4b\xbox_live_b.png", "$d4b\xbox_live_a.png", "$d4b\xbox_live_c.png"); reg = "820,230,890,305"; cor = "cinza"; txt = "...DE / ...IVE (fim das duas linhas)" },
  @{ n = "pausa_sair_do_jogo"; f = @("$d4b\sair_do_jogo_b.png", "$d4b\sair_do_jogo_a.png", "$d4b\sair_do_jogo_c.png"); reg = "190,150,620,260"; cor = "cinza"; txt = "SAIR DO JOGO / VOCE TEM CERTEZA?" },
  # o texto rola: a referencia e a captura do meio da janela (128,0 s); a comparacao usa as 21
  @{ n = "dialogo_bottles"; f = $bottles; fref = @($bottles[10], $bottles[9], $bottles[11]); reg = "280,555,1040,645"; cor = "branco"; txt = "TERRA. PRESSIONE (B) PARA INTERAGIR COM ... (fala do Bottles no jogo)" }
)
# Uma regiao sem texto nas duas imagens daria IoU 1,0 (0/0): por isso cada lado tem de ter pelo
# menos $PixelsMinimos pixels da cor do texto; senao a tela conta como FAIL (texto sumiu).
$PixelsMinimos = 300
# Tamanho das capturas na comparacao (MISSAO 00 rodada 8, 2026-11-11): as referencias e as regioes
# acima foram medidas em 1280x720 (a imagem do jogo). A camada de PC escolhe a saida pelo monitor
# (1080p/1440p/4K, mesma proporcao), entao uma captura maior com a mesma proporcao e reduzida para
# 1280x720 numa copia em 5_normalizadas (a original fica como esta) antes de comparar. Proporcao
# diferente nao e reduzida, e a comparacao continua julgando a captura como ela e.
$RefW = 1280; $RefH = 720
Add-Type -AssemblyName System.Drawing
$normDir = Join-Path $OutDir "5_normalizadas"
function Normalize-Shot([string]$path) {
  $img = [Drawing.Image]::FromFile($path)
  try {
    if ($img.Width -eq $RefW -and $img.Height -eq $RefH) { return $path }
    if ([math]::Abs($img.Width * $RefH - $img.Height * $RefW) -gt $img.Height) { return $path }
    New-Item -ItemType Directory -Force $normDir | Out-Null
    $out = Join-Path $normDir ((Split-Path (Split-Path $path) -Leaf) + "_" + (Split-Path $path -Leaf))
    if (Test-Path $out) { return $out }
    $bmp = New-Object Drawing.Bitmap $RefW, $RefH
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBilinear
    $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($img, 0, 0, $RefW, $RefH)
    $bmp.Save($out, [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    return $out
  } finally { $img.Dispose() }
}
if ($GravarReferencias -or $GravarReferenciasNovas) { New-Item -ItemType Directory -Force $Referencias | Out-Null }
$vis = @()
foreach ($s in $shots) {
  # refn: outra regiao da mesma tela usa a referencia ja gravada (e conferida) daquela tela
  $ref = Join-Path $Referencias ($(if ($s.refn) { $s.refn } else { $s.n }) + ".png")
  $cands = @($s.f | Where-Object { Test-Path $_ } | ForEach-Object { Normalize-Shot $_ })
  if (-not $cands.Count) { $vis += [ordered]@{ tela = $s.n; status = "FAIL"; detalhe = "captura ausente" }; continue }
  if (-not $s.refn -and ($GravarReferencias -or ($GravarReferenciasNovas -and -not (Test-Path $ref)))) {
    # referencia = a candidata com mais pixels de texto (a primeira, a do meio da janela, no empate);
    # com fref, so entre as candidatas do meio (texto que rola enquanto e escrito)
    $refCands = if ($s.fref) { @($s.fref | Where-Object { Test-Path $_ } | ForEach-Object { Normalize-Shot $_ }) } else { $cands }
    $best = $null; $bestPx = -1
    foreach ($c in $refCands) {
      $j = (& python -I $Cmp $c $c $s.reg $s.cor) | ConvertFrom-Json
      if ($j.pixels_ref -gt $bestPx) { $best = $c; $bestPx = $j.pixels_ref }
    }
    Copy-Item $best $ref -Force
    $st = if ($bestPx -ge $PixelsMinimos) { "REFERENCIA_GRAVADA" } else { "FAIL" }
    $vis += [ordered]@{ tela = $s.n; status = $st; detalhe = ("{0} ({1} pixels de texto); conferir a leitura: {2}" -f (Split-Path $best -Leaf), $bestPx, $s.txt) }
    continue
  }
  if (-not (Test-Path $ref)) { $vis += [ordered]@{ tela = $s.n; status = "PENDENTE"; detalhe = "sem referencia (rode com -GravarReferencias)" }; continue }
  $best = $null
  foreach ($c in $cands) {
    $j = (& python -I $Cmp $ref $c $s.reg $s.cor) | ConvertFrom-Json
    if (-not $best -or $j.iou -gt $best.j.iou) { $best = @{ j = $j; f = $c } }
  }
  $j = $best.j
  # por linha: so nas telas paradas (no texto que rola, as linhas mudam de lugar entre capturas)
  $porLinha = $s.n -ne "dialogo_bottles"
  $linhaOk = (-not $porLinha) -or ($null -ne $j.iou_linha_min -and $j.iou_linha_min -ge $IouLinhaMinimo)
  $st = if ($j.iou -ge $IouMinimo -and $linhaOk -and $j.pixels_ref -ge $PixelsMinimos -and $j.pixels_novo -ge $PixelsMinimos) { "PASS" } else { "FAIL" }
  $linhaTxt = if ($porLinha) { "pior linha {0} de {1} (minimo {2})" -f $j.iou_linha_min, @($j.linhas).Count, $IouLinhaMinimo } else { "por linha nao se aplica (texto rolando)" }
  $vis += [ordered]@{ tela = $s.n; status = $st; detalhe = ("IoU do texto {0} (minimo {1}); {2}; pixels ref/novo {3}/{4} (minimo {5}); captura {6}; esperado: {7}" -f $j.iou, $IouMinimo, $linhaTxt, $j.pixels_ref, $j.pixels_novo, $PixelsMinimos, (Split-Path $best.f -Leaf), $s.txt) }
}
$results.regressao_visual = $vis
$vf = @($vis | Where-Object status -eq "FAIL").Count
$vp = @($vis | Where-Object status -eq "PASS").Count
$vstatus = if ($vf) { "FAIL" } elseif ($vp -eq $vis.Count) { "PASS" } else { "PENDENTE" }
Add-Step "5_regressao_visual_do_texto" $vstatus (($vis | ForEach-Object { "{0}={1}" -f $_.tela, $_.status }) -join "; ")

# 6. arquivos do jogo depois (os mesmos sha256 do passo 1)
$null = Inspect (Join-Path $OutDir "6_estatico_depois") @()
$ha = Get-Content (Join-Path $OutDir "1_estatico\hashes.json") -Raw | ConvertFrom-Json
$hd = Get-Content (Join-Path $OutDir "6_estatico_depois\hashes.json") -Raw | ConvertFrom-Json
$names = @($ha.PSObject.Properties.Name) + @($hd.PSObject.Properties.Name) | Sort-Object -Unique
$diff = @($names | Where-Object { -not $ha.$_ -or -not $hd.$_ -or $ha.$_.sha256 -ne $hd.$_.sha256 })
Add-Step "6_arquivos_do_jogo_intactos" $(if ($diff.Count -eq 0 -and $names.Count -gt 0) { "PASS" } else { "FAIL" }) ("{0}/{1} arquivos com o mesmo sha256 antes e depois{2}" -f ($names.Count - $diff.Count), $names.Count, $(if ($diff.Count) { "; diferentes: " + ($diff -join ", ") } else { "" }))

$results.fim = (Get-Date).ToString("s")
$fails = @($results.passos | Where-Object status -eq "FAIL").Count
$results.status = if ($fails) { "FAIL" } else { "PASS" }
$results | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $OutDir "resultado.json") -Encoding utf8
Write-Host ("RESULTADO: {0} ({1})" -f $results.status, $OutDir)
if ($fails) { exit 1 }
