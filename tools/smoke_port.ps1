# MISSAO 01 - teste de fumaca automatizado do port nativo (sem acoes manuais).
#
# Passos (cada um grava evidencia em -OutDir):
#   0. sha256 dos arquivos do jogo ANTES (verify_baseline.py)
#   1. preflight na pasta real                  -> espera codigo 0
#   2. preflight numa pasta de teste sem RAWFiles\db360.cmp (arquivos de teste, nao do jogo)
#                                               -> espera codigo 2 e o arquivo citado no relatorio
#   3. sem --game_data_root                     -> espera codigo 3
#   4. jogo real: roda ate a captura de tela (padrao 75 s apos iniciar), grava PNG, relatorio e log
#                                               -> espera codigo 0, quadros > 0 e PNG nao vazio
#   5. falha provocada (--port_test_crash)      -> espera codigo 0xE0425442, relatorio e minidump
#   6. sha256 dos arquivos do jogo DEPOIS       -> espera 36/36 iguais
# A janela abre atras das outras e devolve o foco (--background_window implicito no modo automatico).
#
# Uso: pwsh -File tools\smoke_port.ps1 [-GameDir <pasta>] [-OutDir <pasta>] [-ShotSeconds 75]
param(
  [string]$GameDir = (Join-Path $PSScriptRoot "..\..\Banjo Tooie BR"),
  [string]$OutDir = (Join-Path $PSScriptRoot "..\_audit\out\m01\smoke"),
  [double]$ShotSeconds = 75,
  [double]$TimeoutSeconds = 240
)
$ErrorActionPreference = "Stop"
$App = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$GameDir = (Resolve-Path $GameDir).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Exe = Join-Path $App "banjotooie-recomp\out\build\win-amd64-release\banjotooie.exe"
if (-not (Test-Path $Exe)) { throw "executavel nao encontrado: $Exe (rode tools\build_port.cmd)" }
$Inventory = Join-Path $App "_audit\out\inventory.json"
$results = [ordered]@{ started = (Get-Date).ToString("s"); exe = $Exe; game_dir = $GameDir; steps = @() }

function Add-Step($name, $passed, $detail, $extra = @{}) {
  $row = [ordered]@{ step = $name; passed = [bool]$passed; detail = $detail }
  foreach ($k in $extra.Keys) { $row[$k] = $extra[$k] }
  $script:results.steps += $row
  $tag = if ($passed) { "PASS" } else { "FAIL" }
  Write-Host ("[{0}] {1}: {2}" -f $tag, $name, $detail)
}

function Run-Port([string[]]$argv, [int]$timeoutS) {
  $p = Start-Process -FilePath $Exe -ArgumentList $argv -PassThru -WindowStyle Normal
  if (-not $p.WaitForExit($timeoutS * 1000)) {
    Stop-Process -Id $p.Id -Force
    return -999
  }
  return $p.ExitCode
}

function Quote([string]$s) { '"' + $s + '"' }

# 0. hashes antes
python -I (Join-Path $App "_audit\verify_baseline.py") $GameDir $Inventory (Join-Path $OutDir "hashes_antes.json") | Out-Null
$before = Get-Content (Join-Path $OutDir "hashes_antes.json") -Raw | ConvertFrom-Json
Add-Step "hashes_antes" ($before.summary.status -eq "PASS") ("{0}/{1} iguais" -f $before.summary.iguais, $before.summary.expected)

# 1. preflight na pasta real
$rep1 = Join-Path $OutDir "preflight_ok.json"
$code = Run-Port @("--game_data_root", (Quote $GameDir), "--preflight_only", "--boot_report", (Quote $rep1)) 60
$j = Get-Content $rep1 -Raw | ConvertFrom-Json
Add-Step "preflight_pasta_real" ($code -eq 0 -and $j.preflight.passed) ("codigo {0}; obrigatorios ausentes: {1}" -f $code, ($j.preflight.missing_required -join ", ")) @{ exit_code = $code }

# 2. pasta de teste sem db360.cmp (arquivos de teste com conteudo ficticio; so o XEX e copiado)
$fake = Join-Path $OutDir "pasta_incompleta"
if (Test-Path $fake) { Get-ChildItem $fake -Recurse -File | ForEach-Object { $_.IsReadOnly = $false } }
New-Item -ItemType Directory -Force (Join-Path $fake "RAWFiles") | Out-Null
Copy-Item (Join-Path $GameDir "default.xex") (Join-Path $fake "default.xex") -Force
foreach ($n in @("db360.textures.cmp", "banjo2_360.tbl", "banjo2_360.ctl")) {
  Set-Content -Path (Join-Path $fake "RAWFiles\$n") -Value "arquivo de teste" -NoNewline
}
$missing = Join-Path $fake "RAWFiles\db360.cmp"
if (Test-Path $missing) { Remove-Item -LiteralPath $missing -Force }
$rep2 = Join-Path $OutDir "preflight_faltando.json"
$code = Run-Port @("--game_data_root", (Quote $fake), "--preflight_only", "--boot_report", (Quote $rep2)) 60
$j = Get-Content $rep2 -Raw | ConvertFrom-Json
$cited = ($j.preflight.missing_required -contains "RAWFiles\db360.cmp")
Add-Step "preflight_arquivo_faltando" ($code -eq 2 -and $cited) ("codigo {0}; ausentes citados: {1}" -f $code, ($j.preflight.missing_required -join ", ")) @{ exit_code = $code }

