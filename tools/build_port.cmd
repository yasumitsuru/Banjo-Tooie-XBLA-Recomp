@echo off
rem ===========================================================================
rem MISSAO 01 - build reproduzivel do port nativo (Windows 11 x86-64).
rem
rem Toolchain fixa (verificada nesta maquina, nada instalado):
rem   - Visual Studio 2022 Build Tools: MSVC 14.44 + Windows SDK (vcvars64.bat)
rem   - Clang 19.1.5 que vem com o VS 2022 (alvo x86_64-pc-windows-msvc; o SDK pede >= 18)
rem   - CMake 4.4 (C:\Program Files\CMake) e Ninja 1.13 (caminho completo, sem MSYS2 no PATH)
rem
rem Fases:
rem   1) compila o gerador "rexglue" do SDK (fork iron-jay/rexglue-sdk @ 035aa25, submodulo)
rem   2) rexglue codegen: gera o C++ a partir do default.xex da pasta do jogo (so leitura)
rem   3) compila banjotooie.exe + rexruntime.dll
rem
rem Uso:  tools\build_port.cmd [all|sdk|codegen|game]      (padrao: all)
rem Saidas: out\sdk (gerador), banjotooie-recomp\generated (codigo gerado, nao versionado),
rem         banjotooie-recomp\out\build\win-amd64-release (executavel). Logs em _audit\out\m01.
rem ===========================================================================
setlocal EnableExtensions
set "STAGE=%~1"
if "%STAGE%"=="" set "STAGE=all"

set "APP=%~dp0.."
for %%I in ("%APP%") do set "APP=%%~fI"
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "LLVM=%VSROOT%\VC\Tools\Llvm\x64\bin"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "NINJA=C:\msys64\clang64\bin\ninja.exe"
set "LOGS=%APP%\_audit\out\m01"
if not exist "%LOGS%" mkdir "%LOGS%"

for %%T in ("%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" "%LLVM%\clang++.exe" "%CMAKE%" "%NINJA%") do (
  if not exist "%%~T" (
    echo [ERRO] ferramenta nao encontrada: %%~T
    exit /b 10
  )
)

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || (echo [ERRO] vcvars64 falhou & exit /b 11)
set "PATH=%LLVM%;C:\Program Files\CMake\bin;%PATH%"

rem REXGLUE_ENABLE_TRACY=OFF: o perfilador Tracy (TracyClient.dll, que escuta na rede para
rem profiling remoto) nao e necessario para jogar, e a DLL sumiu da pasta logo apos ser ligada
rem nesta maquina (antivirus Kaspersky ativo; inferido). Sem Tracy o runtime nao muda o jogo.
set COMMON=-G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" "-DCMAKE_C_COMPILER=%LLVM%\clang.exe" "-DCMAKE_CXX_COMPILER=%LLVM%\clang++.exe" -DCMAKE_C_FLAGS=-march=x86-64-v3 -DCMAKE_CXX_FLAGS=-march=x86-64-v3 -DREXGLUE_ENABLE_TRACY=OFF

if /i "%STAGE%"=="all" goto :sdk
if /i "%STAGE%"=="sdk" goto :sdk
if /i "%STAGE%"=="codegen" goto :codegen
if /i "%STAGE%"=="game" goto :game
echo [ERRO] fase desconhecida: %STAGE%
exit /b 12

:sdk
echo === [1/3] SDK: gerador rexglue ===
rem Links simbolicos do Git viram texto no Windows (libmspack); copia os destinos. Idempotente.
python -I "%APP%\tools\materialize_symlinks.py" "%APP%\rexglue-sdk" > "%LOGS%\0_symlinks.log" 2>&1 || (echo [ERRO] materialize_symlinks falhou, ver %LOGS%\0_symlinks.log & exit /b 20)
"%CMAKE%" -S "%APP%\rexglue-sdk" -B "%APP%\out\sdk" %COMMON% -DCMAKE_BUILD_TYPE=Release -DREXGLUE_BUILD_TESTS=OFF > "%LOGS%\1_sdk_configure.log" 2>&1 || (echo [ERRO] configure do SDK falhou, ver %LOGS%\1_sdk_configure.log & exit /b 21)
"%CMAKE%" --build "%APP%\out\sdk" --target rexglue > "%LOGS%\1_sdk_build.log" 2>&1 || (echo [ERRO] build do gerador falhou, ver %LOGS%\1_sdk_build.log & exit /b 22)
if /i "%STAGE%"=="sdk" goto :done

:codegen
echo === [2/3] codegen a partir do default.xex ===
rem O SDK grava seus binarios em rexglue-sdk\out\win-amd64 (REXGLUE_OUTPUT_DIR do proprio SDK).
set "REXGLUE=%APP%\rexglue-sdk\out\win-amd64\rexglue.exe"
if not exist "%REXGLUE%" (echo [ERRO] rexglue.exe nao encontrado: %REXGLUE% & exit /b 31)
pushd "%APP%\banjotooie-recomp"
"%REXGLUE%" --force codegen banjotooie_manifest.toml > "%LOGS%\2_codegen.log" 2>&1
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (echo [ERRO] codegen falhou ^(%RC%^), ver %LOGS%\2_codegen.log & exit /b 32)
if /i "%STAGE%"=="codegen" goto :done

:game
echo === [3/3] jogo: banjotooie.exe ===
pushd "%APP%\banjotooie-recomp"
"%CMAKE%" --preset win-amd64-release %COMMON% -DREXSDK_DIR=../rexglue-sdk > "%LOGS%\3_game_configure.log" 2>&1
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (popd & echo [ERRO] configure do jogo falhou, ver %LOGS%\3_game_configure.log & exit /b 41)
"%CMAKE%" --build --preset win-amd64-release --target banjotooie > "%LOGS%\3_game_build.log" 2>&1
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (echo [ERRO] build do jogo falhou, ver %LOGS%\3_game_build.log & exit /b 42)

:done
echo === OK: fase %STAGE% concluida ===
exit /b 0
