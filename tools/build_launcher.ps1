# Compila o launcher portátil (app\launcher) e monta a pasta portátil + ZIP de distribuição.
#
#   tools\build_launcher.cmd            compila (Release x64) e monta out\portable\<pacote>\ e o .zip
#   tools\build_launcher.cmd -SoCompilar só compila para out\launcher\
#
# Usa o csc.exe do .NET Framework 4 que vem com o Windows (C# 5, WPF): nada a instalar.
# O pacote leva o launcher, o port já compilado (out\native\banjotooie.exe + rexruntime.dll), o
# runtime do Visual C++ (redistribuível da Microsoft), licenças e o LEIA-ME. NÃO leva nenhum
# arquivo do jogo (default.xex, RAWFiles...), nem o banjotooie.toml de desenvolvimento, nem saves.
param([switch]$SoCompilar)
$ErrorActionPreference = 'Stop'
$app = Split-Path -Parent $PSScriptRoot
$src = Join-Path $app 'launcher'
$version = '1.0.1'
$exeName = 'Banjo-Tooie PT-BR Recompiled.exe'

$fw = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$csc = Join-Path $fw 'csc.exe'
if (-not (Test-Path $csc)) { throw "csc.exe do .NET Framework 4 não encontrado em $fw" }
$wpf = Join-Path $fw 'WPF'

$outDir = Join-Path $app 'out\launcher'
New-Item -ItemType Directory -Force $outDir | Out-Null
$exe = Join-Path $outDir $exeName
$refs = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll',
  'System.IO.Compression.dll', 'System.IO.Compression.FileSystem.dll', 'System.Web.Extensions.dll',
  (Join-Path $fw 'System.Xaml.dll'), (Join-Path $wpf 'WindowsBase.dll'),
  (Join-Path $wpf 'PresentationCore.dll'), (Join-Path $wpf 'PresentationFramework.dll')) |
  ForEach-Object { "/reference:$_" }
$cscArgs = @('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/langversion:5', '/utf8output',
  "/out:$exe", "/win32icon:$(Join-Path $src 'assets\banjotooie.ico')",
  "/resource:$(Join-Path $src 'Launcher.xaml'),BanjoLauncher.Launcher.xaml",
  "/resource:$(Join-Path $src 'assets\banjotooie.png'),BanjoLauncher.banjotooie.png",
  "/resource:$(Join-Path $src 'assets\banjotooie.ico'),BanjoLauncher.banjotooie.ico") + $refs +
  @((Join-Path $src 'Core.cs'), (Join-Path $src 'Lang.cs'), (Join-Path $src 'Launcher.cs'))
Write-Host "[launcher] compilando $exeName (C# 5, x64, Release)..."
& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "csc falhou (código $LASTEXITCODE)" }
Write-Host "[launcher] OK: $exe ($((Get-Item $exe).Length) bytes)"
if ($SoCompilar) { exit 0 }

# ---------- pasta portátil ----------
$native = Join-Path $app 'out\native'
foreach ($f in 'banjotooie.exe', 'rexruntime.dll') {
  if (-not (Test-Path (Join-Path $native $f))) { throw "Falta $native\${f}: compile o port antes (tools\build_native.cmd)." }
}
$vcDir = Get-ChildItem 'C:\Program Files*\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT' -Directory -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $vcDir) { throw 'Runtime redistribuível do Visual C++ (Microsoft.VC143.CRT x64) não encontrado.' }

