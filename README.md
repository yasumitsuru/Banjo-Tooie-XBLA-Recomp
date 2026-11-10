This build contains the original game's own files, for the private use of the person who owns the game, on their own computer. It must not be shared or published.

# Banjo-Tooie XBLA PT-BR Recompiled — estado do projeto

Projeto de port nativo para Windows 11 (x86-64) do Banjo-Tooie (XBLA), com recompilação estática (ReXGlue), visando 4K/60 FPS e interface em PT-BR, preservando o jogo original.

**Status atual (após a MISSÃO 01):** o `default.xex` original foi descompilado
(Ghidra, PowerPC) e os valores reais do jogo estão documentados em `MECHANICS.md`.
Nada do jogo foi modificado; nenhuma funcionalidade nova foi implementada.

- Relatório completo da auditoria: `docs/AUDITORIA-MISSAO-00.md` (inventário, matriz de requisitos, bloqueios, ordem de implementação)
- **Valores reais do original (missão 01): `MECHANICS.md`** — boot, idioma, diálogo de erro, ajustes de vídeo, áudio 48 kHz, cadência 60 FPS, imports — tudo com endereços reais do XEX (base `0x82000000`)
- Ferramentas de extração (regeneram os dados): `_audit/xex_unpack.py`, `_audit/DumpXex.java` + `_audit/run_headless.cmd`
- Evidências das verificações: `_audit/out/` (inventário, imagem desempacotada, mapas do Ghidra — pasta não commitada, regenerável)
- Arquivos originais do jogo: fora desta pasta, em `..\Banjo Tooie BR` (não modificados)

**Próximo passo (missão 02):** usar os endereços de `MECHANICS.md` para trazer a base do
port e gerar o código recompilado a partir do executável original, comparando com a base
antes de compilar.
