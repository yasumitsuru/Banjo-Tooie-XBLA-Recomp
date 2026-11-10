# MISSAO 02 (rodada 2) - exploracao: roda o port com o controle virtual e tira capturas a cada
# -Passo segundos, para descobrir em que tempo cada tela aparece. Nao grava nada na pasta do jogo;
# usuario e cache ficam em -OutDir. Uso:
#   pwsh -File _audit\m02\explorar_jogo.ps1 -OutDir <pasta nova> -Roteiro "start@26,a@30" \
#        -De 40 -Ate 150 -Passo 3
param(
  [Parameter(Mandatory)] [string]$OutDir,
  [Parameter(Mandatory)] [string]$Roteiro,
  [double]$De = 40, [double]$Ate = 150, [double]$Passo = 3,
  [string]$GameDir = (Join-Path $PSScriptRoot "..\..\..\Banjo Tooie BR"),
  [string]$Exe = (Join-Path $PSScriptRoot "..\..\out\native\banjotooie.exe"),
  [string]$Extra = ""
)
$ErrorActionPreference = "Stop"
$GameDir = (Resolve-Path $GameDir).Path
$Exe = (Resolve-Path $Exe).Path
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
function Quote([string]$s) { '"' + $s + '"' }
# Numeros sempre com ponto (o Windows desta maquina usa virgula decimal; o port exige ponto).
$inv = [cultureinfo]::InvariantCulture
function N($x) { ([double]$x).ToString($inv) }
$shots = @()
for ($t = $De; $t -lt $Ate; $t += $Passo) { $shots += [string]::Format($inv, "{0}={1}\t{2:000.0}.png", $t, $OutDir, $t) }
$argv = @("--game_data_root", (Quote $GameDir), "--user_data_root", (Quote "$OutDir\user"),
  "--cache_path", (Quote "$OutDir\cache"), "--audio_mute", "--boot_report", (Quote "$OutDir\boot.json"),
  "--port_test_input", $Roteiro, "--port_test_shots", (Quote ($shots -join ";")),
  "--screenshot", (Quote ([string]::Format($inv, "{0}\t{1:000.0}.png", $OutDir, $Ate))), "--screenshot_seconds", (N $Ate),
  "--smoke_timeout_seconds", (N ($Ate + 40)))
if ($Extra) { $argv += $Extra.Split(" ") }
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Exe -ArgumentList $argv -PassThru
if (-not $p.WaitForExit(([int]$Ate + 90) * 1000)) { Stop-Process -Id $p.Id -Force; $code = -999 } else { $code = $p.ExitCode }
$b = if (Test-Path "$OutDir\boot.json") { Get-Content "$OutDir\boot.json" -Raw | ConvertFrom-Json } else { $null }
$ti = if ($b) { ($b.stages | Where-Object name -eq "test_input" | Select-Object -Last 1).detail } else { "sem relatorio" }
"codigo={0} segundos={1:N0} capturas={2}" -f $code, $sw.Elapsed.TotalSeconds, @(Get-ChildItem "$OutDir\t*.png").Count
$ti
