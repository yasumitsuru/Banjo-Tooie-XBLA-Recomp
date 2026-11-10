# MISSAO 00 - checagem de dependencias (somente leitura). Uso: pwsh -File check_tools.ps1 <saida.log>
param([string]$Log)
$ErrorActionPreference = 'Continue'
function Out-Log([string]$t) { Add-Content -Encoding utf8 -Path $Log -Value $t }
Set-Content -Encoding utf8 -Path $Log -Value "# Checagem de dependencias - $(Get-Date -Format o)"

$cmds = @(
  @('git', '--version'), @('cmake', '--version'), @('ninja', '--version'),
  @('clang', '--version'), @('python', '--version'), @('java', '-version')
)
foreach ($c in $cmds) {
  Out-Log ""; Out-Log ("> " + ($c -join ' '))
  $exe = Get-Command $c[0] -ErrorAction SilentlyContinue
  if (-not $exe) { Out-Log "NAO ENCONTRADO"; continue }
  Out-Log ("caminho: " + $exe.Source)
  $o = & $c[0] $c[1] 2>&1 | Select-Object -First 2
  Out-Log (($o | ForEach-Object { "$_" }) -join "`n")
}

Out-Log ""; Out-Log "> Visual Studio / MSVC (vswhere)"
$pf86 = [Environment]::GetFolderPath('ProgramFilesX86')
$vsw = Join-Path $pf86 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path $vsw) {
  $vs = & $vsw -all -products '*' -format json | ConvertFrom-Json
  foreach ($v in $vs) {
    Out-Log ("{0} | {1} | {2}" -f $v.displayName, $v.installationVersion, $v.installationPath)
    $msvc = Join-Path $v.installationPath 'VC\Tools\MSVC'
    if (Test-Path $msvc) { Out-Log ("  MSVC: " + ((Get-ChildItem $msvc -Directory | Select-Object -ExpandProperty Name) -join ', ')) }
  }
} else { Out-Log "vswhere ausente" }

Out-Log ""; Out-Log "> Windows SDK (Include)"
$sdk = Join-Path $pf86 'Windows Kits\10\Include'
if (Test-Path $sdk) { Out-Log ((Get-ChildItem $sdk -Directory | Select-Object -ExpandProperty Name) -join ', ') } else { Out-Log "ausente" }

Out-Log ""; Out-Log "> Vulkan runtime (System32\vulkan-1.dll)"
Out-Log ("presente: " + (Test-Path (Join-Path $env:WINDIR 'System32\vulkan-1.dll')))

Out-Log ""; Out-Log "> GPU / monitor atual"
foreach ($g in Get-CimInstance Win32_VideoController) {
  Out-Log ("{0} | driver {1} | {2}x{3} @ {4} Hz" -f $g.Name, $g.DriverVersion, $g.CurrentHorizontalResolution, $g.CurrentVerticalResolution, $g.CurrentRefreshRate)
}

Out-Log ""; Out-Log "> Xenia (emulador; necessario so para rodar o original)"
$x = Get-ChildItem -Path 'C:\', 'W:\' -Filter 'xenia*.exe' -Recurse -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 3 -ExpandProperty FullName
if ($x) { Out-Log ($x -join "`n") } else { Out-Log "nao encontrado (busca ate 3 niveis em C:\ e W:\)" }

Out-Log ""; Out-Log "> Ghidra configurado"
foreach ($p in @('C:\AI\ReverseEngineering\ghidra')) { Out-Log ("{0}: {1}" -f $p, (Test-Path $p)) }
