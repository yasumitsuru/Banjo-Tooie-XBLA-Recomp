"""MISSAO 00 (rev. 3) - confere no codigo-fonte da ponte grafica (fork rexglue-sdk) cada
afirmacao usada no relatorio. Somente leitura: nao compila nem executa nada.

Cada verificacao procura um trecho exato num arquivo e registra arquivo:linha.
PASS = todas as afirmacoes encontradas; FAIL = alguma nao encontrada.

Uso: python -I check_sdk_fork.py <raiz_do_sdk_extraido> <saida.json>
"""
import json
import os
import re
import sys

# (id, requisito, arquivo relativo, regex, o que comprova)
CHECKS = [
    ("video_preset_cvar", "4K", "src/ui/window.cpp",
     r'REXCVAR_DEFINE_STRING\(resolution, "", "GPU",(?:.*\n){1,2}.*720p, 1080p, 1440p, 4k',
     "opcao 'resolution' (preset do modo de video do jogo + janela)"),
    ("video_preset_4k", "4K", "include/rex/graphics/video_mode_util.h",
     r'normalized == "4k"\) \{\s*\n\s*width_out = 3840;\s*\n\s*height_out = 2160;',
     "preset 4k = 3840x2160"),
    ("video_mode_default", "4K", "src/ui/window.cpp",
     r"REXCVAR_DEFINE_INT32\(video_mode_width, 1280,",
     "modo de video informado ao jogo: padrao 1280 de largura"),
    ("video_mode_height_default", "4K", "src/ui/window.cpp",
     r"REXCVAR_DEFINE_INT32\(video_mode_height, 720,",
     "modo de video informado ao jogo: padrao 720 de altura"),
    ("video_mode_clamp", "4K", "src/kernel/xboxkrnl/xboxkrnl_video.cpp",
     r"std::clamp\(configured_width, 640, 0x0FFF\)",
     "largura informada ao jogo limitada a 640..4095 (3840 cabe)"),
    ("resolution_scale", "4K", "src/graphics/pipeline/texture/cache.cpp",
     r"REXCVAR_DEFINE_INT32\(resolution_scale, 1,[\s\S]{0,200}\.range\(1, 8\)",
     "escala de renderizacao 1..8 (padrao 1 = sem escala)"),
    ("present_effects", "4K", "src/ui/presenter.cpp",
     r'\.allowed\(\{"bilinear", "cas", "fsr", "fsr2", "fsr3"\}\)',
     "efeitos de apresentacao/upscale: bilinear, cas, fsr, fsr2, fsr3"),
    ("vsync", "60 FPS", "src/graphics/command_processor.cpp",
     r'REXCVAR_DEFINE_BOOL\(vsync, true,',
     "vsync ligado por padrao (nao ha limitador/alvo de FPS proprio)"),
    ("refresh_rate", "60 FPS", "src/ui/window.cpp",
     r"REXCVAR_DEFINE_DOUBLE\(video_mode_refresh_rate, 60\.0,[\s\S]{0,120}\.range\(24\.0, 240\.0\)",
     "taxa de atualizacao informada ao jogo: 60 Hz (24..240)"),
    ("fps_overlay", "60 FPS (medicao)", "src/ui/overlay/debug_overlay.cpp",
     r'"Guest: %\.1f FPS \(%\.2f ms\)"',
     "overlay de depuracao mostra FPS do jogo"),
    ("fps_overlay_key", "60 FPS (medicao)", "src/ui/rex_app.cpp",
     r'RegisterBind\("bind_debug_overlay", "F3"',
     "overlay de FPS abre com F3"),
    ("perf_csv", "60 FPS (medicao)", "src/core/perf/counter.cpp",
     r"REXCVAR_DEFINE_STRING\(perf_log_csv, \"\"",
     "log de desempenho em CSV (perf_log_csv)"),
    ("settings_overlay_key", "menu START", "src/ui/rex_app.cpp",
     r'RegisterBind\("bind_settings", "F4"',
     "menu de ajustes do runtime (editor de opcoes) abre com F4 - nao e menu START do jogo"),
    ("settings_save", "menu START", "src/ui/overlay/settings_overlay.cpp",
     r'ImGui::Button\("Save to config"\)',
     "menu de ajustes grava as opcoes no arquivo de configuracao"),
    ("input_backend", "gamepad", "src/input/input_system.cpp",
     r'REXCVAR_DEFINE_STRING\(input_backend, "sdl", "Input", "Input backend: sdl, xinput"\)',
     "controle: SDL (padrao) ou XInput"),
    ("sdl_mappings", "gamepad", "src/input/sdl/sdl_input_driver.cpp",
     r'REXCVAR_DEFINE_STRING\(hid_mappings_file, "gamecontrollerdb\.txt"',
     "SDL usa gamecontrollerdb.txt para mapear controles"),
    ("mnk_mode", "gamepad", "src/input/mnk/mnk_input_driver.cpp",
     r'REXCVAR_DEFINE_BOOL\(mnk_mode, false,',
     "teclado como controle (desligado por padrao)"),
    ("mnk_start_key", "gamepad", "src/input/mnk/mnk_input_driver.cpp",
     r'REXCVAR_DEFINE_STRING\(keybind_start, "Escape"',
     "no teclado, START = Esc"),
    ("saves_user_dir", "saves", "src/ui/rex_app.cpp",
     r"user_dir = rex::filesystem::GetUserFolder\(\) / GetName\(\);",
     "saves/perfil vao para <pasta do usuario>/<nome do app>"),
    ("user_folder_documents", "saves", "src/core/filesystem_win.cpp",
     r"SHGetKnownFolderPath\(FOLDERID_Documents",
     "no Windows a pasta do usuario e Documentos"),
    ("game_folder_readonly", "preservacao", "src/system/runtime.cpp",
     r"mount_path, abs_game_root, !REXCVAR_GET\(allow_game_relative_writes\)\);",
     "pasta do jogo montada somente leitura salvo allow_game_relative_writes"),
    ("game_writes_default_off", "preservacao", "src/filesystem/virtual_file_system.cpp",
     r"REXCVAR_DEFINE_BOOL\(allow_game_relative_writes, false,",
     "allow_game_relative_writes desligado por padrao"),
    ("game_data_root", "dados", "src/system/runtime.cpp",
     r'REXCVAR_DEFINE_STRING\(game_data_root, "", "Runtime", "Override game data path"\);',
     "pasta dos dados do jogo por opcao game_data_root"),
    ("audio_dummy_fallback", "audio", "src/audio/sdl/sdl_audio_driver.cpp",
     r"falling back to the 'dummy' audio backend",
     "sem dispositivo de audio, cai para o backend 'dummy' (evita a tela preta vista no Xenia)"),
    ("license_full", "versao completa", "src/kernel/xam/xam_content.cpp",
     r"license_mask",
     "licenca tratada no runtime (fork: padrao licenciado/completo)"),
    ("xex_plain_supported", "codegen", "src/system/xex_module.cpp",
     r"case XEX_ENCRYPTION_NONE:\s*\n\s*if \(exe_length > uncompressed_size\) \{\s*\n\s*return 1;\s*\n\s*\}\s*\n\s*memcpy\(buffer, p, exe_length\);",
     "leitor de XEX aceita imagem sem cifra e sem compressao (copia direta)"),
    ("codegen_uses_runtime_loader", "codegen", "src/codegen/project_recompiler.cpp",
     r"runtime->LoadXexImage\(entryVfsPath\)",
     "o codegen carrega o XEX pelo mesmo leitor do runtime"),
    ("d3d12_default_win", "grafico", "src/ui/rex_app.cpp",
     r"#if REX_HAS_D3D12\s*\n\s*config_\.graphics = REX_GRAPHICS_BACKEND\(rex::graphics::d3d12::D3D12GraphicsSystem\);",
     "no Windows o backend grafico preferido e D3D12 (Vulkan como alternativa)"),
    ("headless_cvar", "testes", "src/kernel/xam/xam_ui.cpp",
     r"REXCVAR_DEFINE_BOOL\(headless, false,",
     "modo headless para dialogos do sistema (util para testes automatizados)"),
    ("tests_dir", "testes", "tests/CMakeLists.txt",
     r"PPC instruction tests",
     "o SDK tem testes de instrucoes PPC e testes unitarios (nao ha testes do jogo)"),
]


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def main():
    root, out = sys.argv[1:3]
    rows = []
    for cid, req, rel, pattern, claim in CHECKS:
        path = os.path.join(root, *rel.split("/"))
        row = {"id": cid, "requisito": req, "arquivo": rel, "afirmacao": claim}
        if not os.path.isfile(path):
            row.update(status="FAIL", motivo="arquivo nao encontrado")
        else:
            text = open(path, encoding="utf-8", errors="replace").read()
            m = re.search(pattern, text)
            if m:
                row.update(status="PASS", linha=line_of(text, m.start()),
                           trecho=m.group(0).splitlines()[0][:160])
            else:
                row.update(status="FAIL", motivo="trecho nao encontrado")
        rows.append(row)
    passed = sum(r["status"] == "PASS" for r in rows)
    summary = {"sdk_root": root, "verificacoes": len(rows), "pass": passed,
               "fail": [r["id"] for r in rows if r["status"] != "PASS"],
               "status": "PASS" if passed == len(rows) else "FAIL"}
    json.dump({"summary": summary, "rows": rows}, open(out, "w", encoding="utf-8"),
              indent=2, ensure_ascii=False)
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    for r in rows:
        print(f'{r["status"]:4} {r["id"]:28} {r["arquivo"]}:{r.get("linha", "-")}')


if __name__ == "__main__":
    main()