$pkgName = 'Banjo-Tooie-PT-BR-Recompiled'
$portable = Join-Path $app 'out\portable'
$pkg = Join-Path $portable $pkgName
if (Test-Path $pkg) { Remove-Item -Recurse -Force $pkg }
New-Item -ItemType Directory -Force (Join-Path $pkg 'bin'), (Join-Path $pkg 'LICENCAS\terceiros') | Out-Null
Copy-Item $exe (Join-Path $pkg $exeName)
Copy-Item (Join-Path $native 'banjotooie.exe'), (Join-Path $native 'rexruntime.dll') (Join-Path $pkg 'bin')
foreach ($d in 'msvcp140.dll', 'msvcp140_atomic_wait.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
  Copy-Item (Join-Path $vcDir.FullName $d) (Join-Path $pkg 'bin')
}
Copy-Item (Join-Path $src 'LICENSE-GPL-3.0.txt') (Join-Path $pkg 'LICENCAS\launcher-GPL-3.0.txt')
Copy-Item (Join-Path $app 'rexglue-sdk\LICENSE') (Join-Path $pkg 'LICENCAS\ReXGlue-SDK-BSD-3-Clause.txt')
# Licenças das bibliotecas de terceiros do runtime (rexglue-sdk\thirdparty): todas as que existem.
Get-ChildItem (Join-Path $app 'rexglue-sdk\thirdparty') -Directory | ForEach-Object {
  $lib = $_.Name
  $files = Get-ChildItem $_.FullName -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING)' }
  if (-not $files) { $files = Get-ChildItem $_.FullName -File -Recurse -Depth 2 -ErrorAction SilentlyContinue | Where-Object { $_.Name -match '^(LICEN[CS]E|COPYING)' } | Select-Object -First 3 }
  foreach ($f in $files) { Copy-Item $f.FullName (Join-Path $pkg "LICENCAS\terceiros\$lib-$($f.Name)") }
}
$vcLic = @"
Runtime do Microsoft Visual C++ 2015-2022 (x64): msvcp140.dll, msvcp140_atomic_wait.dll,
vcruntime140.dll e vcruntime140_1.dll, copiados de $($vcDir.Name) do Visual Studio 2022 Build Tools.
São arquivos redistribuíveis da Microsoft (lista "Distributable Code" dos termos de licença do
Visual Studio), incluídos para o jogo abrir sem instalar o pacote do Visual C++.
"@
[IO.File]::WriteAllText((Join-Path $pkg 'LICENCAS\Microsoft-Visual-C++-runtime.txt'), $vcLic, (New-Object Text.UTF8Encoding($true)))
foreach ($t in 'LEIA-ME.txt', 'CREDITOS.txt', 'README-EN.txt', 'CREDITS-EN.txt') {
  $text = [IO.File]::ReadAllText((Join-Path $src "pacote\$t"), [Text.Encoding]::UTF8) -replace "`r?`n", "`r`n"
  [IO.File]::WriteAllText((Join-Path $pkg $t), $text, (New-Object Text.UTF8Encoding($true)))
}

# Nenhum arquivo do jogo pode entrar no pacote.
$forbidden = Get-ChildItem $pkg -Recurse -File | Where-Object { $_.Name -match '^(default\.xex|db360.*\.cmp|X360_strings\.dat|banjo2_360\.(tbl|ctl)|xarialuni\.ttf|Arcade(Normal|Wide)\.ptc|58410955.*|savegame\.dat|banjotooie\.toml|pc_settings\.toml)$' }
if ($forbidden) { throw "Arquivo proibido no pacote: $($forbidden.FullName -join ', ')" }

# Manifesto com o SHA-256 de cada arquivo (conferência depois de copiar/mover).
$sha = [Security.Cryptography.SHA256]::Create()
$manifest = Get-ChildItem $pkg -Recurse -File | Sort-Object FullName | ForEach-Object {
  $s = [IO.File]::OpenRead($_.FullName)
  try { $h = ([BitConverter]::ToString($sha.ComputeHash($s)) -replace '-', '').ToLower() } finally { $s.Dispose() }
  '{0}  {1}' -f $h, $_.FullName.Substring($pkg.Length + 1)
}
[IO.File]::WriteAllLines((Join-Path $pkg 'MANIFESTO-SHA256.txt'), $manifest, (New-Object Text.UTF8Encoding($false)))

$zip = Join-Path $portable "$pkgName-$version-win64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($pkg, $zip, [IO.Compression.CompressionLevel]::Optimal, $true)
$size = (Get-ChildItem $pkg -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("[launcher] pasta portátil: {0} ({1} arquivos, {2:N1} MB)" -f $pkg, (Get-ChildItem $pkg -Recurse -File).Count, ($size / 1MB))
Write-Host ("[launcher] ZIP: {0} ({1:N1} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
