@echo off
rem MISSAO 02 - decompila enderecos no projeto Ghidra headless (XEXLoaderWV) ja analisado.
rem Uso: _audit\decompile_at.cmd <pasta_saida> <end1> [end2 ...]
"C:\AI\ReverseEngineering\ghidra\support\analyzeHeadless.bat" "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_proj" xex_dump -process "default.xex" -noanalysis -readOnly -scriptPath "W:\Decomp\Banjo 3\app\_audit" -postScript DecompileAt.java %* < NUL > "W:\Decomp\Banjo 3\app\_audit\out\ghidra_decompile.log" 2>&1
