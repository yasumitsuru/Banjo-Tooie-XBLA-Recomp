BANJO-TOOIE XBLA PT-BR RECOMPILED — PORTABLE LAUNCHER
====================================================

Native Windows (x64) port of the Xbox Live Arcade Banjo-Tooie, recompiled from the Xbox 360 code —
it is not an emulator. This folder does NOT contain any game files: you use your own copy of
Banjo-Tooie XBLA, already extracted.

The launcher is available in Portuguese (Brazil) and English: use the IDIOMA · LANGUAGE selector at
the bottom of the sidebar. The first time, it follows the Windows language. The language of the
game itself comes from your game files (PT-BR translation or the English global version).

WHAT YOU NEED
- 64-bit Windows 10 or 11, a graphics card with DirectX 12.
- Your extracted Banjo-Tooie XBLA folder: it has the default.xex file and the RAWFiles folder
  (with db360.cmp, db360.textures.cmp, X360_strings.dat, banjo2_360.tbl and others).
  The port targets the PT-BR translated version; the global (English) version also runs.
- Nothing to install: the Visual C++ runtime is included and the .NET Framework 4 ships with Windows.

HOW TO USE
1. Extract the whole ZIP into a folder you can write to (for example D:\Games\Banjo-Tooie).
   Do not use "Program Files".
2. Open "Banjo-Tooie PT-BR Recompiled.exe".
3. On "Play", click "Browse…" and choose your extracted game folder.
   "Check" tells you whether the folder can be used and which version it is.
4. Leave "Copy the game files into this portable folder" on (recommended) and click
   PREPARE MY GAME. Your original files are never changed.
5. Adjust Graphics, Controls and Audio & game if you like (SAVE SETTINGS) and click PLAY.
   Inside the game, F1 opens the same PC Settings.

PORTABLE FOLDER
  Banjo-Tooie PT-BR Recompiled.exe   the launcher
  bin\        the port (banjotooie.exe, rexruntime.dll and the Visual C++ runtime)
  jogo\       the copy of your game files (created when preparing)
  dados\      saves, automatic backups, PC settings, logs and crash reports
  cache\      graphics card shader cache
  config\     launcher.ini (the chosen game folder, stored relative to this folder, and the language)
  logs\       launcher logs and diagnostics reports
Everything stays here: nothing goes into the Windows Registry, Documents or AppData, and nothing is
downloaded from the internet. To move the game to another drive or computer, copy the whole folder.
(The graphics driver may keep its own cache outside this folder; that is up to the driver.)

SAVES
- The save lives in dados\. Automatic backups live in dados\backups.
- "Saves & backups" exports everything to a .zip and imports it back.
- If you played the port without this launcher, the save is in Documents\My Games\Banjo-Tooie XBLA:
  "Copy the save from Documents to this folder" copies it (the original is not touched).

TROUBLESHOOTING
- "Diagnostics" → "Create diagnostics report" writes a .txt in logs\ with what helps to investigate.
- Logs of each play session: dados\logs. Crash reports: dados\crashes.

NOTICE
Banjo-Tooie and its trademarks belong to Microsoft and Rare. The port program contains code
recompiled from the game and is for the personal use of people who own their copy. Do not share
the prepared folder (it then contains your game files). Credits and licenses: CREDITS-EN.txt and
LICENCAS\.
