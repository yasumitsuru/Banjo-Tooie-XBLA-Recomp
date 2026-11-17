@echo off
rem Compila o launcher portatil e monta out\portable\ (+ ZIP). Ver tools\build_launcher.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_launcher.ps1" %*
exit /b %ERRORLEVEL%