# 3. sem pasta do jogo
$rep3 = Join-Path $OutDir "preflight_sem_pasta.json"
$code = Run-Port @("--preflight_only", "--boot_report", (Quote $rep3)) 60
Add-Step "preflight_sem_pasta" ($code -eq 3) ("codigo {0}" -f $code) @{ exit_code = $code }

# 4. jogo real ate a captura
$rep4 = Join-Path $OutDir "boot_jogo.json"
$shot = Join-Path $OutDir "captura_jogo.png"
$log = Join-Path $OutDir "banjotooie.log"
$user = Join-Path $OutDir "user_data"
$cache = Join-Path $OutDir "cache"
foreach ($f in @($rep4, $shot, $log)) { if (Test-Path $f) { Remove-Item -LiteralPath $f -Force } }
$sw = [Diagnostics.Stopwatch]::StartNew()
$code = Run-Port @("--game_data_root", (Quote $GameDir), "--user_data_root", (Quote $user), "--cache_path", (Quote $cache),
  "--audio_mute", "--log_file", (Quote $log), "--boot_report", (Quote $rep4), "--screenshot", (Quote $shot),
  "--screenshot_seconds", "$ShotSeconds", "--smoke_timeout_seconds", "$TimeoutSeconds") ([int]$TimeoutSeconds + 60)
$sw.Stop()
$j = if (Test-Path $rep4) { Get-Content $rep4 -Raw | ConvertFrom-Json } else { $null }
$frames = if ($j) { [int64]$j.guest.frames } else { 0 }
$shotOk = $j -and $j.screenshot -and $j.screenshot.ok -and (Test-Path $shot)
$stages = if ($j) { ($j.stages | ForEach-Object { $_.name }) -join " > " } else { "(sem relatorio)" }
Add-Step "jogo_inicia_e_apresenta_quadros" ($code -eq 0 -and $frames -gt 0) ("codigo {0}; {1} quadros; fps(ultimo s)={2}; {3:N0} s; etapas: {4}" -f $code, $frames, $j.guest.fps, $sw.Elapsed.TotalSeconds, $stages) @{ exit_code = $code; frames = $frames }
Add-Step "captura_png" $shotOk ($(if ($j -and $j.screenshot) { "{0}x{1}; {2}" -f $j.screenshot.width, $j.screenshot.height, $j.screenshot.detail } else { "sem captura" }))
if (Test-Path $shot) { $results.screenshot_sha256 = (Get-FileHash $shot -Algorithm SHA256).Hash.ToLower() }

# 5. falha provocada (so teste): o filtro do port deve gravar relatorio + minidump e sair com 0xE0425442
$rep5 = Join-Path $OutDir "boot_falha_teste.json"
$dumps = Join-Path $OutDir "crashdumps"
if (Test-Path $dumps) { Get-ChildItem $dumps -Filter *.dmp | Remove-Item -Force }
if (Test-Path $rep5) { Remove-Item -LiteralPath $rep5 -Force }
$code = Run-Port @("--game_data_root", (Quote $GameDir), "--user_data_root", (Quote $user), "--cache_path", (Quote $cache),
  "--audio_mute", "--boot_report", (Quote $rep5), "--port_test_crash") 120
$expected = [Convert]::ToUInt32("E0425442", 16)
$got = [uint32]([int64]$code -band 0xFFFFFFFFL)
$j = if (Test-Path $rep5) { Get-Content $rep5 -Raw | ConvertFrom-Json } else { $null }
$dmp = Get-ChildItem $dumps -Filter *.dmp -ErrorAction SilentlyContinue | Select-Object -First 1
$crashText = if ($j) { $j.crash } else { "(sem relatorio)" }
Add-Step "falha_provocada_gera_minidump" ($got -eq $expected -and $j -and $j.crash -and $dmp) ("codigo 0x{0:X8}; minidump: {1}; relatorio: {2}" -f $got, $(if ($dmp) { "$($dmp.Name) ($($dmp.Length) bytes)" } else { "nenhum" }), $crashText)

# 6. hashes depois
python -I (Join-Path $App "_audit\verify_baseline.py") $GameDir $Inventory (Join-Path $OutDir "hashes_depois.json") | Out-Null
$after = Get-Content (Join-Path $OutDir "hashes_depois.json") -Raw | ConvertFrom-Json
Add-Step "hashes_depois" ($after.summary.status -eq "PASS") ("{0}/{1} iguais; novos: {2}" -f $after.summary.iguais, $after.summary.expected, ($after.summary.novos -join ", "))

$results.finished = (Get-Date).ToString("s")
$results.passed = -not ($results.steps | Where-Object { -not $_.passed })
$results | ConvertTo-Json -Depth 6 | Set-Content -Path (Join-Path $OutDir "smoke_result.json") -Encoding utf8
Write-Host ("RESULTADO: {0}" -f $(if ($results.passed) { "PASS" } else { "FAIL" }))
if (-not $results.passed) { exit 1 }
