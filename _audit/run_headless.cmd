@echo off
rem Regenera o dump headless do Ghidra (XEXLoaderWV) sobre o default.xex original.
rem Fase 1: importa + analisa (projeto em _audit\out\ghidra_xex_proj).
rem Fase 2: roda DumpXex.java (funções, refs, call edges) em _audit\out\ghidra_xex_out.
if not exist "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_proj" mkdir "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_proj"

"C:\AI\ReverseEngineering\ghidra\support\analyzeHeadless.bat" "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_proj" xex_dump -import "W:\Decomp\Banjo 3\Banjo Tooie BR\default.xex" -analysisTimeoutPerFile 7200 < NUL > "W:\Decomp\Banjo 3\app\_audit\out\ghidra_headless.log" 2>&1
if errorlevel 1 goto :done

"C:\AI\ReverseEngineering\ghidra\support\analyzeHeadless.bat" "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_proj" xex_dump -process "default.xex" -noanalysis -scriptPath "W:\Decomp\Banjo 3\app\_audit" -postScript DumpXex.java "W:\Decomp\Banjo 3\app\_audit\out\ghidra_xex_out" < NUL >> "W:\Decomp\Banjo 3\app\_audit\out\ghidra_headless.log" 2>&1
:done
