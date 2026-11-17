@echo off
rem Craft Anything's npm build entry point delegates to the real native CMake target.
rem Uses the same local toolchain and generated game sources as tools\build_port.cmd.
setlocal EnableExtensions
set "APP=%~dp0.."
for %%I in ("%APP%") do set "APP=%%~fI"
set "BUILD=%APP%\out\native"
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "LLVM=%VSROOT%\VC\Tools\Llvm\x64\bin"
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "NINJA=C:\msys64\clang64\bin\ninja.exe"

for %%T in ("%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" "%LLVM%\clang.exe" "%LLVM%\clang++.exe" "%CMAKE%" "%NINJA%") do (
  if not exist "%%~T" (
    echo [ERROR] Required local build tool not found: %%~T
    exit /b 10
  )
)
if not exist "%APP%\banjotooie-recomp\generated\rexglue.cmake" (
  echo [ERROR] Generated game sources are missing. Run tools\build_port.cmd all first.
  exit /b 11
)

rem Valores reais do jogo (decompilados do default.xex, MECHANICS.md ?12) viram
rem banjotooie-recomp\src\pc\mechanics_values.h e sao conferidos contra pc_hooks.toml.
rem Se a tabela de ganchos divergir dos valores decompilados, o build para aqui.
python -I "%APP%\tools\gen_mechanics_header.py"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
set "PATH=%LLVM%;C:\Program Files\CMake\bin;%PATH%"

"%CMAKE%" -S "%APP%\banjotooie-recomp" -B "%BUILD%" -G Ninja ^
  "-DCMAKE_MAKE_PROGRAM=%NINJA%" ^
  "-DCMAKE_C_COMPILER=%LLVM%\clang.exe" ^
  "-DCMAKE_CXX_COMPILER=%LLVM%\clang++.exe" ^
  "-DCMAKE_BUILD_TYPE=Release" ^
  "-DCMAKE_C_FLAGS=-march=x86-64-v3" ^
  "-DCMAKE_CXX_FLAGS=-march=x86-64-v3" ^
  "-DREXGLUE_ENABLE_TRACY=OFF" ^
  "-DREXGLUE_BUILD_TESTS=OFF" ^
  "-DREXSDK_DIR=%APP%\rexglue-sdk"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CMAKE%" --build "%BUILD%" --target banjotooie banjotooie_craft_launcher banjotooie_seletor --parallel 4
if errorlevel 1 exit /b %ERRORLEVEL%

rem Launcher portatil (app\launcher: C# 5/WPF, compilado com o csc do .NET Framework do Windows) e
rem a pasta portatil out\portable\ + ZIP com o port recem-compilado. Ver tools\build_launcher.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%APP%\tools\build_launcher.ps1"
exit /b %ERRORLEVEL%
