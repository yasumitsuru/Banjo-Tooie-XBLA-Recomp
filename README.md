This build contains the original game's own files, for the private use of the person who owns the game, on their own computer. It must not be shared or published.

# Banjo-Tooie XBLA PT-BR Recompiled — estado do projeto

Projeto de port nativo para Windows 11 (x86-64) do Banjo-Tooie (XBLA), por recompilação estática
(ReXGlue), visando 4K/60 FPS e PT-BR, preservando o jogo original.

**Status atual: MISSÃO 00 (auditoria) concluída, revisão 2.** Ainda não existe código do port
nem executável recompilado aqui; nenhuma funcionalidade foi implementada; os arquivos do jogo
não foram modificados (verificado por hash antes e depois).

- **Relatório da auditoria:** `docs/AUDITORIA-MISSAO-00.md` (inventário, comparação com a base,
  testes executados, matriz IMPLEMENTADO/PARCIAL/PENDENTE/BLOQUEADO, mapa de módulos, ordem de
  implementação, correções da rev. 1)
- **Evidências versionadas:** `docs/evidencias/` (hashes, integridade do XEX, 49/49 overrides,
  patches × XEX, smoke test do original, ferramentas, HEADs remotos)
- **Valores reais do executável original:** `MECHANICS.md` (endereços do XEX, base `0x82000000`)
- **Ferramentas de auditoria (reproduzíveis, só leitura):** `_audit/verify_baseline.py`,
  `_audit/verify_xex_integrity.py`, `_audit/check_overrides.py`, `_audit/check_xenia_patches.py`,
  `_audit/check_tools.ps1`, além de `_audit/xex_unpack.py` e `_audit/run_headless.cmd` (Ghidra)
- `_audit/out/`: saídas regeneráveis e capturas do jogo (não versionadas)
- Arquivos originais do jogo: fora desta pasta, em `..\Banjo Tooie BR` (somente leitura)

**Próximo passo (só quando for pedido):** trazer a base do port e gerar o código recompilado a
partir do seu executável (M1 + M2 do relatório).
